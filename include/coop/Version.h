#pragma once
#include <cstdint>

namespace coop {

// Incrementar a cada mudanca incompativel de wire format. Peers com versoes
// diferentes sao rejeitados no handshake (Msg::Reject).
constexpr uint16_t kProtocolVersion = 1;
constexpr uint16_t kDefaultPort     = 7777;
constexpr uint8_t  kMaxPlayers      = 4;
constexpr uint32_t kProtocolMagic   = 0x504F4353; // 'SCOP'

} // namespace coop
