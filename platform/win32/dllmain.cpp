// dinput8.dll proxy.
//
// O gta_sa.exe importa DINPUT8.dll!DirectInput8Create. O Windows resolve DLLs
// nao-"KnownDLLs" primeiro na pasta do executavel, entao um dinput8.dll ao lado
// do jogo e carregado no lugar do do sistema - sem CLEO, ASI loader ou injetor.
// Encaminhamos a unica exportacao usada para o dinput8 real e instalamos
// nossos hooks.
#include <windows.h>
#include <unknwn.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include <MinHook.h>

#include "Runtime.h"
#include "win32/Addresses.h"

namespace {

using namespace coop;
namespace addr = coop::win32::addr;

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8Create_t g_realDirectInput8Create = nullptr;

using GameProcess_t = void(__cdecl*)();
GameProcess_t g_origGameProcess = nullptr;

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    if (FILE* f = fopen("coop.log", "a")) {
        fputs(buf, f);
        fputc('\n', f);
        fclose(f);
    }
}

bool IsSupportedExe() {
    MEMORY_BASIC_INFORMATION mbi{};
    auto* probe = reinterpret_cast<const uint32_t*>(addr::kVersionProbe10US);
    if (!VirtualQuery(probe, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    return *probe == addr::kVersionProbeValue;
}

template <typename T>
T Read(uintptr_t a) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(T));
    return v;
}

// Le o CPlayerPed local e converte para o formato neutro de rede.
bool ReadLocalPlayer(PlayerSnapshot& s) {
    auto ped = Read<uintptr_t>(addr::CWorld_Players);
    if (!ped) return false;
    auto mat = Read<uintptr_t>(ped + addr::CPlaceable_m_matrix);
    if (!mat) return false;

    s.pos = {Read<float>(mat + addr::CMatrix_pos), Read<float>(mat + addr::CMatrix_pos + 4),
             Read<float>(mat + addr::CMatrix_pos + 8)};
    // unidades do jogo (m por passo de 1/50 s) -> m/s
    constexpr float kStepHz = 50.0f;
    s.vel = {Read<float>(ped + addr::CPhysical_m_vecMove) * kStepHz,
             Read<float>(ped + addr::CPhysical_m_vecMove + 4) * kStepHz,
             Read<float>(ped + addr::CPhysical_m_vecMove + 8) * kStepHz};
    float heading = Read<float>(ped + addr::CPed_m_fCurrentRot);
    s.rot = {0, 0, std::sin(heading * 0.5f), std::cos(heading * 0.5f)};
    s.health = static_cast<uint8_t>(std::fmin(255.0f, std::fmax(0.0f, Read<float>(ped + addr::CPed_m_fHealth))));
    s.armour = static_cast<uint8_t>(std::fmin(255.0f, std::fmax(0.0f, Read<float>(ped + addr::CPed_m_fArmour))));
    return true;
}

// Fase 1: sessao iniciada por linha de comando (-coop-host | -coop-connect <ip>).
// A partir da Fase 2 isso passa a ser acionado pelo menu nativo.
void StartSessionFromCommandLine() {
    std::string cmd = GetCommandLineA();
    auto& rt = Runtime::Get();
    if (cmd.find("-coop-host") != std::string::npos) {
        Log("[coop] host: %s", rt.StartHost() ? "ok" : "FALHOU");
    } else if (auto p = cmd.find("-coop-connect "); p != std::string::npos) {
        std::string ip = cmd.substr(p + 14);
        ip = ip.substr(0, ip.find(' '));
        Log("[coop] conectando a %s: %s", ip.c_str(), rt.StartClient(ip) ? "ok" : "FALHOU");
    }
}

void __cdecl Hook_GameProcess() {
    static bool started = false;
    if (!started) {
        // Fora do loader lock: aqui ja e seguro abrir sockets.
        started = true;
        StartSessionFromCommandLine();
    }

    auto& rt = Runtime::Get();
    DWORD now = GetTickCount();
    rt.Frame(now);

    g_origGameProcess();

    static DWORD lastSend = 0;
    if (rt.Active() && now - lastSend >= 50) { // 20 Hz
        lastSend = now;
        PlayerSnapshot s;
        s.slot = rt.LocalSlot();
        s.timeMs = now;
        if (ReadLocalPlayer(s)) rt.SubmitLocalPlayer(s);
    }
}

void InstallHooks() {
    if (!IsSupportedExe()) {
        Log("[coop] executavel nao suportado (requer gta_sa.exe 1.0 US); coop desativado.");
        return;
    }
    if (MH_Initialize() != MH_OK) return;
    if (MH_CreateHook(reinterpret_cast<LPVOID>(addr::CGame_Process),
                      reinterpret_cast<LPVOID>(&Hook_GameProcess),
                      reinterpret_cast<LPVOID*>(&g_origGameProcess)) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("[coop] falha ao instalar hook em CGame::Process.");
        return;
    }
    Log("[coop] hooks instalados (protocolo %u).", static_cast<unsigned>(kProtocolVersion));
}

} // namespace

extern "C" HRESULT WINAPI Coop_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid,
                                                  LPVOID* out, LPUNKNOWN outer) {
    if (!g_realDirectInput8Create) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strncat(path, "\\dinput8.dll", MAX_PATH - strlen(path) - 1);
        if (HMODULE real = LoadLibraryA(path))
            g_realDirectInput8Create = reinterpret_cast<DirectInput8Create_t>(
                reinterpret_cast<void*>(GetProcAddress(real, "DirectInput8Create")));
        if (!g_realDirectInput8Create) return E_FAIL;
    }
    return g_realDirectInput8Create(inst, version, riid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Sob loader lock: apenas patch de memoria. Nada de LoadLibrary/sockets aqui.
        InstallHooks();
    }
    return TRUE;
}
