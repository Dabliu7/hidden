#include "script/OpcodePolicy.h"

#include <algorithm>
#include <cstddef>

namespace coop {

namespace {

using C = OpClass;
using Q = Quantifier;

// Tabela-semente, ORDENADA por opcode (verificado em tests/test_core.cpp).
// A tabela completa (~1400 opcodes usados pelo main.scm) sera gerada por
// tools/opgen a partir de data/opcodes.json; estas entradas cobrem o caminho
// critico da Fase 4 e servem de referencia de classificacao.
constexpr OpPolicy kTable[] = {
    {0x0053, C::HostOnly,       Q::Protagonist, "CREATE_PLAYER"},
    {0x009A, C::EntityCreate,   Q::Protagonist, "CREATE_CHAR"},
    {0x009B, C::EntityDestroy,  Q::Protagonist, "DELETE_CHAR"},
    {0x00A1, C::PlayerDirected, Q::Protagonist, "SET_CHAR_COORDINATES"},
    {0x00A5, C::EntityCreate,   Q::Protagonist, "CREATE_CAR"},
    {0x00A6, C::EntityDestroy,  Q::Protagonist, "DELETE_CAR"},
    {0x00BA, C::ReplicateFx,    Q::Protagonist, "PRINT_BIG"},
    {0x00BB, C::ReplicateFx,    Q::Protagonist, "PRINT"},
    {0x00BC, C::ReplicateFx,    Q::Protagonist, "PRINT_NOW"},
    {0x00BE, C::ReplicateFx,    Q::Protagonist, "CLEAR_PRINTS"},
    {0x00C0, C::ReplicateFx,    Q::Protagonist, "SET_TIME_OF_DAY"},
    {0x00D8, C::Lifecycle,      Q::Protagonist, "MISSION_HAS_FINISHED"},
    {0x00DB, C::PartyCondition, Q::Protagonist, "IS_CHAR_IN_CAR"},
    {0x00DF, C::PartyCondition, Q::Protagonist, "IS_CHAR_IN_ANY_CAR"},
    {0x00EC, C::PartyCondition, Q::Any,         "LOCATE_CHAR_ANY_MEANS_2D"},
    {0x00FE, C::PartyCondition, Q::Any,         "LOCATE_CHAR_ANY_MEANS_3D"},
    {0x0107, C::EntityCreate,   Q::Protagonist, "CREATE_OBJECT"},
    {0x0108, C::EntityDestroy,  Q::Protagonist, "DELETE_OBJECT"},
    {0x0109, C::PlayerDirected, Q::All,         "ADD_SCORE"},
    {0x0110, C::PlayerDirected, Q::All,         "ALTER_WANTED_LEVEL"},
    {0x0112, C::Lifecycle,      Q::All,         "HAS_DEATHARREST_BEEN_EXECUTED"},
    {0x0118, C::PartyCondition, Q::All,         "IS_CHAR_DEAD"},
    {0x014E, C::ReplicateFx,    Q::Protagonist, "DISPLAY_ONSCREEN_TIMER"},
    {0x015A, C::ReplicateFx,    Q::Protagonist, "RESTORE_CAMERA"},
    {0x015F, C::ReplicateFx,    Q::Protagonist, "SET_FIXED_CAMERA_POSITION"},
    {0x0160, C::ReplicateFx,    Q::Protagonist, "POINT_CAMERA_AT_POINT"},
    {0x0164, C::ReplicateFx,    Q::Protagonist, "REMOVE_BLIP"},
    {0x016A, C::ReplicateFx,    Q::Protagonist, "DO_FADE"},
    {0x0186, C::ReplicateFx,    Q::Protagonist, "ADD_BLIP_FOR_CAR"},
    {0x0187, C::ReplicateFx,    Q::Protagonist, "ADD_BLIP_FOR_CHAR"},
    {0x018A, C::ReplicateFx,    Q::Protagonist, "ADD_BLIP_FOR_COORD"},
    {0x01B2, C::PlayerDirected, Q::All,         "GIVE_WEAPON_TO_CHAR"},
    {0x01B4, C::PlayerDirected, Q::All,         "SET_PLAYER_CONTROL"},
    {0x01B6, C::ReplicateFx,    Q::Protagonist, "FORCE_WEATHER_NOW"},
    {0x01C2, C::EntityDestroy,  Q::Protagonist, "MARK_CHAR_AS_NO_LONGER_NEEDED"},
    {0x01C3, C::EntityDestroy,  Q::Protagonist, "MARK_CAR_AS_NO_LONGER_NEEDED"},
    {0x0213, C::EntityCreate,   Q::Protagonist, "CREATE_PICKUP"},
    {0x0223, C::PlayerDirected, Q::Protagonist, "SET_CHAR_HEALTH"},
    {0x0247, C::ReplicateFx,    Q::Protagonist, "REQUEST_MODEL"},
    {0x0248, C::Barrier,        Q::All,         "HAS_MODEL_LOADED"},
    {0x0256, C::PartyCondition, Q::Any,         "IS_PLAYER_PLAYING"},
    {0x02A3, C::ReplicateFx,    Q::Protagonist, "SWITCH_WIDESCREEN"},
    {0x02E4, C::ReplicateFx,    Q::Protagonist, "LOAD_CUTSCENE"},
    {0x02E7, C::ReplicateFx,    Q::Protagonist, "START_CUTSCENE"},
    {0x02E9, C::Barrier,        Q::All,         "HAS_CUTSCENE_FINISHED"},
    {0x02EA, C::ReplicateFx,    Q::Protagonist, "CLEAR_CUTSCENE"},
    {0x02EB, C::ReplicateFx,    Q::Protagonist, "RESTORE_CAMERA_JUMPCUT"},
    {0x0318, C::Lifecycle,      Q::Protagonist, "REGISTER_MISSION_PASSED"},
    {0x036A, C::PlayerDirected, Q::Protagonist, "WARP_CHAR_INTO_CAR"},
    {0x03CF, C::ReplicateFx,    Q::Protagonist, "LOAD_MISSION_AUDIO"},
    {0x03D0, C::Barrier,        Q::All,         "HAS_MISSION_AUDIO_LOADED"},
    {0x03D1, C::ReplicateFx,    Q::Protagonist, "PLAY_MISSION_AUDIO"},
    {0x03E5, C::ReplicateFx,    Q::Protagonist, "PRINT_HELP"},
    {0x0417, C::Lifecycle,      Q::Protagonist, "LOAD_AND_LAUNCH_MISSION_INTERNAL"},
    {0x04BB, C::PlayerDirected, Q::All,         "SET_AREA_VISIBLE"},
    {0x05CA, C::ReplicateTask,  Q::Protagonist, "TASK_ENTER_CAR_AS_PASSENGER"},
    {0x05CB, C::ReplicateTask,  Q::Protagonist, "TASK_ENTER_CAR_AS_DRIVER"},
    {0x05D3, C::ReplicateTask,  Q::Protagonist, "TASK_GO_STRAIGHT_TO_COORD"},
    {0x0605, C::ReplicateTask,  Q::Protagonist, "TASK_PLAY_ANIM"},
    {0x0615, C::ReplicateTask,  Q::Protagonist, "OPEN_SEQUENCE_TASK"},
    {0x0616, C::ReplicateTask,  Q::Protagonist, "CLOSE_SEQUENCE_TASK"},
    {0x0618, C::ReplicateTask,  Q::Protagonist, "PERFORM_SEQUENCE_TASK"},
    {0x06B9, C::Barrier,        Q::All,         "HAS_CUTSCENE_LOADED"},
    {0x06D5, C::ReplicateFx,    Q::Protagonist, "CREATE_RACING_CHECKPOINT"},
    {0x06D6, C::ReplicateFx,    Q::Protagonist, "DELETE_CHECKPOINT"},
    {0x0860, C::PlayerDirected, Q::Protagonist, "SET_CHAR_AREA_VISIBLE"},
};

constexpr OpPolicy kDefault = {0xFFFF, C::HostOnly, Q::Protagonist, "<host-only>"};

} // namespace

const OpPolicy& LookupOpcode(uint16_t opcode) {
    opcode &= 0x7FFF; // bit 15 = condicao negada (NOT)
    const OpPolicy* end = kTable + sizeof(kTable) / sizeof(kTable[0]);
    const OpPolicy* it = std::lower_bound(
        kTable, end, opcode, [](const OpPolicy& p, uint16_t op) { return p.opcode < op; });
    return (it != end && it->opcode == opcode) ? *it : kDefault;
}

size_t OpcodeTableSize() { return sizeof(kTable) / sizeof(kTable[0]); }
const OpPolicy* OpcodeTableData() { return kTable; }

} // namespace coop
