#pragma once
// Traducao NetId <-> handle local de pool.
// Um handle de script do GTA e (indice_no_pool << 8) | flags_do_slot, logo o
// MESMO ped tem handles diferentes em cada peer. Nada que contenha handle
// local pode ir para a rede: tudo e convertido para NetId na borda.
#include <cstdint>
#include <unordered_map>

#include "net/Protocol.h"

namespace coop {

enum class EntityKind : uint8_t { Ped = 0, Vehicle = 1, Object = 2, Pickup = 3, Blip = 4, Checkpoint = 5 };

class HandleMap {
public:
    void Bind(EntityKind kind, NetId id, int32_t localHandle) {
        toLocal_[Key(kind, id)] = localHandle;
        toNet_[LKey(kind, localHandle)] = id;
    }
    void Unbind(EntityKind kind, NetId id) {
        auto it = toLocal_.find(Key(kind, id));
        if (it == toLocal_.end()) return;
        toNet_.erase(LKey(kind, it->second));
        toLocal_.erase(it);
    }
    // false => entidade ainda nao existe localmente; o chamador (ScriptRpc
    // executor) deve enfileirar o comando em vez de executa-lo com handle -1.
    bool ToLocal(EntityKind kind, NetId id, int32_t& out) const {
        auto it = toLocal_.find(Key(kind, id));
        if (it == toLocal_.end()) return false;
        out = it->second;
        return true;
    }
    NetId ToNet(EntityKind kind, int32_t localHandle) const {
        auto it = toNet_.find(LKey(kind, localHandle));
        return it == toNet_.end() ? kInvalidNetId : it->second;
    }
    size_t Size() const { return toLocal_.size(); }

private:
    static uint32_t Key(EntityKind k, NetId id) { return (static_cast<uint32_t>(k) << 16) | id; }
    static uint64_t LKey(EntityKind k, int32_t h) {
        return (static_cast<uint64_t>(k) << 32) | static_cast<uint32_t>(h);
    }
    std::unordered_map<uint32_t, int32_t> toLocal_;
    std::unordered_map<uint64_t, NetId> toNet_;
};

} // namespace coop
