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
// ---------------------------------------------------------------------------
constexpr GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr HRESULT kInputLost   = static_cast<HRESULT>(0x8007001EL); // DIERR_INPUTLOST
constexpr HRESULT kNotAcquired = static_cast<HRESULT>(0x8007000CL); // DIERR_NOTACQUIRED

// Indices nas vtables COM (dinput.h)
constexpr int kDI8_CreateDevice    = 3;
constexpr int kDev_Acquire         = 7;
constexpr int kDev_GetDeviceState  = 9;

using CreateDevice_t   = HRESULT(WINAPI*)(void* self, const GUID& guid, void** device, void* outer);
using GetDeviceState_t = HRESULT(WINAPI*)(void* self, DWORD size, void* data);
using Acquire_t        = HRESULT(WINAPI*)(void* self);

CreateDevice_t   g_origCreateDevice = nullptr;
GetDeviceState_t g_origGetDeviceState = nullptr;
void*            g_mouse = nullptr;
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

HRESULT WINAPI Hook_GetDeviceState(void* self, DWORD size, void* data) {
    HRESULT hr = g_origGetDeviceState(self, size, data);
    if (self == g_mouse && (hr == kInputLost || hr == kNotAcquired)) {
        auto acquire = reinterpret_cast<Acquire_t>(VTable(self)[kDev_Acquire]);
        if (SUCCEEDED(acquire(self))) {
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
        void* orig = nullptr;
        if (PatchSlot(VTable(g_mouse), kDev_GetDeviceState, reinterpret_cast<void*>(&Hook_GetDeviceState), &orig)) {
            g_origGetDeviceState = reinterpret_cast<GetDeviceState_t>(orig);
            g_log("[coop] mouse: correcao de readquisicao instalada.");
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
