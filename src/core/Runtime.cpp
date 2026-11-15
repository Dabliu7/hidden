#include "Runtime.h"

namespace coop {

Runtime& Runtime::Get() {
    static Runtime r;
    return r;
}

bool Runtime::StartHost(uint16_t port) {
    localSlot_ = 0;
    return transport_.Host(port);
}

bool Runtime::StartClient(const std::string& address, uint16_t port) {
    localSlot_ = 1; // provisorio; o slot definitivo vem em Msg::Welcome (Fase 2)
    return transport_.Connect(address, port);
}

void Runtime::Stop() {
    transport_.Close();
    for (auto& b : remote_) b.Clear();
}

void Runtime::Frame(uint32_t nowMs) {
    nowMs_ = nowMs;
    Transport::Callbacks cb;
    cb.onPacket = [this](Transport::PeerId from, Channel ch, const uint8_t* d, size_t n) {
        OnPacket(from, ch, d, n);
    };
    transport_.Poll(cb);
}

void Runtime::SubmitLocalPlayer(const PlayerSnapshot& s) {
    if (!Active()) return;
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

    if (ch == Channel::State && type == Msg::PlayerSnapshot) {
        PlayerSnapshot s;
        if (!s.Read(r) || s.slot == localSlot_) return;
        remote_[s.slot].Push({s.timeMs, s.pos, s.rot, s.vel});
        if (IsHost()) {
            // Topologia em estrela: o host retransmite para os demais clientes.
            ByteWriter w;
            s.Write(w);
            for (Transport::PeerId p = 0; p < kMaxPlayers - 1u; ++p)
                if (p != from) transport_.Send(p, Channel::State, w);
        }
    }
    // Demais mensagens: Fases 2-4 (ver docs/ROADMAP.md).
}

} // namespace coop
