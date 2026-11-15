#pragma once
// Buffer de interpolacao para entidades remotas. A entidade e renderizada em
// (agora - atraso), entre dois snapshots reais; so extrapola (dead reckoning)
// quando o buffer seca, e por tempo limitado.
#include <array>
#include <cstddef>
#include <cstdint>

#include "coop/Math.h"

namespace coop {

struct TransformSample {
    uint32_t timeMs = 0;
    Vec3 pos;
    Quat rot;
    Vec3 vel; // m/s
};

class SnapshotBuffer {
public:
    static constexpr size_t kCapacity = 32;

    enum class Mode : uint8_t { Empty, Interpolated, Extrapolated, Stalled };

    struct Result {
        TransformSample state;
        Mode mode = Mode::Empty;
    };

    // Descarta amostras fora de ordem (canal unreliable pode reordenar).
    bool Push(const TransformSample& s);

    // renderTimeMs = relogio de sessao - atraso de interpolacao.
    Result Sample(uint32_t renderTimeMs, uint32_t maxExtrapolationMs = 250) const;

    size_t Size() const { return count_; }
    void Clear() { count_ = 0; head_ = 0; }

private:
    const TransformSample& At(size_t i) const { return buf_[(head_ + i) % kCapacity]; }
    std::array<TransformSample, kCapacity> buf_{};
    size_t head_ = 0;  // indice da amostra mais antiga
    size_t count_ = 0;
};

} // namespace coop
