// Correcoes de compatibilidade com Windows moderno. Nenhuma depende de
// enderecos do gta_sa.exe: atuam sobre APIs do sistema (user32 / DirectInput).
#include "win32/SystemFixes.h"

#include <windows.h>

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

HRESULT WINAPI Hook_GetDeviceState(void* self, DWORD size, void* data) {
    HRESULT hr = g_origGetDeviceState(self, size, data);
    if (self == g_mouse && FAILED(hr)) {
        if (SUCCEEDED(g_origAcquire(self))) {
            hr = g_origGetDeviceState(self, size, data);
            if (!g_loggedRecovery) {
                g_loggedRecovery = true;
                g_log("[coop] mouse: dispositivo readquirido apos perda de foco.");
            }
        }
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

} // namespace

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
