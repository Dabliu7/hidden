// Integracao com o front-end nativo (CMenuManager) - etapa 1 do milestone M2.
//
// O menu do PC e dirigido por dados: CMenuManager::aScreens. Aqui:
//   1. localizamos a pagina do menu principal PELO CONTEUDO (entradas FEP_STG e
//      FEP_QUI, sem FEP_RES), nao por indice fixo - se o layout nao bater, nada
//      e alterado e a tabela e despejada no coop.log para diagnostico;
//   2. inserimos a entrada COOP_MN logo apos "Iniciar Jogo", com a mesma acao
//      (abre a pagina nativa Novo Jogo / Carregar);
//   3. servimos o texto de COOP_MN por um hook em CText::Get;
//   4. observamos (somente leitura) pagina e linha selecionada para saber se o
//      jogador entrou por essa entrada => sessao cooperativa como host.
#include "win32/Menu.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include <MinHook.h>

#include "win32/Addresses.h"

namespace coop::win32::menu {

namespace {

#pragma pack(push, 1)
struct MenuEntry {
    uint8_t action;
    char    name[8];
    uint8_t type;
    uint8_t target;
    uint8_t pad0;
    int16_t x, y;
    uint8_t align;
    uint8_t pad1;
};
struct MenuScreen {
    char      title[8];
    uint8_t   prevPage;
    uint8_t   parentEntry;
    MenuEntry entries[12];
};
#pragma pack(pop)
static_assert(sizeof(MenuEntry) == 18, "layout de MenuEntry");
static_assert(sizeof(MenuScreen) == 0xE2, "layout de MenuScreen");

constexpr int  kScreenCount = 44;
constexpr char kCoopKey[8]  = "COOP_MN";
const char     kCoopLabel[] = "Cooperativo (1-4 Jogadores)";

void (*g_log)(const char*, ...) = nullptr;

using TextGet_t = const char*(__fastcall*)(void* self, void* edx, const char* key);
TextGet_t g_origTextGet = nullptr;

int  g_mainPage = -1;   // indice da pagina do menu principal
int  g_startPage = -1;  // pagina aberta por "Iniciar Jogo"
int  g_coopRow = -1;    // linha da nossa entrada no menu principal
int  g_lastPage = -1;
int  g_lastRowOnMain = -1;
bool g_hostRequested = false;

MenuScreen* Screens() { return reinterpret_cast<MenuScreen*>(addr::CMenuManager_aScreens); }

bool KeyIs(const char* name, const char* key) { return std::strncmp(name, key, 8) == 0; }

int FindEntry(const MenuScreen& s, const char* key) {
    for (int i = 0; i < 12; ++i)
        if (KeyIs(s.entries[i].name, key)) return i;
    return -1;
}

void DumpScreens() {
    for (int p = 0; p < kScreenCount; ++p) {
        const MenuScreen& s = Screens()[p];
        char line[160];
        int n = wsprintfA(line, "[coop] menu[%d] '%.8s':", p, s.title);
        for (int i = 0; i < 12 && n < 140; ++i)
            if (s.entries[i].name[0]) n += wsprintfA(line + n, " %.8s", s.entries[i].name);
        for (char* c = line; *c; ++c)
            if (static_cast<unsigned char>(*c) < 0x20 || static_cast<unsigned char>(*c) > 0x7E) *c = '?';
        g_log("%s", line);
    }
}

bool PatchMainMenu() {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(Screens(), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY))) {
        g_log("[coop] menu: tabela aScreens inacessivel; entrada nao adicionada.");
        return false;
    }

    for (int p = 0; p < kScreenCount; ++p) {
        const MenuScreen& s = Screens()[p];
        if (FindEntry(s, "FEP_STG") >= 0 && FindEntry(s, "FEP_QUI") >= 0 && FindEntry(s, "FEP_RES") < 0) {
            g_mainPage = p;
            break;
        }
    }
    if (g_mainPage < 0) {
        g_log("[coop] menu: pagina principal nao encontrada; entrada nao adicionada. Tabela lida:");
        DumpScreens();
        return false;
    }

    MenuScreen& main = Screens()[g_mainPage];
    if (FindEntry(main, kCoopKey) >= 0) return true; // ja aplicado
    int stg = FindEntry(main, "FEP_STG");
    if (stg >= 11 || main.entries[11].name[0] != 0) {
        g_log("[coop] menu: sem linha livre no menu principal; entrada nao adicionada.");
        return false;
    }

    const MenuEntry start = main.entries[stg];
    const MenuEntry next  = main.entries[stg + 1];
    std::memmove(&main.entries[stg + 2], &main.entries[stg + 1], sizeof(MenuEntry) * (10 - stg));

    MenuEntry& e = main.entries[stg + 1];
    e = start;                       // mesma acao/destino de "Iniciar Jogo"
    std::memcpy(e.name, kCoopKey, 8);
    e.x = next.x;                    // ocupa a posicao da linha que deslocou
    e.y = next.y;
    e.align = next.align;
    if (next.name[0] && (next.x != 0 || next.y != 0)) {
        // Posicoes explicitas: desce as linhas seguintes pelo mesmo espacamento.
        int16_t dy = static_cast<int16_t>(next.y - start.y);
        for (int i = stg + 2; i < 12; ++i)
            if (main.entries[i].name[0] && (main.entries[i].x || main.entries[i].y))
                main.entries[i].y = static_cast<int16_t>(main.entries[i].y + dy);
    }

    g_coopRow = stg + 1;
    g_startPage = start.target;
    g_log("[coop] menu: entrada adicionada (pagina %d, linha %d, destino %d).", g_mainPage, g_coopRow,
          g_startPage);
    return true;
}

// Observa a navegacao. Somente leitura; offsets conferidos pelo proprio log.
void TrackNavigation() {
    auto* mgr = reinterpret_cast<const uint8_t*>(addr::FrontEndMenuManager);
    int page = mgr[addr::CMenuManager_m_nCurrentPage];
    int32_t row;
    std::memcpy(&row, mgr + addr::CMenuManager_m_nCurrentRow, 4);

    if (page == g_mainPage) g_lastRowOnMain = row;
    if (page == g_lastPage) return;

    g_log("[coop] menu: pagina %d -> %d (linha no principal = %d).", g_lastPage, page, g_lastRowOnMain);
    if (page == g_startPage && g_lastPage == g_mainPage) {
        g_hostRequested = (g_lastRowOnMain == g_coopRow);
        g_log("[coop] menu: modo %s selecionado.", g_hostRequested ? "COOPERATIVO" : "normal");
    } else if (page == g_mainPage) {
        g_hostRequested = false;
    }
    g_lastPage = page;
}

const char* __fastcall Hook_TextGet(void* self, void* edx, const char* key) {
    if (key && key[0] == 'C' && KeyIs(key, kCoopKey)) {
        TrackNavigation();
        return kCoopLabel;
    }
    if (g_lastPage == g_mainPage && g_lastPage >= 0) TrackNavigation(); // saiu do principal
    return g_origTextGet(self, edx, key);
}

} // namespace

void Install(void (*log)(const char* fmt, ...)) {
    g_log = log;
    if (!PatchMainMenu()) return;
    if (MH_CreateHook(reinterpret_cast<LPVOID>(addr::CText_Get), reinterpret_cast<LPVOID>(&Hook_TextGet),
                      reinterpret_cast<LPVOID*>(&g_origTextGet)) != MH_OK) {
        g_log("[coop] menu: falha no hook de CText::Get (a entrada aparecera sem texto).");
    }
}

bool HostRequested() { return g_hostRequested; }

} // namespace coop::win32::menu
