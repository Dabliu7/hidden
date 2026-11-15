#pragma once
// Classificacao de opcodes do main.scm. O host e o UNICO peer que executa
// bytecode; esta tabela decide o que acontece com cada comando depois que o
// handler nativo roda no host. Ver docs/ARCHITECTURE.md secao 5.
#include <cstddef>
#include <cstdint>

namespace coop {

enum class OpClass : uint8_t {
    HostOnly,       // logica pura (variaveis, saltos, matematica, timers): nao sai do host
    ReplicateFx,    // efeito sem entidade (texto, fade, camera, audio, clima, blip): RPC p/ todos
    ReplicateTask,  // TASK_* / sequencias: RPC p/ todos; so o "syncer" da entidade simula de fato
    EntityCreate,   // cria entidade de script: aloca NetId + EntityCreate no canal Script
    EntityDestroy,  // destroi/libera entidade de script
    PlayerDirected, // age sobre o ped protagonista: encaminhado ao DONO do ped (e aplicado ao grupo conforme a politica)
    PartyCondition, // condicao sobre o jogador: avaliada com quantificador sobre o grupo
    Barrier,        // condicao de "pronto" (modelo/cutscene/audio carregado): so e verdadeira quando TODOS os peers confirmam
    Lifecycle,      // inicio/fim de missao, deatharrest: alimenta a MissionStateMachine
};

// Como uma condicao que referencia o jogador e avaliada com 2-4 jogadores.
enum class Quantifier : uint8_t {
    Protagonist, // apenas o ped vinculado ao token de protagonista (default)
    Any,         // qualquer membro do grupo satisfaz (ex.: chegar ao marcador)
    All,         // todos precisam satisfazer (ex.: todos mortos => falha)
};

struct OpPolicy {
    uint16_t    opcode;
    OpClass     cls;
    Quantifier  quant;
    const char* name;
};

// Opcode ausente da tabela => HostOnly (seguro: nada vaza para os clientes).
const OpPolicy& LookupOpcode(uint16_t opcode);

size_t OpcodeTableSize();
const OpPolicy* OpcodeTableData();

} // namespace coop
