// libGTASA_Coop.so
//
// Carregada pelo proprio APK: o patcher (tools/apk-patcher) adiciona uma
// chamada System.loadLibrary("GTASA_Coop") logo apos a de "GTASA" na Activity
// do jogo. Quando JNI_OnLoad roda, libGTASA.so ja esta mapeada e relocada.
//
// Etapa atual (equivalente ao primeiro marco do PC):
//   - gancho por frame via tabela de importacao da libGTASA.so (sem inline hook);
//   - sessao iniciada por arquivo de configuracao (coop.txt);
//   - posicao do jogador local publicada na rede (funcoes exportadas pelo jogo,
//     resolvidas por nome - nenhum offset de struct);
//   - diagnostico em coop.log e no logcat.
// Ainda NAO ha: item no menu, personagem remoto desenhado no mundo.
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "Runtime.h"
#include "android/GotHook.h"

namespace {

using namespace coop;

std::string g_dataDir; // /storage/emulated/0/Android/data/<pacote>/files

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, "GTASA_Coop", buf);
    if (g_dataDir.empty()) return;
    if (FILE* f = fopen((g_dataDir + "/coop.log").c_str(), "a")) {
        fputs(buf, f);
        fputc('\n', f);
        fclose(f);
    }
}

// O diretorio externo do app e gravavel sem permissao e acessivel pelo usuario.
void InitDataDir() {
    char pkg[256] = {};
    if (FILE* f = fopen("/proc/self/cmdline", "r")) {
        fread(pkg, 1, sizeof(pkg) - 1, f);
        fclose(f);
    }
    if (char* colon = std::strchr(pkg, ':')) *colon = 0; // "pacote:processo"
    if (pkg[0]) g_dataDir = std::string("/storage/emulated/0/Android/data/") + pkg + "/files";
}

uint32_t NowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000);
}

// ---- Funcoes exportadas pela libGTASA.so (nomes C++ "mangled")
struct CVector { float x, y, z; };
using FindPlayerPed_t     = void* (*)(int);
using FindPlayerCoors_t   = CVector (*)(int);
using FindPlayerSpeed_t   = const CVector* (*)(int);
using FindPlayerHeading_t = float (*)(int);

FindPlayerPed_t     g_findPlayerPed = nullptr;
FindPlayerCoors_t   g_findPlayerCoors = nullptr;
FindPlayerSpeed_t   g_findPlayerSpeed = nullptr;
FindPlayerHeading_t g_findPlayerHeading = nullptr;

template <typename T>
void Resolve(void* lib, const char* mangled, T* out) {
    *out = reinterpret_cast<T>(dlsym(lib, mangled));
    Log("[coop] simbolo %s: %s", mangled, *out ? "ok" : "NAO ENCONTRADO");
}

// coop.txt: uma linha, "host" ou "connect <ip>".
void StartSessionFromConfig() {
    std::string path = g_dataDir + "/coop.txt";
    char line[128] = {};
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        Log("[coop] sem %s; jogo normal. Crie o arquivo com 'host' ou 'connect <ip>'.", path.c_str());
        return;
    }
    fgets(line, sizeof(line), f);
    fclose(f);
    line[std::strcspn(line, "\r\n")] = 0;

    auto& rt = Runtime::Get();
    if (std::strncmp(line, "host", 4) == 0) {
        Log("[coop] host na porta %u: %s", static_cast<unsigned>(kDefaultPort), rt.StartHost() ? "ok" : "FALHOU");
    } else if (std::strncmp(line, "connect ", 8) == 0) {
        Log("[coop] conectando a %s: %s", line + 8, rt.StartClient(line + 8) ? "ok" : "FALHOU");
    } else {
        Log("[coop] coop.txt invalido: '%s' (use 'host' ou 'connect <ip>').", line);
    }
}

bool ReadLocalPlayer(PlayerSnapshot& s) {
    if (!g_findPlayerPed || !g_findPlayerCoors || !g_findPlayerPed(-1)) return false;
    CVector p = g_findPlayerCoors(-1);
    s.pos = {p.x, p.y, p.z};
    if (g_findPlayerSpeed) {
        if (const CVector* v = g_findPlayerSpeed(-1)) {
            constexpr float kStepHz = 50.0f; // unidades do jogo (m por passo de 1/50 s) -> m/s
            s.vel = {v->x * kStepHz, v->y * kStepHz, v->z * kStepHz};
        }
    }
    if (g_findPlayerHeading) {
        float h = g_findPlayerHeading(-1);
        s.rot = {0, 0, std::sin(h * 0.5f), std::cos(h * 0.5f)};
    }
    return true;
}

// Chamado a partir das funcoes de video que o jogo executa a cada quadro.
void Tick() {
    static pid_t gameThread = 0;
    static uint32_t lastTick = 0, lastSend = 0, lastReport = 0, ticks = 0;
    static bool started = false, sawPlayer = false;

    pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
    if (!gameThread) gameThread = tid;
    if (tid != gameThread) return; // o nucleo roda em uma unica thread

    uint32_t now = NowMs();
    if (now - lastTick < 10) return; // as funcoes de video sao chamadas varias vezes por quadro
    lastTick = now;
    ++ticks;

    auto& rt = Runtime::Get();
    PlayerSnapshot s;
    bool inGame = ReadLocalPlayer(s);
    if (inGame && !sawPlayer) {
        sawPlayer = true;
        Log("[coop] jogador local encontrado em (%.1f, %.1f, %.1f).", s.pos.x, s.pos.y, s.pos.z);
    }
    if (inGame && !started) {
        started = true;
        rt.SetStatusSink([](const char* msg) { Log("[coop] %s", msg); });
        StartSessionFromConfig();
    }

    rt.Frame(now);

    if (inGame && rt.Active() && now - lastSend >= 50) { // 20 Hz
        lastSend = now;
        s.slot = rt.LocalSlot();
        s.timeMs = now;
        rt.SubmitLocalPlayer(s);
    }

    static int reports = 12;
    if (reports > 0 && now - lastReport > 10000) {
        lastReport = now;
        --reports;
        Log("[coop] ativo: ticks=%u em-jogo=%d sessao=%d.", ticks, inGame ? 1 : 0, rt.Active() ? 1 : 0);
        for (uint8_t slot = 0; slot < kMaxPlayers; ++slot) {
            auto r = rt.SampleRemotePlayer(slot);
            if (r.mode != SnapshotBuffer::Mode::Empty)
                Log("[coop] jogador %d remoto em (%.1f, %.1f, %.1f).", slot + 1, r.state.pos.x, r.state.pos.y,
                    r.state.pos.z);
        }
    }
}

// ---- Ganchos de importacao: so servem para nos dar um "tique" por quadro.
using GlClear_t = void (*)(unsigned);
using EglSwap_t = unsigned (*)(void*, void*);
GlClear_t g_origGlClear = nullptr;
EglSwap_t g_origEglSwap = nullptr;

void Hook_glClear(unsigned mask) {
    g_origGlClear(mask);
    Tick();
}

unsigned Hook_eglSwapBuffers(void* display, void* surface) {
    Tick();
    return g_origEglSwap(display, surface);
}

} // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    InitDataDir();
    Log("[coop] libGTASA_Coop carregada (protocolo %u, %s).", static_cast<unsigned>(kProtocolVersion),
        sizeof(void*) == 8 ? "arm64" : "arm32");

    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) {
        Log("[coop] libGTASA.so nao esta carregada; coop desativado.");
        return JNI_VERSION_1_6;
    }
    Resolve(lib, "_Z13FindPlayerPedi", &g_findPlayerPed);
    Resolve(lib, "_Z15FindPlayerCoorsi", &g_findPlayerCoors);
    Resolve(lib, "_Z15FindPlayerSpeedi", &g_findPlayerSpeed);
    Resolve(lib, "_Z17FindPlayerHeadingi", &g_findPlayerHeading);

    int clear = coop::android::HookImport("libGTASA.so", "glClear", reinterpret_cast<void*>(&Hook_glClear),
                                          reinterpret_cast<void**>(&g_origGlClear));
    int swap = coop::android::HookImport("libGTASA.so", "eglSwapBuffers", reinterpret_cast<void*>(&Hook_eglSwapBuffers),
                                         reinterpret_cast<void**>(&g_origEglSwap));
    Log("[coop] ganchos de quadro: glClear=%d eglSwapBuffers=%d.", clear, swap);
    if (clear + swap == 0) Log("[coop] nenhum gancho de quadro instalado; coop desativado.");
    return JNI_VERSION_1_6;
}
