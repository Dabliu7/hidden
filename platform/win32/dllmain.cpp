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
#include "win32/Menu.h"
#include "win32/ScriptCommand.h"
#include "win32/SystemFixes.h"

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

template <typename T>
void Write(uintptr_t a, T v) { std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(T)); }

// ---------------------------------------------------------------------------
// Jogadores remotos (MVP): um ped comum com a aparencia do CJ por slot, levado
// a cada frame para a posicao interpolada. Ainda sem animacao/armas - o ped
// "desliza". CPlayerPed de verdade + troca de contexto vem depois (SAD 4.2).
// ---------------------------------------------------------------------------
struct RemotePed {
    int32_t handle = -1;
    DWORD   lastFresh = 0;
};
RemotePed g_remotePeds[kMaxPlayers];

void DestroyRemotePed(int slot) {
    RemotePed& rp = g_remotePeds[slot];
    if (rp.handle == -1) return;
    Log("[coop] removendo ped do jogador %d.", slot + 1);
    coop::win32::ScriptCommand(0x009B).Int(rp.handle).Run(); // DELETE_CHAR
    rp.handle = -1;
}

void UpdateRemotePeds(DWORD now) {
    auto& rt = Runtime::Get();
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        RemotePed& rp = g_remotePeds[slot];
        if (!rt.Active() || slot == rt.LocalSlot()) { DestroyRemotePed(slot); continue; }

        auto r = rt.SampleRemotePlayer(static_cast<uint8_t>(slot));
        using Mode = SnapshotBuffer::Mode;
        if (r.mode == Mode::Interpolated || r.mode == Mode::Extrapolated) rp.lastFresh = now;
        if (r.mode == Mode::Empty || now - rp.lastFresh > 5000) { DestroyRemotePed(slot); continue; }

        const Vec3& p = r.state.pos;
        if (rp.handle == -1) {
            Log("[coop] criando ped do jogador %d em (%.1f, %.1f, %.1f).", slot + 1, p.x, p.y, p.z);
            // CREATE_CHAR pedtype=4 (civil) modelo=0 (CJ, sempre carregado) x y z -> local 0
            rp.handle = coop::win32::ScriptCommand(0x009A).Int(4).Int(0).Float(p.x).Float(p.y).Float(p.z).Out(0).Run().Result(0);
            Log("[coop] ped criado, handle=%d.", rp.handle);
            if (rp.handle <= 0) { rp.handle = -1; continue; }
        }

        auto ped = reinterpret_cast<uintptr_t(__cdecl*)(int)>(addr::CPools_GetPed)(rp.handle);
        if (!ped) { rp.handle = -1; continue; } // o jogo removeu o ped; recria no proximo frame
        auto mat = Read<uintptr_t>(ped + addr::CPlaceable_m_matrix);
        if (!mat) continue;

        Write(mat + addr::CMatrix_pos, p.x);
        Write(mat + addr::CMatrix_pos + 4, p.y);
        Write(mat + addr::CMatrix_pos + 8, p.z);
        constexpr float kStepHz = 50.0f; // m/s -> metros por passo de 1/50 s
        Write(ped + addr::CPhysical_m_vecMove, r.state.vel.x / kStepHz);
        Write(ped + addr::CPhysical_m_vecMove + 4, r.state.vel.y / kStepHz);
        Write(ped + addr::CPhysical_m_vecMove + 8, 0.0f); // sem gravidade acumulada entre correcoes
        float heading = 2.0f * std::atan2(r.state.rot.z, r.state.rot.w);
        Write(ped + addr::CPed_m_fCurrentRot, heading);
        Write(ped + addr::CPed_m_fAimingRot, heading);
    }
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

void ShowHelp(const char* text) {
    Log("[coop] exibindo mensagem na tela: %s", text);
    reinterpret_cast<void(__cdecl*)(const char*, bool, bool, bool)>(addr::CHud_SetHelpMessage)(
        text, false, false, false);
    Log("[coop] mensagem exibida.");
}

void StartHosting() {
    bool ok = Runtime::Get().StartHost();
    Log("[coop] host na porta %u: %s", static_cast<unsigned>(kDefaultPort), ok ? "ok" : "FALHOU");
    ShowHelp(ok ? "Coop: sala aberta na porta 7777. Aguardando jogadores."
                : "Coop: nao foi possivel abrir a porta 7777.");
}

// Sessao por linha de comando (-coop-host | -coop-connect <ip>); o menu nativo
// cobre apenas "hospedar" por enquanto.
void StartSessionFromCommandLine() {
    std::string cmd = GetCommandLineA();
    auto& rt = Runtime::Get();
    if (cmd.find("-coop-host") != std::string::npos) {
        StartHosting();
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
        Runtime::Get().SetStatusSink([](const char* msg) { ShowHelp(msg); });
        StartSessionFromCommandLine();
    }

    auto& rt = Runtime::Get();
    // Entrou no jogo pela entrada "Cooperativo" do menu principal.
    if (!rt.Active() && coop::win32::menu::HostRequested()) StartHosting();
    DWORD now = GetTickCount();
    rt.Frame(now);

    g_origGameProcess();
    UpdateRemotePeds(now);

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
    coop::win32::menu::Install(&Log);
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
    HRESULT hr = g_realDirectInput8Create(inst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out) coop::win32::fixes::InstallMouseFix(*out, &Log);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Sob loader lock: apenas patch de memoria. Nada de LoadLibrary/sockets aqui.
        // As correcoes de sistema valem para qualquer versao do executavel.
        coop::win32::fixes::ApplyDpiAwareness(&Log);
        InstallHooks();
    }
    return TRUE;
}
