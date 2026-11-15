#pragma once
// Gancho por tabela de importacao (GOT/PLT) de uma biblioteca ja carregada.
// Troca o ponteiro que a biblioteca usa para chamar uma funcao importada
// (ex.: glClear). Nao reescreve codigo de maquina, entao funciona igual em
// ARMv7 e ARM64 e dispensa bibliotecas de inline hook.
#include <cstddef>

namespace coop::android {

// Substitui, em `libraryNameSuffix` (ex.: "libGTASA.so"), toda entrada de
// importacao de `symbol` por `replacement`. Devolve quantas entradas foram
// trocadas (0 = biblioteca nao importa o simbolo) e, em *original, o ponteiro
// anterior.
int HookImport(const char* libraryNameSuffix, const char* symbol, void* replacement, void** original);

} // namespace coop::android
