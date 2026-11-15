#pragma once
#include <array>
#include <cstdint>
#include <functional>
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

    // Mensagens curtas de estado ("jogador entrou", "conectado"...) para a
    // camada de plataforma exibir na tela/log. Chamado na thread do jogo.
    void SetStatusSink(std::function<void(const char*)> sink) { status_ = std::move(sink); }

    bool Active() const { return transport_.IsOpen(); }
    bool IsHost() const { return transport_.IsHost(); }
    uint8_t LocalSlot() const { return localSlot_; }

    static constexpr uint32_t kInterpDelayMs = 100;

private:
    void OnPacket(Transport::PeerId from, Channel ch, const uint8_t* data, size_t len);
    void OnConnect(Transport::PeerId peer);
    void OnDisconnect(Transport::PeerId peer);
    void Status(const char* fmt, int n = 0);

    // Sem TimeSync ainda: cada remetente carimba com o proprio relogio. O
    // receptor converte para o relogio local usando o MENOR atraso observado
    // (a amostra que menos esperou na rede e a melhor estimativa do offset).
    struct ClockOffset { bool valid = false; int32_t value = 0; };
    std::array<ClockOffset, kMaxPlayers> offsets_{};
    std::function<void(const char*)> status_;
    bool ready_ = false;       // host: sempre; cliente: apos Welcome
    bool pendingStop_ = false;

    Transport transport_;
    std::array<SnapshotBuffer, kMaxPlayers> remote_{};
    uint32_t nowMs_ = 0;
    uint8_t localSlot_ = 0;
};

} // namespace coop
