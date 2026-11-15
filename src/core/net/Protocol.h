#pragma once
#include <cstdint>

#include "coop/Math.h"
#include "coop/Version.h"
#include "net/ByteStream.h"
#include "net/Quantize.h"

namespace coop {

// Canais ENet. A separacao e funcional, nao cosmetica:
//  - Script e reliable+ordered e carrega TAMBEM o ciclo de vida das entidades
//    de missao; assim um ScriptRpc nunca chega antes do EntityCreate do qual
//    depende (ver docs/ARCHITECTURE.md secao 5.4).
//  - State e unreliable-sequenced: snapshot perdido nao e retransmitido.
enum class Channel : uint8_t { Control = 0, Script = 1, State = 2, Event = 3, Count = 4 };

enum class Msg : uint8_t {
    // Control
    Hello = 1, Welcome, Reject, LobbyState, StartGame, TimeSync, WorldBaseline,
    // State
    PlayerSnapshot = 16, VehicleSnapshot, PedSnapshot,
    // Script (reliable ordered)
    EntityCreate = 32, EntityDestroy, OwnershipChange,
    ScriptRpc = 48, Barrier, BarrierAck, MissionState, ProtagonistBind,
    // Event
    Fire = 64, Damage, Explosion, EnterVehicle, ExitVehicle,
};

enum class Platform : uint8_t { Win32 = 0, AndroidArm32 = 1, AndroidArm64 = 2, Host = 255 };

using NetId = uint16_t;           // 0 = invalido
constexpr NetId kInvalidNetId = 0;

struct Hello {
    uint32_t magic = kProtocolMagic;
    uint16_t protocol = kProtocolVersion;
    Platform platform = Platform::Host;
    std::string name;

    void Write(ByteWriter& w) const {
        w.Put(Msg::Hello); w.Put(magic); w.Put(protocol); w.Put(platform); w.PutStr(name);
    }
    bool Read(ByteReader& r) {
        magic = r.Get<uint32_t>(); protocol = r.Get<uint16_t>();
        platform = r.Get<Platform>(); name = r.GetStr();
        return r.Ok() && magic == kProtocolMagic;
    }
};

// Estado on-foot/in-vehicle do jogador, 20-30 Hz, canal State.
// Tudo em unidades SI no fio (m, m/s); a conversao de/para as unidades internas
// do jogo (m por passo de 1/50 s) fica na camada de plataforma.
struct PlayerSnapshot {
    uint8_t  slot = 0;
    uint32_t timeMs = 0;       // relogio de sessao (ver TimeSync)
    Vec3     pos;
    Quat     rot;
    Vec3     vel;
    uint16_t keys = 0;         // bitmask logico (sprint, pulo, tiro, mira...) - nao e CPad cru
    int16_t  aimYaw = 0;       // 1/32768 de pi
    int16_t  aimPitch = 0;
    uint8_t  health = 0;
    uint8_t  armour = 0;
    uint8_t  weapon = 0;
    uint8_t  moveState = 0;
    uint8_t  interior = 0;
    NetId    vehicle = kInvalidNetId;
    uint8_t  seat = 0;

    void Write(ByteWriter& w) const {
        w.Put(Msg::PlayerSnapshot); w.Put(slot); w.Put(timeMs);
        PutPos(w, pos); PutQuat(w, rot); PutVel(w, vel);
        w.Put(keys); w.Put(aimYaw); w.Put(aimPitch);
        w.Put(health); w.Put(armour); w.Put(weapon); w.Put(moveState); w.Put(interior);
        w.Put(vehicle); w.Put(seat);
    }
    bool Read(ByteReader& r) {
        slot = r.Get<uint8_t>(); timeMs = r.Get<uint32_t>();
        pos = GetPos(r); rot = GetQuat(r); vel = GetVel(r);
        keys = r.Get<uint16_t>(); aimYaw = r.Get<int16_t>(); aimPitch = r.Get<int16_t>();
        health = r.Get<uint8_t>(); armour = r.Get<uint8_t>(); weapon = r.Get<uint8_t>();
        moveState = r.Get<uint8_t>(); interior = r.Get<uint8_t>();
        vehicle = r.Get<NetId>(); seat = r.Get<uint8_t>();
        return r.Ok() && slot < kMaxPlayers;
    }
};

} // namespace coop
