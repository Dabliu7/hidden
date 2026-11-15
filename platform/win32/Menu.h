#pragma once

namespace coop::win32::menu {

// Insere "Cooperativo (1-4 Jogadores)" no menu principal e instala o hook de
// texto. Chamado uma vez, apos MH_Initialize. Falha de forma segura: se a
// tabela de menus nao tiver o formato esperado, nada e alterado.
void Install(void (*log)(const char* fmt, ...));

// true quando o jogador entrou em "Iniciar Jogo" pela entrada cooperativa.
bool HostRequested();

} // namespace coop::win32::menu
