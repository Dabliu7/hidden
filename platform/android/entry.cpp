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
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "Runtime.h"
#include "android/GotHook.h"

namespace {

using namespace coop;

std::string g_dataDir; // /storage/emulated/0/Android/data/<pacote>/files

std::string g_pendingLog; // linhas emitidas antes de a pasta de dados existir

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, "GTASA_Coop", buf);
    if (g_dataDir.empty()) return;
    // No JNI_OnLoad a pasta do app pode ainda nao existir: acumula e grava depois.
    FILE* f = fopen((g_dataDir + "/coop.log").c_str(), "a");
    if (!f) {
        if (g_pendingLog.size() < 16384) g_pendingLog.append(buf).push_back('\n');
        return;
    }
    if (!g_pendingLog.empty()) {
        fputs(g_pendingLog.c_str(), f);
        g_pendingLog.clear();
    }
    fputs(buf, f);
    fputc('\n', f);
    fclose(f);
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
using FindPlayerSpeed_t   = const CVector* (*)(int);

FindPlayerPed_t     g_findPlayerPed = nullptr;
FindPlayerSpeed_t   g_findPlayerSpeed = nullptr;

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

// Layout de CPlaceable (base de todo CEntity): vtable, transformacao simples
// {CVector pos; float heading}, ponteiro para a matriz completa (pos em +0x30).
constexpr size_t kSimplePos = sizeof(void*);
constexpr size_t kMatrixPtr = sizeof(void*) + 16;
constexpr size_t kMatrixPos = 0x30;

template <typename T>
T ReadAt(const void* base, size_t offset) {
    T v;
    std::memcpy(&v, static_cast<const char*>(base) + offset, sizeof(T));
    return v;
}

void InstallCrashHandler();

bool g_traceSteps = true; // registra cada etapa da primeira leitura, para localizar um crash

bool ReadLocalPlayer(PlayerSnapshot& s) {
    if (!g_findPlayerPed) return false;
    void* ped = g_findPlayerPed(-1);
    if (!ped) return false;
    bool trace = g_traceSteps;
    g_traceSteps = false;
    if (trace) {
        InstallCrashHandler(); // o jogo pode ter registrado o proprio tratador depois do nosso
        Log("[coop] etapa 1: jogador=%p; lendo posicao.", ped);
    }

    auto* matrix = ReadAt<const char*>(ped, kMatrixPtr);
    if (matrix && (reinterpret_cast<uintptr_t>(matrix) & 3) == 0 && reinterpret_cast<uintptr_t>(matrix) > 0x10000) {
        s.pos = {ReadAt<float>(matrix, kMatrixPos), ReadAt<float>(matrix, kMatrixPos + 4),
                 ReadAt<float>(matrix, kMatrixPos + 8)};
    } else {
        s.pos = {ReadAt<float>(ped, kSimplePos), ReadAt<float>(ped, kSimplePos + 4), ReadAt<float>(ped, kSimplePos + 8)};
    }
    if (trace) Log("[coop] etapa 2: posicao (%.1f, %.1f, %.1f), matriz=%p; lendo velocidade.", s.pos.x, s.pos.y,
                   s.pos.z, matrix);

    if (g_findPlayerSpeed) {
        if (const CVector* v = g_findPlayerSpeed(-1)) {
            constexpr float kStepHz = 50.0f; // unidades do jogo (m por passo de 1/50 s) -> m/s
            s.vel = {v->x * kStepHz, v->y * kStepHz, v->z * kStepHz};
        }
    }
    if (trace) Log("[coop] etapa 3: velocidade ok; calculando direcao pela matriz.");
    // Direcao a partir do vetor "frente" da matriz (segunda linha, +0x10).
    // FindPlayerHeading(-1) falha quando chamada fora da thread de jogo.
    if (matrix && (reinterpret_cast<uintptr_t>(matrix) & 3) == 0 && reinterpret_cast<uintptr_t>(matrix) > 0x10000) {
        float fx = ReadAt<float>(matrix, 0x10), fy = ReadAt<float>(matrix, 0x14);
        float h = std::atan2(-fx, fy);
        s.rot = {0, 0, std::sin(h * 0.5f), std::cos(h * 0.5f)};
    }
    if (trace) Log("[coop] etapa 4: leitura do jogador concluida.");
    return true;
}

// ---- Diagnostico de crash: registra o sinal e ONDE ocorreu (modulo + deslocamento).
struct sigaction g_oldActions[32];

void CrashHandler(int sig, siginfo_t* info, void* context) {
    static volatile int once = 0;
    if (!once) {
        once = 1;
        uintptr_t pc = 0, lr = 0;
        auto* uc = static_cast<ucontext_t*>(context);
#if defined(__aarch64__)
        pc = uc->uc_mcontext.pc;
        lr = uc->uc_mcontext.regs[30];
#elif defined(__arm__)
        pc = uc->uc_mcontext.arm_pc;
        lr = uc->uc_mcontext.arm_lr;
#else
        (void)uc;
#endif
        Log("[coop] CRASH sinal=%d endereco-acessado=%p", sig, info ? info->si_addr : nullptr);
        for (int i = 0; i < 2; ++i) {
            uintptr_t addr = i == 0 ? pc : lr;
            Dl_info dl{};
            if (addr && dladdr(reinterpret_cast<void*>(addr), &dl) && dl.dli_fname) {
                const char* name = std::strrchr(dl.dli_fname, '/');
                Log("[coop] CRASH %s: %s+0x%lx (%s)", i == 0 ? "pc" : "lr", name ? name + 1 : dl.dli_fname,
                    static_cast<unsigned long>(addr - reinterpret_cast<uintptr_t>(dl.dli_fbase)),
                    dl.dli_sname ? dl.dli_sname : "?");
            } else {
                Log("[coop] CRASH %s: %p (modulo desconhecido)", i == 0 ? "pc" : "lr", reinterpret_cast<void*>(addr));
            }
        }
    }
    // Repassa ao tratador anterior com as MESMAS informacoes do sinal. O runtime
    // do Android usa SIGSEGV internamente (checagens implicitas) e precisa do
    // endereco original da falha; reemitir com raise() perderia esse dado.
    if (sig < 0 || sig >= 32) return;
    const struct sigaction& old = g_oldActions[sig];
    if (old.sa_flags & SA_SIGINFO) {
        if (old.sa_sigaction) old.sa_sigaction(sig, info, context);
    } else if (old.sa_handler == SIG_DFL) {
        // Sem tratador anterior: restaura o padrao e retorna; a instrucao que
        // falhou e reexecutada e o sistema encerra o processo normalmente.
        signal(sig, SIG_DFL);
    } else if (old.sa_handler != SIG_IGN) {
        old.sa_handler(sig);
    }
}

void InstallCrashHandler() {
    struct sigaction sa{};
    sa.sa_sigaction = &CrashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE}) {
        struct sigaction current{};
        sigaction(sig, nullptr, &current);
        if (current.sa_sigaction == &CrashHandler) continue; // ja e o nosso: nao encadear em si mesmo
        sigaction(sig, &sa, &g_oldActions[sig]);
    }
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
        Log("[coop] etapa 5: iniciando sessao a partir do coop.txt.");
        rt.SetStatusSink([](const char* msg) { Log("[coop] %s", msg); });
        StartSessionFromConfig();
    }

    rt.Frame(now);
    static bool framed = false;
    if (inGame && !framed) {
        framed = true;
        Log("[coop] etapa 6: primeiro quadro em jogo concluido.");
    }

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

    InstallCrashHandler();
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) {
        Log("[coop] libGTASA.so nao esta carregada; coop desativado.");
        return JNI_VERSION_1_6;
    }
    Resolve(lib, "_Z13FindPlayerPedi", &g_findPlayerPed);
    Resolve(lib, "_Z15FindPlayerSpeedi", &g_findPlayerSpeed);

    int clear = coop::android::HookImport("libGTASA.so", "glClear", reinterpret_cast<void*>(&Hook_glClear),
                                          reinterpret_cast<void**>(&g_origGlClear));
    int swap = coop::android::HookImport("libGTASA.so", "eglSwapBuffers", reinterpret_cast<void*>(&Hook_eglSwapBuffers),
                                         reinterpret_cast<void**>(&g_origEglSwap));
    Log("[coop] ganchos de quadro: glClear=%d eglSwapBuffers=%d.", clear, swap);
    if (clear + swap == 0) Log("[coop] nenhum gancho de quadro instalado; coop desativado.");
    return JNI_VERSION_1_6;
}
