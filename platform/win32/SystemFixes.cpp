// Correcoes de compatibilidade com Windows moderno. Nenhuma depende de
// enderecos do gta_sa.exe: atuam sobre APIs do sistema (user32 / DirectInput).
#include "win32/SystemFixes.h"

#include <windows.h>
#include <unknwn.h>

#include <cstdint>

#include <cstring>

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
int              g_logBudget = 12; // diagnostico limitado: nao encher o log
bool             g_loggedRecovery = false;

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
    st->x = pos.x - center.x;
    st->y = pos.y - center.y;
    st->wheel = 0;
    st->buttons[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 0x80 : 0;
    st->buttons[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 0x80 : 0;
    st->buttons[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 0x80 : 0;
    st->buttons[3] = 0;
    SetCursorPos(center.x, center.y); // proxima leitura mede o deslocamento a partir do centro
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
// de video livre, consultada por DirectDraw (GetAvailableVidMem). Em placas
// modernas esse numero de 32 bits estoura e chega pequeno ou sem sentido, e
// as resolucoes altas somem. Interceptamos a consulta e devolvemos 1 GB.
// ---------------------------------------------------------------------------
constexpr int   kDD7_GetAvailableVidMem = 23;
constexpr DWORD kReportedVideoMem = 0x40000000; // 1 GB: cabe qualquer modo e nao estoura conta com sinal

using DirectDrawCreateEx_t = HRESULT(WINAPI*)(GUID* guid, void** dd, const IID& iid, IUnknown* outer);
using GetAvailableVidMem_t = HRESULT(WINAPI*)(void* self, void* caps, DWORD* total, DWORD* free);

DirectDrawCreateEx_t g_origDirectDrawCreateEx = nullptr;
GetAvailableVidMem_t g_origGetAvailableVidMem = nullptr;
int                  g_vidMemLogs = 4;

HRESULT WINAPI Hook_GetAvailableVidMem(void* self, void* caps, DWORD* total, DWORD* free) {
    HRESULT hr = g_origGetAvailableVidMem(self, caps, total, free);
    DWORD t = total ? *total : 0, f = free ? *free : 0;
    if (total) *total = kReportedVideoMem;
    if (free) *free = kReportedVideoMem;
    if (g_vidMemLogs > 0) {
        --g_vidMemLogs;
        g_log("[coop] resolucao: memoria de video informada pelo sistema total=%lu livre=%lu (resultado 0x%08X); "
              "repassando 1 GB ao jogo.", t, f, hr);
    }
    return S_OK;
}

HRESULT WINAPI Hook_DirectDrawCreateEx(GUID* guid, void** dd, const IID& iid, IUnknown* outer) {
    HRESULT hr = g_origDirectDrawCreateEx(guid, dd, iid, outer);
    if (SUCCEEDED(hr) && dd && *dd && !g_origGetAvailableVidMem) {
        void* orig = nullptr;
        if (PatchSlot(VTable(*dd), kDD7_GetAvailableVidMem, reinterpret_cast<void*>(&Hook_GetAvailableVidMem), &orig)) {
            g_origGetAvailableVidMem = reinterpret_cast<GetAvailableVidMem_t>(orig);
            g_log("[coop] resolucao: consulta de memoria de video interceptada.");
        }
    }
    return hr;
}

// Troca uma entrada da tabela de importacao do executavel principal.
bool PatchImport(const char* dll, const char* function, void* replacement, void** original) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        if (!imp->OriginalFirstThunk) return false;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            *original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
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
    void* orig = nullptr;
    if (PatchImport("ddraw.dll", "DirectDrawCreateEx", reinterpret_cast<void*>(&Hook_DirectDrawCreateEx), &orig)) {
        g_origDirectDrawCreateEx = reinterpret_cast<DirectDrawCreateEx_t>(orig);
        log("[coop] resolucao: DirectDrawCreateEx interceptado.");
    } else {
        log("[coop] resolucao: o executavel nao importa ddraw.dll!DirectDrawCreateEx; correcao de memoria de video inativa.");
    }
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
