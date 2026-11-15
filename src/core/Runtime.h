#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "net/Transport.h"
#include "sync/SnapshotBuffer.h"

namespace coop {

// Ponto unico de entrada do nucleo portavel. A camada de plataforma
// (platform/win32, platform/android) chama Frame() uma vez por iteracao do
// loop do jogo, a partir da thread do jogo.
class Runtime {
public:
    static Runtime& Get();

    bool StartHost(uint16_t port = kDefaultPort);
    bool StartClient(const std::string& address, uint16_t port = kDefaultPort);
    void Stop();

    void Frame(uint32_t nowMs);

    // Publica o estado do jogador local (preenchido pela camada de plataforma).
    void SubmitLocalPlayer(const PlayerSnapshot& s);

    // Estado interpolado de um jogador remoto para o instante de render atual.
    SnapshotBuffer::Result SampleRemotePlayer(uint8_t slot) const;

    bool Active() const { return transport_.IsOpen(); }
    bool IsHost() const { return transport_.IsHost(); }
    uint8_t LocalSlot() const { return localSlot_; }

    static constexpr uint32_t kInterpDelayMs = 100;

private:
    void OnPacket(Transport::PeerId from, Channel ch, const uint8_t* data, size_t len);

    Transport transport_;
    std::array<SnapshotBuffer, kMaxPlayers> remote_{};
    uint32_t nowMs_ = 0;
    uint8_t localSlot_ = 0;
};

} // namespace coop
