#include "net/Transport.h"

#include <enet/enet.h>

namespace coop {

namespace {
int g_enetRefs = 0;
}

Transport::Transport() {
    if (g_enetRefs++ == 0) enet_initialize();
}

Transport::~Transport() {
    Close();
    if (--g_enetRefs == 0) enet_deinitialize();
}

bool Transport::Host(uint16_t port, uint8_t maxClients) {
    Close();
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    host_ = enet_host_create(&addr, maxClients, static_cast<size_t>(Channel::Count), 0, 0);
    isHost_ = host_ != nullptr;
    return isHost_;
}

bool Transport::Connect(const std::string& address, uint16_t port) {
    Close();
    host_ = enet_host_create(nullptr, 1, static_cast<size_t>(Channel::Count), 0, 0);
    if (!host_) return false;
    ENetAddress addr;
    if (enet_address_set_host(&addr, address.c_str()) != 0) { Close(); return false; }
    addr.port = port;
    server_ = enet_host_connect(host_, &addr, static_cast<size_t>(Channel::Count), kProtocolVersion);
    if (!server_) { Close(); return false; }
    return true;
}

void Transport::Close() {
    if (!host_) return;
    for (size_t i = 0; i < host_->peerCount; ++i)
        if (host_->peers[i].state != ENET_PEER_STATE_DISCONNECTED)
            enet_peer_disconnect_now(&host_->peers[i], 0);
    enet_host_destroy(host_);
    host_ = nullptr;
    server_ = nullptr;
    isHost_ = false;
}

uint16_t Transport::BoundPort() const { return host_ ? host_->address.port : 0; }

uint32_t Transport::FlagsFor(Channel ch) {
    // State: unreliable + sequenced (padrao ENet para pacotes sem flag).
    return ch == Channel::State ? 0u : static_cast<uint32_t>(ENET_PACKET_FLAG_RELIABLE);
}

void Transport::Poll(const Callbacks& cb) {
    if (!host_) return;
    ENetEvent ev;
    while (enet_host_service(host_, &ev, 0) > 0) {
        auto id = static_cast<PeerId>(ev.peer - host_->peers);
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (cb.onConnect) cb.onConnect(id);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (cb.onDisconnect) cb.onDisconnect(id);
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (cb.onPacket && ev.channelID < static_cast<uint8_t>(Channel::Count))
                cb.onPacket(id, static_cast<Channel>(ev.channelID), ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        default:
            break;
        }
    }
}

bool Transport::Send(PeerId peer, Channel ch, const ByteWriter& w) {
    if (!host_ || peer >= host_->peerCount) return false;
    ENetPacket* p = enet_packet_create(w.Data(), w.Size(), FlagsFor(ch));
    if (!p) return false;
    if (enet_peer_send(&host_->peers[peer], static_cast<enet_uint8>(ch), p) != 0) {
        enet_packet_destroy(p);
        return false;
    }
    return true;
}

void Transport::Broadcast(Channel ch, const ByteWriter& w) {
    if (!host_) return;
    ENetPacket* p = enet_packet_create(w.Data(), w.Size(), FlagsFor(ch));
    if (p) enet_host_broadcast(host_, static_cast<enet_uint8>(ch), p);
}

} // namespace coop
