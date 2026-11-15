#pragma once

namespace coop::win32::fixes {

// Resolucao: declara o processo como DPI-aware. Deve rodar antes de o jogo
// criar a janela (chamado no DllMain).
void ApplyDpiAwareness(void (*log)(const char* fmt, ...));

// Resolucao: faz o jogo enxergar memoria de video suficiente para listar os
// modos altos (a consulta por DirectDraw estoura em placas modernas).
void ApplyVideoMemoryFix(void (*log)(const char* fmt, ...));

// Mouse: readquire automaticamente o dispositivo quando o DirectInput perde o
// foco. `directInput8` e o objeto devolvido por DirectInput8Create.
void InstallMouseFix(void* directInput8, void (*log)(const char* fmt, ...));

// Informa onde o jogo guarda o indicador "menu aberto" (1 byte, != 0 em menu),
// para a correcao de mouse saber quando prender o cursor.
void SetMenuActiveFlag(const unsigned char* flag);

} // namespace coop::win32::fixes
