#include "sync/SnapshotBuffer.h"

namespace coop {

bool SnapshotBuffer::Push(const TransformSample& s) {
    if (count_ > 0) {
        // diferenca com sinal: tolera wrap do relogio de 32 bits (~49 dias)
        auto delta = static_cast<int32_t>(s.timeMs - At(count_ - 1).timeMs);
        if (delta <= 0) return false;
    }
    if (count_ == kCapacity) {
        head_ = (head_ + 1) % kCapacity;
        --count_;
    }
    buf_[(head_ + count_) % kCapacity] = s;
    ++count_;
    return true;
}

SnapshotBuffer::Result SnapshotBuffer::Sample(uint32_t t, uint32_t maxExtrapolationMs) const {
    Result r;
    if (count_ == 0) return r;

    const TransformSample& newest = At(count_ - 1);
    auto ahead = static_cast<int32_t>(t - newest.timeMs);
    if (ahead >= 0) {
        // Buffer seco: dead reckoning linear, limitado. Passado o limite a
        // entidade congela no ultimo ponto previsto em vez de "voar".
        uint32_t dt = static_cast<uint32_t>(ahead);
        r.mode = dt > maxExtrapolationMs ? Mode::Stalled : Mode::Extrapolated;
        if (dt > maxExtrapolationMs) dt = maxExtrapolationMs;
        r.state = newest;
        r.state.timeMs = t;
        r.state.pos = newest.pos + newest.vel * (dt / 1000.0f);
        if (r.mode == Mode::Stalled) r.state.vel = {};
        return r;
    }

    const TransformSample& oldest = At(0);
    if (static_cast<int32_t>(t - oldest.timeMs) <= 0) {
        r.state = oldest;
        r.mode = Mode::Interpolated;
        return r;
    }

    for (size_t i = count_ - 1; i > 0; --i) {
        const TransformSample& a = At(i - 1);
        const TransformSample& b = At(i);
        if (static_cast<int32_t>(t - a.timeMs) >= 0) {
            float span = static_cast<float>(b.timeMs - a.timeMs);
            float k = span > 0 ? static_cast<float>(t - a.timeMs) / span : 0.0f;
            r.state.timeMs = t;
            r.state.pos = Lerp(a.pos, b.pos, k);
            r.state.rot = Nlerp(a.rot, b.rot, k);
            r.state.vel = Lerp(a.vel, b.vel, k);
            r.mode = Mode::Interpolated;
            return r;
        }
    }
    r.state = oldest;
    r.mode = Mode::Interpolated;
    return r;
}

} // namespace coop
