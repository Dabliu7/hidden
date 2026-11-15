// Testes do nucleo portavel. Sem framework: o binario retorna != 0 em falha.
#include <cmath>
#include <cstdio>

#include "Runtime.h"
#include "net/Protocol.h"
#include "net/Transport.h"
#include "script/HandleMap.h"
#include "script/OpcodePolicy.h"
#include "sync/SnapshotBuffer.h"

using namespace coop;

static int g_failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static void TestSnapshotRoundTrip() {
    PlayerSnapshot a;
    a.slot = 2; a.timeMs = 123456;
    a.pos = {2495.33f, -1687.5f, 13.52f};
    a.rot = Quat{0.1f, -0.2f, 0.6f, 0.76f}.Normalized();
    a.vel = {12.5f, -3.25f, 0.5f};
    a.keys = 0xA5A5; a.health = 100; a.weapon = 31; a.vehicle = 77; a.seat = 1;

    ByteWriter w;
    a.Write(w);
    CHECK(w.Size() <= 48); // cabe folgado em um datagrama a 30 Hz

    ByteReader r(w.Data(), w.Size());
    CHECK(r.Get<Msg>() == Msg::PlayerSnapshot);
    PlayerSnapshot b;
    CHECK(b.Read(r));
    CHECK(r.Remaining() == 0);
    CHECK((a.pos - b.pos).Length() < 0.02f);
    CHECK((a.vel - b.vel).Length() < 0.02f);
    CHECK(std::fabs(a.rot.Dot(b.rot)) > 0.9999f);
    CHECK(b.slot == 2 && b.keys == 0xA5A5 && b.vehicle == 77 && b.seat == 1 && b.weapon == 31);
}

static void TestTruncatedPacketIsRejected() {
    PlayerSnapshot a;
    ByteWriter w;
    a.Write(w);
    for (size_t n = 0; n + 1 < w.Size(); ++n) {
        ByteReader r(w.Data(), n);
        r.Get<Msg>();
        PlayerSnapshot b;
        CHECK(!b.Read(r));
    }
}

static void TestInterpolation() {
    SnapshotBuffer buf;
    CHECK(buf.Sample(0).mode == SnapshotBuffer::Mode::Empty);
    CHECK(buf.Push({1000, {0, 0, 0}, {}, {10, 0, 0}}));
    CHECK(buf.Push({1100, {1, 0, 0}, {}, {10, 0, 0}}));
    CHECK(!buf.Push({1050, {9, 9, 9}, {}, {}})); // fora de ordem: descartado

    auto mid = buf.Sample(1050);
    CHECK(mid.mode == SnapshotBuffer::Mode::Interpolated);
    CHECK(std::fabs(mid.state.pos.x - 0.5f) < 1e-4f);

    auto ext = buf.Sample(1200); // 100 ms alem do ultimo snapshot, 10 m/s
    CHECK(ext.mode == SnapshotBuffer::Mode::Extrapolated);
    CHECK(std::fabs(ext.state.pos.x - 2.0f) < 1e-4f);

    auto stall = buf.Sample(5000); // limite de 250 ms => congela em 1 + 2.5
    CHECK(stall.mode == SnapshotBuffer::Mode::Stalled);
    CHECK(std::fabs(stall.state.pos.x - 3.5f) < 1e-4f);

    for (uint32_t i = 0; i < 100; ++i) buf.Push({2000 + i * 50, {float(i), 0, 0}, {}, {}});
    CHECK(buf.Size() == SnapshotBuffer::kCapacity);
    CHECK(std::fabs(buf.Sample(2000 + 99 * 50 - 25).state.pos.x - 98.5f) < 1e-3f);
}

static void TestOpcodeTable() {
    const OpPolicy* t = OpcodeTableData();
    for (size_t i = 1; i < OpcodeTableSize(); ++i) CHECK(t[i - 1].opcode < t[i].opcode);
    CHECK(LookupOpcode(0x00A5).cls == OpClass::EntityCreate);
    CHECK(LookupOpcode(0x80DB).cls == OpClass::PartyCondition); // NOT IS_CHAR_IN_CAR
    CHECK(LookupOpcode(0x02E9).cls == OpClass::Barrier);
    CHECK(LookupOpcode(0x0004).cls == OpClass::HostOnly);       // SET_VAR_INT: nao tabelado
}

static void TestHandleMap() {
    HandleMap m;
    int32_t h = 0;
    CHECK(!m.ToLocal(EntityKind::Ped, 5, h));
    m.Bind(EntityKind::Ped, 5, 0x1201);
    m.Bind(EntityKind::Vehicle, 5, 0x1201); // mesmo NetId/handle, tipo diferente
    CHECK(m.ToLocal(EntityKind::Ped, 5, h) && h == 0x1201);
    CHECK(m.ToNet(EntityKind::Vehicle, 0x1201) == 5);
    m.Unbind(EntityKind::Ped, 5);
    CHECK(!m.ToLocal(EntityKind::Ped, 5, h));
    CHECK(m.ToNet(EntityKind::Ped, 0x1201) == kInvalidNetId);
    CHECK(m.Size() == 1);
}

static void TestLoopbackTransport() {
    Transport host, client;
    CHECK(host.Host(0));
    CHECK(client.Connect("127.0.0.1", host.BoundPort()));

    bool connected = false, received = false;
    PlayerSnapshot got;
    Transport::Callbacks hostCb, clientCb;
    hostCb.onPacket = [&](Transport::PeerId, Channel ch, const uint8_t* d, size_t n) {
        ByteReader r(d, n);
        if (ch == Channel::State && r.Get<Msg>() == Msg::PlayerSnapshot) received = got.Read(r);
    };
    clientCb.onConnect = [&](Transport::PeerId) { connected = true; };

    PlayerSnapshot s;
    s.slot = 1;
    s.pos = {100, 200, 30};
    for (int i = 0; i < 2000 && !received; ++i) {
        host.Poll(hostCb);
        client.Poll(clientCb);
        if (connected) {
            ByteWriter w;
            s.Write(w);
            client.Broadcast(Channel::State, w);
        }
    }
    CHECK(connected);
    CHECK(received);
    CHECK(got.slot == 1 && std::fabs(got.pos.y - 200.0f) < 0.02f);
}

// Host + cliente completos: atribuicao de slot e estado remoto nos dois
// sentidos, com relogios locais deliberadamente diferentes.
static void TestRuntimeSession() {
    Runtime host, client;
    int hostMsgs = 0, clientMsgs = 0;
    host.SetStatusSink([&](const char*) { ++hostMsgs; });
    client.SetStatusSink([&](const char*) { ++clientMsgs; });
    CHECK(host.StartHost(47811));
    CHECK(client.StartClient("127.0.0.1", 47811));

    bool hostSees = false, clientSees = false;
    for (uint32_t i = 0; i < 3000 && !(hostSees && clientSees); ++i) {
        uint32_t hostNow = 1000000 + i * 10, clientNow = 5000 + i * 10;
        host.Frame(hostNow);
        client.Frame(clientNow);
        PlayerSnapshot h, c;
        h.slot = host.LocalSlot();   h.timeMs = hostNow;   h.pos = {10, 20, 30};
        c.slot = client.LocalSlot(); c.timeMs = clientNow; c.pos = {-5, 6, 7};
        host.SubmitLocalPlayer(h);
        client.SubmitLocalPlayer(c);
        auto a = host.SampleRemotePlayer(1);
        auto b = client.SampleRemotePlayer(0);
        hostSees = a.mode == SnapshotBuffer::Mode::Interpolated && std::fabs(a.state.pos.x + 5.0f) < 0.02f;
        clientSees = b.mode == SnapshotBuffer::Mode::Interpolated && std::fabs(b.state.pos.y - 20.0f) < 0.02f;
    }
    CHECK(client.LocalSlot() == 1);
    CHECK(hostSees);
    CHECK(clientSees);
    CHECK(hostMsgs == 1 && clientMsgs == 1); // "jogador entrou" / "conectado"
}

int main() {
    TestSnapshotRoundTrip();
    TestTruncatedPacketIsRejected();
    TestInterpolation();
    TestOpcodeTable();
    TestHandleMap();
    TestLoopbackTransport();
    TestRuntimeSession();
    std::printf(g_failures ? "%d falha(s)\n" : "todos os testes passaram\n", g_failures);
    return g_failures ? 1 : 0;
}
