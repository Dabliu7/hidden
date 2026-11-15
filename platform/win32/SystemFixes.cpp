// Correcoes de compatibilidade com Windows moderno. Nenhuma depende de
// enderecos do gta_sa.exe: atuam sobre APIs do sistema (user32 / DirectInput).
#include "win32/SystemFixes.h"

#include <windows.h>
#include <unknwn.h>

#include <cstdint>

#include <cstring>

#include <MinHook.h>

namespace coop::win32::fixes {

namespace {

void (*g_log)(const char*, ...) = nullptr;

// ---------------------------------------------------------------------------
// Resolucao / DPI
// Com escala de tela acima de 100 %, o Windows "virtualiza" a area de trabalho
// para programas antigos: o jogo enxerga uma resolucao menor que a real, a
// imagem sai borrada/cortada e o cursor fica deslocado. Declarar o processo
// DPI-aware desliga essa virtualizacao.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Mouse
// O jogo le o mouse por DirectInput. Quando a janela perde o foco (alt-tab,
// troca de modo de video, sobreposicoes), o dispositivo fica "nao adquirido" e
// GetDeviceState passa a falhar - o mouse para de responder no jogo. Aqui,
// qualquer leitura que falhe por esse motivo readquire o dispositivo e repete.
//
// Alem disso o jogo pede o mouse em modo EXCLUSIVO. No Windows 8 em diante a
// aquisicao exclusiva falha com frequencia (ao sair do menu, por exemplo) e o
// mouse fica morto: sem clique e sem girar a camera. Forcamos o modo
// NAO-exclusivo, a mesma abordagem das correcoes conhecidas da comunidade.
// ---------------------------------------------------------------------------
constexpr GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// Indices nas vtables COM (dinput.h)
constexpr int kDI8_CreateDevice    = 3;
constexpr int kDev_Acquire         = 7;
constexpr int kDev_GetDeviceState  = 9;
constexpr int kDev_SetCoopLevel    = 13;

constexpr DWORD kExclusive    = 0x1; // DISCL_EXCLUSIVE
constexpr DWORD kNonExclusive = 0x2; // DISCL_NONEXCLUSIVE

using CreateDevice_t   = HRESULT(WINAPI*)(void* self, const GUID& guid, void** device, void* outer);
using GetDeviceState_t = HRESULT(WINAPI*)(void* self, DWORD size, void* data);
using Acquire_t        = HRESULT(WINAPI*)(void* self);
using SetCoopLevel_t   = HRESULT(WINAPI*)(void* self, HWND window, DWORD flags);

CreateDevice_t   g_origCreateDevice = nullptr;
GetDeviceState_t g_origGetDeviceState = nullptr;
Acquire_t        g_origAcquire = nullptr;
SetCoopLevel_t   g_origSetCoopLevel = nullptr;
void*            g_mouse = nullptr;
int              g_logBudget = 24; // diagnostico limitado: nao encher o log
bool             g_loggedRecovery = false;
bool             g_gameplay = false;  // o jogo pediu modo exclusivo => esta em jogo, nao em menu
bool             g_skipDelta = true;  // descarta o primeiro deslocamento apos entrar em jogo

void** VTable(void* object) { return *reinterpret_cast<void***>(object); }

bool PatchSlot(void** vtable, int index, void* replacement, void** original) {
    DWORD old;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *original = vtable[index];
    vtable[index] = replacement;
    VirtualProtect(&vtable[index], sizeof(void*), old, &old);
    return true;
}

HRESULT WINAPI Hook_SetCoopLevel(void* self, HWND window, DWORD flags) {
    if (self != g_mouse) return g_origSetCoopLevel(self, window, flags);
    // O jogo pede exclusivo (0x5) durante a partida e nao-exclusivo (0x6) nos
    // menus, onde o cursor e o do Windows e nao pode ser preso no centro.
    bool gameplay = (flags & kExclusive) != 0;
    if (gameplay && !g_gameplay) g_skipDelta = true;
    g_gameplay = gameplay;
    DWORD fixed = (flags & ~kExclusive) | kNonExclusive;
    HRESULT hr = g_origSetCoopLevel(self, window, fixed);
    if (g_logBudget > 0) {
        --g_logBudget;
        g_log("[coop] mouse: modo cooperativo 0x%X -> 0x%X (resultado 0x%08X).", flags, fixed, hr);
    }
    return hr;
}

HRESULT WINAPI Hook_Acquire(void* self) {
    HRESULT hr = g_origAcquire(self);
    if (self == g_mouse && FAILED(hr) && g_logBudget > 0) {
        --g_logBudget;
        g_log("[coop] mouse: Acquire falhou (0x%08X).", hr);
    }
    return hr;
}

// Layout de DIMOUSESTATE / DIMOUSESTATE2 (os 16 primeiros bytes sao iguais).
struct MouseState {
    LONG x, y, wheel;
    BYTE buttons[4];
};

bool WeAreForeground(HWND* window) {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg || !GetWindowThreadProcessId(fg, &pid) || pid != GetCurrentProcessId()) return false;
    *window = fg;
    return true;
}

// Plano B: se o DirectInput entrega o mouse "mudo" (sem movimento e sem
// botoes enquanto o Windows ve o mouse sendo usado), passamos a preencher o
// estado com as APIs comuns do Windows. A roda do mouse nao e coberta.
bool  g_fallback = false;

int   g_diAlive = 0;       // leituras em que o DirectInput trouxe algo
int   g_deadEvidence = 0;  // leituras mudas com o mouse em uso segundo o Windows
POINT g_lastCursor = {};
DWORD g_calls = 0, g_failures = 0, g_lastReport = 0;
int   g_reports = 6;

void FillFromWindows(HWND window, MouseState* st) {
    RECT rc;
    POINT center = {0, 0}, pos;
    if (!GetClientRect(window, &rc) || !GetCursorPos(&pos)) return;
    center.x = (rc.right - rc.left) / 2;
    center.y = (rc.bottom - rc.top) / 2;
    ClientToScreen(window, &center);
    st->x = st->y = st->wheel = 0;
    if (g_gameplay) {
        if (!g_skipDelta) {
            st->x = pos.x - center.x;
            st->y = pos.y - center.y;
        }
        g_skipDelta = false;
        SetCursorPos(center.x, center.y); // proxima leitura mede o deslocamento a partir do centro
    }
    st->buttons[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 0x80 : 0;
    st->buttons[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 0x80 : 0;
    st->buttons[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 0x80 : 0;
    st->buttons[3] = 0;
}

HRESULT WINAPI Hook_GetDeviceState(void* self, DWORD size, void* data) {
    HRESULT hr = g_origGetDeviceState(self, size, data);
    if (self != g_mouse) return hr;

    if (FAILED(hr) && SUCCEEDED(g_origAcquire(self))) {
        hr = g_origGetDeviceState(self, size, data);
        if (!g_loggedRecovery) {
            g_loggedRecovery = true;
            g_log("[coop] mouse: dispositivo readquirido apos perda de foco.");
        }
    }
    ++g_calls;
    if (FAILED(hr)) ++g_failures;

    HWND window = nullptr;
    if (data && size >= sizeof(MouseState) && WeAreForeground(&window)) {
        auto* st = static_cast<MouseState*>(data);
        if (FAILED(hr)) std::memset(data, 0, size);
        bool diActive = SUCCEEDED(hr) && (st->x || st->y || st->wheel || st->buttons[0] || st->buttons[1] ||
                                          st->buttons[2]);
        if (diActive) ++g_diAlive;

        if (!g_fallback) {
            POINT pos;
            bool osActive = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
            if (GetCursorPos(&pos)) {
                osActive = osActive || pos.x != g_lastCursor.x || pos.y != g_lastCursor.y;
                g_lastCursor = pos;
            }
            if (osActive && !diActive && g_diAlive == 0 && ++g_deadEvidence > 30) {
                g_fallback = true;
                g_log("[coop] mouse: DirectInput mudo (%lu leituras, %lu falhas); usando leitura direta do Windows.",
                      g_calls, g_failures);
            }
        }
        if (g_fallback) {
            FillFromWindows(window, st);
            hr = S_OK;
        }
    }

    DWORD now = GetTickCount();
    if (g_reports > 0 && now - g_lastReport > 5000) {
        g_lastReport = now;
        --g_reports;
        g_log("[coop] mouse: leituras=%lu falhas=%lu com-dados=%d plano-B=%d.", g_calls, g_failures, g_diAlive,
              g_fallback ? 1 : 0);
    }
    return hr;
}

HRESULT WINAPI Hook_CreateDevice(void* self, const GUID& guid, void** device, void* outer) {
    HRESULT hr = g_origCreateDevice(self, guid, device, outer);
    if (FAILED(hr) || !device || !*device || std::memcmp(&guid, &kGuidSysMouse, sizeof(GUID)) != 0) return hr;

    g_mouse = *device;
    if (!g_origGetDeviceState) {
        void *state = nullptr, *acquire = nullptr, *coop = nullptr;
        void** vt = VTable(g_mouse);
        if (PatchSlot(vt, kDev_Acquire, reinterpret_cast<void*>(&Hook_Acquire), &acquire) &&
            PatchSlot(vt, kDev_SetCoopLevel, reinterpret_cast<void*>(&Hook_SetCoopLevel), &coop) &&
            PatchSlot(vt, kDev_GetDeviceState, reinterpret_cast<void*>(&Hook_GetDeviceState), &state)) {
            g_origAcquire = reinterpret_cast<Acquire_t>(acquire);
            g_origSetCoopLevel = reinterpret_cast<SetCoopLevel_t>(coop);
            g_origGetDeviceState = reinterpret_cast<GetDeviceState_t>(state);
            g_log("[coop] mouse: correcao instalada (nao-exclusivo + readquisicao).");
        } else {
            g_log("[coop] mouse: nao foi possivel instalar a correcao.");
        }
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Resolucao / memoria de video
// O jogo descarta da lista de resolucoes os modos que "nao cabem" na memoria
// de video livre. Em placas modernas esse numero de 32 bits estoura e chega
// pequeno, negativo ou sem sentido, e as resolucoes altas somem. Como nao
// sabemos por qual API o executavel consulta a memoria, interceptamos as
// candidatas nas proprias DLLs do sistema (vale para import estatico,
// GetProcAddress ou COM) e devolvemos 1 GB:
//   - DirectDraw: IDirectDraw2/4/7::GetAvailableVidMem
//   - Direct3D 9: IDirect3DDevice9::GetAvailableTextureMem
// O log registra o que cada uma devolveu de verdade e os modos que o D3D9
// enumerou para o jogo.
// ---------------------------------------------------------------------------
constexpr DWORD kReportedVideoMem = 0x40000000; // 1 GB: cabe qualquer modo e nao estoura conta com sinal

constexpr GUID kIID_DD2   = {0xB3A6F3E0, 0x2B43, 0x11CF, {0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56}};
constexpr GUID kIID_DD4   = {0x9C59509A, 0x39BD, 0x11D1, {0x8C, 0x4A, 0x00, 0xC0, 0x4F, 0xD9, 0x30, 0xC5}};
constexpr GUID kIID_DD7   = {0x15E65EC0, 0x3B9C, 0x11D2, {0xB9, 0x2F, 0x00, 0x60, 0x97, 0x97, 0xEA, 0x5B}};
constexpr GUID kCLSID_DD  = {0xD7B70EE0, 0x4340, 0x11CF, {0xB0, 0x63, 0x00, 0x20, 0xAF, 0xC2, 0xCD, 0x35}};
constexpr GUID kCLSID_DD7 = {0x3C305196, 0x50DB, 0x11D3, {0x9C, 0xFE, 0x00, 0xC0, 0x4F, 0xD9, 0x30, 0xC5}};

constexpr int kUnk_QueryInterface      = 0;
constexpr int kDD_GetAvailableVidMem   = 23;
constexpr int kD3D9_EnumAdapterModes   = 7;
constexpr int kD3D9_CreateDevice       = 16;
constexpr int kDev9_GetAvailTextureMem = 4;

bool Same(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

// Uma mesma funcao-gancho atende varias vtables (IDirectDraw2/4/7 tem
// implementacoes distintas); guardamos o original por vtable.
struct SlotHook { void** vtable; void* original; };
struct SlotTable {
    SlotHook items[6];
    int count = 0;
    void* Find(void* object) const {
        for (int i = 0; i < count; ++i)
            if (items[i].vtable == VTable(object)) return items[i].original;
        return nullptr;
    }
    bool Install(void* object, int slot, void* replacement) {
        if (Find(object)) return true;
        if (count >= 6) return false;
        void* orig = nullptr;
        if (!PatchSlot(VTable(object), slot, replacement, &orig)) return false;
        items[count++] = {VTable(object), orig};
        return true;
    }
};
SlotTable g_vidMemHooks, g_queryHooks;
int g_videoLogs = 10;

void VideoLog(const char* fmt, unsigned long a = 0, unsigned long b = 0, unsigned long c = 0) {
    if (g_videoLogs <= 0) return;
    --g_videoLogs;
    g_log(fmt, a, b, c);
}

void OnDirectDrawObject(void* object, const GUID& iid);

HRESULT WINAPI Hook_GetAvailableVidMem(void* self, void* caps, DWORD* total, DWORD* free) {
    using Fn = HRESULT(WINAPI*)(void*, void*, DWORD*, DWORD*);
    auto orig = reinterpret_cast<Fn>(g_vidMemHooks.Find(self));
    HRESULT hr = orig ? orig(self, caps, total, free) : S_OK;
    VideoLog("[coop] resolucao: DirectDraw informou memoria total=%lu livre=%lu (resultado 0x%08lX); repassando 1 GB.",
             total ? *total : 0, free ? *free : 0, static_cast<unsigned long>(hr));
    if (total) *total = kReportedVideoMem;
    if (free) *free = kReportedVideoMem;
    return S_OK;
}

HRESULT WINAPI Hook_DDQueryInterface(void* self, const GUID& iid, void** out) {
    using Fn = HRESULT(WINAPI*)(void*, const GUID&, void**);
    auto orig = reinterpret_cast<Fn>(g_queryHooks.Find(self));
    if (!orig) return E_NOINTERFACE;
    HRESULT hr = orig(self, iid, out);
    if (SUCCEEDED(hr) && out && *out) OnDirectDrawObject(*out, iid);
    return hr;
}

void OnDirectDrawObject(void* object, const GUID& iid) {
    if (Same(iid, kIID_DD2) || Same(iid, kIID_DD4) || Same(iid, kIID_DD7)) {
        if (g_vidMemHooks.Install(object, kDD_GetAvailableVidMem, reinterpret_cast<void*>(&Hook_GetAvailableVidMem)))
            VideoLog("[coop] resolucao: consulta de memoria do DirectDraw interceptada.");
    } else {
        // IDirectDraw (v1) nao tem a consulta: o jogo pedira uma versao nova por QueryInterface.
        g_queryHooks.Install(object, kUnk_QueryInterface, reinterpret_cast<void*>(&Hook_DDQueryInterface));
    }
}

using DirectDrawCreate_t   = HRESULT(WINAPI*)(GUID*, void**, IUnknown*);
using DirectDrawCreateEx_t = HRESULT(WINAPI*)(GUID*, void**, const GUID&, IUnknown*);
using CoCreateInstance_t   = HRESULT(WINAPI*)(const GUID&, IUnknown*, DWORD, const GUID&, void**);
DirectDrawCreate_t   g_origDirectDrawCreate = nullptr;
DirectDrawCreateEx_t g_origDirectDrawCreateEx = nullptr;
CoCreateInstance_t   g_origCoCreateInstance = nullptr;

HRESULT WINAPI Hook_DirectDrawCreate(GUID* guid, void** dd, IUnknown* outer) {
    HRESULT hr = g_origDirectDrawCreate(guid, dd, outer);
    VideoLog("[coop] resolucao: o jogo chamou DirectDrawCreate (resultado 0x%08lX).", static_cast<unsigned long>(hr));
    if (SUCCEEDED(hr) && dd && *dd) OnDirectDrawObject(*dd, GUID{});
    return hr;
}

HRESULT WINAPI Hook_DirectDrawCreateEx(GUID* guid, void** dd, const GUID& iid, IUnknown* outer) {
    HRESULT hr = g_origDirectDrawCreateEx(guid, dd, iid, outer);
    VideoLog("[coop] resolucao: o jogo chamou DirectDrawCreateEx (resultado 0x%08lX).", static_cast<unsigned long>(hr));
    if (SUCCEEDED(hr) && dd && *dd) OnDirectDrawObject(*dd, iid);
    return hr;
}

HRESULT WINAPI Hook_CoCreateInstance(const GUID& clsid, IUnknown* outer, DWORD ctx, const GUID& iid, void** out) {
    HRESULT hr = g_origCoCreateInstance(clsid, outer, ctx, iid, out);
    if (Same(clsid, kCLSID_DD) || Same(clsid, kCLSID_DD7)) {
        VideoLog("[coop] resolucao: o jogo criou DirectDraw por COM (resultado 0x%08lX).", static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr) && out && *out) OnDirectDrawObject(*out, iid);
    }
    return hr;
}

// ---- Direct3D 9
struct D3DDisplayMode { UINT width, height, refresh, format; };

using Direct3DCreate9_t = void*(WINAPI*)(UINT);
using EnumAdapterModes_t = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, D3DDisplayMode*);
using CreateDevice9_t = HRESULT(WINAPI*)(void*, UINT, UINT, HWND, DWORD, UINT* presentParams, void** device);
using GetAvailTextureMem_t = UINT(WINAPI*)(void*);
Direct3DCreate9_t    g_origDirect3DCreate9 = nullptr;
EnumAdapterModes_t   g_origEnumAdapterModes = nullptr;
CreateDevice9_t      g_origCreateDevice9 = nullptr;
GetAvailTextureMem_t g_origGetAvailTextureMem = nullptr;
UINT g_modesSeen = 0, g_maxModeW = 0, g_maxModeH = 0;
bool g_saw1080 = false;

UINT WINAPI Hook_GetAvailTextureMem(void* self) {
    UINT real = g_origGetAvailTextureMem(self);
    VideoLog("[coop] resolucao: Direct3D informou %lu bytes de memoria de textura.", real);
    return real > kReportedVideoMem ? kReportedVideoMem : real; // acima de 2 GB vira negativo em conta com sinal
}

HRESULT WINAPI Hook_EnumAdapterModes(void* self, UINT adapter, UINT format, UINT index, D3DDisplayMode* mode) {
    HRESULT hr = g_origEnumAdapterModes(self, adapter, format, index, mode);
    if (SUCCEEDED(hr) && mode) {
        ++g_modesSeen;
        if (mode->width == 1920 && mode->height == 1080) g_saw1080 = true;
        if (mode->width * mode->height > g_maxModeW * g_maxModeH) { g_maxModeW = mode->width; g_maxModeH = mode->height; }
    }
    return hr;
}

HRESULT WINAPI Hook_CreateDevice9(void* self, UINT adapter, UINT type, HWND window, DWORD flags, UINT* pp, void** device) {
    g_log("[coop] resolucao: Direct3D enumerou %lu modos para o jogo; 1920x1080=%s; maior %lux%lu.",
          static_cast<unsigned long>(g_modesSeen), g_saw1080 ? "sim" : "nao",
          static_cast<unsigned long>(g_maxModeW), static_cast<unsigned long>(g_maxModeH));
    if (pp) g_log("[coop] resolucao: o jogo esta criando o video em %lux%lu.", static_cast<unsigned long>(pp[0]),
                  static_cast<unsigned long>(pp[1]));
    HRESULT hr = g_origCreateDevice9(self, adapter, type, window, flags, pp, device);
    if (SUCCEEDED(hr) && device && *device && !g_origGetAvailTextureMem) {
        void* orig = nullptr;
        if (PatchSlot(VTable(*device), kDev9_GetAvailTextureMem, reinterpret_cast<void*>(&Hook_GetAvailTextureMem), &orig))
            g_origGetAvailTextureMem = reinterpret_cast<GetAvailTextureMem_t>(orig);
    }
    return hr;
}

void* WINAPI Hook_Direct3DCreate9(UINT sdk) {
    void* d3d = g_origDirect3DCreate9(sdk);
    if (d3d && !g_origCreateDevice9) {
        void *e = nullptr, *c = nullptr;
        if (PatchSlot(VTable(d3d), kD3D9_EnumAdapterModes, reinterpret_cast<void*>(&Hook_EnumAdapterModes), &e))
            g_origEnumAdapterModes = reinterpret_cast<EnumAdapterModes_t>(e);
        if (PatchSlot(VTable(d3d), kD3D9_CreateDevice, reinterpret_cast<void*>(&Hook_CreateDevice9), &c))
            g_origCreateDevice9 = reinterpret_cast<CreateDevice9_t>(c);
    }
    return d3d;
}

// Hook em uma funcao exportada por uma DLL do sistema (carrega a DLL se preciso).
template <typename T>
bool HookExport(const char* dll, const char* function, void* detour, T* original) {
    HMODULE mod = GetModuleHandleA(dll);
    bool preloaded = mod != nullptr;
    if (!mod) mod = LoadLibraryA(dll);
    void* target = mod ? reinterpret_cast<void*>(GetProcAddress(mod, function)) : nullptr;
    bool ok = target && MH_CreateHook(target, detour, reinterpret_cast<LPVOID*>(original)) == MH_OK &&
              MH_EnableHook(target) == MH_OK;
    g_log("[coop] resolucao: %s!%s %s (%s).", dll, function, ok ? "interceptado" : "NAO interceptado",
          preloaded ? "ja carregada pelo jogo" : "carregada por nos");
    return ok;
}

void LogDesktopMode() {
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm))
        g_log("[coop] resolucao: area de trabalho em %lux%lu, %lu bits.", dm.dmPelsWidth, dm.dmPelsHeight,
              dm.dmBitsPerPel);
    bool has1080 = false;
    DWORD maxW = 0, maxH = 0;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
        if (dm.dmPelsWidth == 1920 && dm.dmPelsHeight == 1080) has1080 = true;
        if (dm.dmPelsWidth * dm.dmPelsHeight > maxW * maxH) { maxW = dm.dmPelsWidth; maxH = dm.dmPelsHeight; }
    }
    g_log("[coop] resolucao: Windows oferece 1920x1080=%s, maior modo %lux%lu.", has1080 ? "sim" : "nao", maxW, maxH);
}

} // namespace

void ApplyVideoMemoryFix(void (*log)(const char* fmt, ...)) {
    g_log = log;
    LogDesktopMode();
    MH_Initialize(); // idempotente: o chamador pode inicializar de novo sem problema
    HookExport("ddraw.dll", "DirectDrawCreate", reinterpret_cast<void*>(&Hook_DirectDrawCreate), &g_origDirectDrawCreate);
    HookExport("ddraw.dll", "DirectDrawCreateEx", reinterpret_cast<void*>(&Hook_DirectDrawCreateEx), &g_origDirectDrawCreateEx);
    HookExport("ole32.dll", "CoCreateInstance", reinterpret_cast<void*>(&Hook_CoCreateInstance), &g_origCoCreateInstance);
    HookExport("d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(&Hook_Direct3DCreate9), &g_origDirect3DCreate9);
}

void ApplyDpiAwareness(void (*log)(const char* fmt, ...)) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    auto setAware = user32 ? reinterpret_cast<BOOL(WINAPI*)()>(
                                 reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDPIAware")))
                           : nullptr;
    bool ok = setAware && setAware();
    log("[coop] resolucao: modo DPI-aware %s.", ok ? "ativado" : "NAO ativado");
}

void InstallMouseFix(void* directInput8, void (*log)(const char* fmt, ...)) {
    g_log = log;
    if (!directInput8 || g_origCreateDevice) return;
    void* orig = nullptr;
    if (PatchSlot(VTable(directInput8), kDI8_CreateDevice, reinterpret_cast<void*>(&Hook_CreateDevice), &orig))
        g_origCreateDevice = reinterpret_cast<CreateDevice_t>(orig);
    else
        log("[coop] mouse: nao foi possivel interceptar o DirectInput.");
}

} // namespace coop::win32::fixes
