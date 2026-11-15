#include "Runtime.h"

#include <cstdio>

namespace coop {

Runtime& Runtime::Get() {
    static Runtime r;
    return r;
}

bool Runtime::StartHost(uint16_t port) {
    localSlot_ = 0;
    ready_ = transport_.Host(port);
    return ready_;
}

bool Runtime::StartClient(const std::string& address, uint16_t port) {
    ready_ = false; // so envia estado depois que o host atribuir o slot (Welcome)
    return transport_.Connect(address, port);
}

void Runtime::Stop() {
    transport_.Close();
    for (auto& b : remote_) b.Clear();
    offsets_ = {};
    ready_ = false;
    pendingStop_ = false;
}

void Runtime::Status(const char* fmt, int n) {
    if (!status_) return;
    char buf[96];
    std::snprintf(buf, sizeof(buf), fmt, n);
    status_(buf);
}

void Runtime::Frame(uint32_t nowMs) {
    nowMs_ = nowMs;
    Transport::Callbacks cb;
    cb.onConnect = [this](Transport::PeerId p) { OnConnect(p); };
    cb.onDisconnect = [this](Transport::PeerId p) { OnDisconnect(p); };
    cb.onPacket = [this](Transport::PeerId from, Channel ch, const uint8_t* d, size_t n) {
        OnPacket(from, ch, d, n);
    };
    transport_.Poll(cb);
    if (pendingStop_) Stop(); // fora do Poll: nao destruir o host ENet dentro do callback
}

void Runtime::OnConnect(Transport::PeerId peer) {
    if (!IsHost()) return; // cliente: aguarda Welcome
    auto slot = static_cast<uint8_t>(peer + 1);
    ByteWriter w;
    w.Put(Msg::Welcome);
    w.Put(kProtocolVersion);
    w.Put(slot);
    transport_.Send(peer, Channel::Control, w);
    Status("Coop: jogador %d entrou.", slot + 1);
}

void Runtime::OnDisconnect(Transport::PeerId peer) {
    if (IsHost()) {
        auto slot = static_cast<uint8_t>(peer + 1);
        if (slot < kMaxPlayers) { remote_[slot].Clear(); offsets_[slot] = {}; }
        Status("Coop: jogador %d saiu.", slot + 1);
    } else {
        Status(ready_ ? "Coop: conexao com o host perdida." : "Coop: nao foi possivel conectar ao host.");
        pendingStop_ = true;
    }
}

void Runtime::SubmitLocalPlayer(const PlayerSnapshot& s) {
    if (!Active() || !ready_) return;
    ByteWriter w;
    s.Write(w);
    transport_.Broadcast(Channel::State, w);
}

SnapshotBuffer::Result Runtime::SampleRemotePlayer(uint8_t slot) const {
    if (slot >= kMaxPlayers) return {};
    return remote_[slot].Sample(nowMs_ - kInterpDelayMs);
}

void Runtime::OnPacket(Transport::PeerId from, Channel ch, const uint8_t* data, size_t len) {
    ByteReader r(data, len);
    auto type = r.Get<Msg>();
    if (!r.Ok()) return;

    if (ch == Channel::Control && type == Msg::Welcome && !IsHost()) {
        auto protocol = r.Get<uint16_t>();
        auto slot = r.Get<uint8_t>();
        if (!r.Ok() || slot == 0 || slot >= kMaxPlayers) return;
        if (protocol != kProtocolVersion) {
            Status("Coop: versao diferente da do host.");
            pendingStop_ = true;
            return;
        }
        localSlot_ = slot;
        ready_ = true;
        Status("Coop: conectado como jogador %d.", slot + 1);
        return;
    }

    if (ch == Channel::State && type == Msg::PlayerSnapshot) {
        PlayerSnapshot s;
        if (!s.Read(r)) return;
        if (IsHost()) s.slot = static_cast<uint8_t>(from + 1); // o slot e o que o host atribuiu
        if (s.slot >= kMaxPlayers || s.slot == localSlot_) return;

        ClockOffset& off = offsets_[s.slot];
        auto delta = static_cast<int32_t>(nowMs_ - s.timeMs);
        if (!off.valid || delta < off.value) off = {true, delta};
        remote_[s.slot].Push({s.timeMs + static_cast<uint32_t>(off.value), s.pos, s.rot, s.vel});

        if (IsHost()) {
            // Topologia em estrela: o host retransmite para os demais clientes.
            ByteWriter w;
            s.Write(w);
            for (Transport::PeerId p = 0; p < kMaxPlayers - 1u; ++p)
                if (p != from) transport_.Send(p, Channel::State, w);
        }
    }
    // Demais mensagens: ver docs/ROADMAP.md.
}

} // namespace coop
