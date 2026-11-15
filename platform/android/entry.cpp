// libGTASA_Coop.so
//
// Carregada pelo proprio APK: o patcher (tools/apk-patcher, Fase 1) adiciona
// uma chamada System.loadLibrary("GTASA_Coop") logo apos a de "GTASA" na
// Activity do jogo. Quando JNI_OnLoad roda, libGTASA.so ja esta mapeada e
// relocada, entao os simbolos C++ exportados por ela podem ser resolvidos por
// nome - sem tabela de enderecos por versao como no PC.
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include "Runtime.h"

#define COOP_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GTASA_Coop", __VA_ARGS__)

namespace {

void* ResolveGameSymbol(const char* mangled) {
    static void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    return lib ? dlsym(lib, mangled) : nullptr;
}

} // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    // Mesmo ponto de enganche do PC (CGame::Process), resolvido por simbolo.
    void* gameProcess = ResolveGameSymbol("_ZN5CGame7ProcessEv");
    COOP_LOG("protocolo %u, CGame::Process=%p", static_cast<unsigned>(coop::kProtocolVersion), gameProcess);
    if (!gameProcess) {
        COOP_LOG("libGTASA.so nao encontrada ou incompativel; coop desativado.");
        return JNI_VERSION_1_6;
    }
    // Fase 1: instalar o inline hook (backend ShadowHook/Dobby atras de
    // platform/common/Hook.h) e chamar coop::Runtime::Get().Frame() por frame.
    return JNI_VERSION_1_6;
}
