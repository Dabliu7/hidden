#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "net/Protocol.h"

struct _ENetHost;
struct _ENetPeer;

namespace coop {

// Wrapper fino sobre ENet. Roda inteiramente na thread do jogo (Poll e chamado
// uma vez por frame, timeout 0): RenderWare e os pools do jogo nao sao
// thread-safe, entao nenhum callback pode tocar no jogo a partir de outra thread.
class Transport {
public:
    using PeerId = uint32_t; // indice do peer no host ENet

    struct Callbacks {
        std::function<void(PeerId)> onConnect;
        std::function<void(PeerId)> onDisconnect;
        std::function<void(PeerId, Channel, const uint8_t*, size_t)> onPacket;
    };

    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    bool Host(uint16_t port, uint8_t maxClients = kMaxPlayers - 1);
    bool Connect(const std::string& address, uint16_t port);
    void Close();

    void Poll(const Callbacks& cb);
    bool Send(PeerId peer, Channel ch, const ByteWriter& w);
    void Broadcast(Channel ch, const ByteWriter& w);

    bool IsHost() const { return isHost_; }
    bool IsOpen() const { return host_ != nullptr; }
    uint16_t BoundPort() const;

private:
    static uint32_t FlagsFor(Channel ch);
    _ENetHost* host_ = nullptr;
    _ENetPeer* server_ = nullptr;
    bool isHost_ = false;
};

} // namespace coop
