#pragma once
// Quantizacao em ponto fixo. Dois objetivos: (1) banda; (2) o valor que
// trafega e um inteiro, entao PC e Android reconstroem exatamente o mesmo
// estado apesar de x87 (80 bits internos) vs NEON/VFP.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "coop/Math.h"
#include "net/ByteStream.h"

namespace coop {

constexpr float kPosScale = 128.0f;   // 1/128 m  (~7.8 mm); mapa SA = +-3000 m
constexpr float kVelScale = 256.0f;   // 1/256 m/s; int16 cobre +-128 m/s

inline void PutPos(ByteWriter& w, const Vec3& p) {
    w.Put(static_cast<int32_t>(std::lround(p.x * kPosScale)));
    w.Put(static_cast<int32_t>(std::lround(p.y * kPosScale)));
    w.Put(static_cast<int32_t>(std::lround(p.z * kPosScale)));
}
inline Vec3 GetPos(ByteReader& r) {
    Vec3 p;
    p.x = r.Get<int32_t>() / kPosScale;
    p.y = r.Get<int32_t>() / kPosScale;
    p.z = r.Get<int32_t>() / kPosScale;
    return p;
}

inline int16_t QVel(float v) {
    return static_cast<int16_t>(std::lround(std::clamp(v * kVelScale, -32767.0f, 32767.0f)));
}
inline void PutVel(ByteWriter& w, const Vec3& v) { w.Put(QVel(v.x)); w.Put(QVel(v.y)); w.Put(QVel(v.z)); }
inline Vec3 GetVel(ByteReader& r) {
    Vec3 v;
    v.x = r.Get<int16_t>() / kVelScale;
    v.y = r.Get<int16_t>() / kVelScale;
    v.z = r.Get<int16_t>() / kVelScale;
    return v;
}

// Quaternion "smallest three": 2 bits (indice do maior componente) + 3x10 bits.
inline void PutQuat(ByteWriter& w, Quat q) {
    q = q.Normalized();
    float c[4] = {q.x, q.y, q.z, q.w};
    int big = 0;
    for (int i = 1; i < 4; ++i)
        if (std::fabs(c[i]) > std::fabs(c[big])) big = i;
    float sign = c[big] < 0 ? -1.0f : 1.0f;
    uint32_t out = static_cast<uint32_t>(big) << 30;
    int shift = 20;
    for (int i = 0; i < 4; ++i) {
        if (i == big) continue;
        // componentes restantes ficam em [-1/sqrt2, 1/sqrt2]
        float n = std::clamp(c[i] * sign * 1.41421356f, -1.0f, 1.0f);
        auto qv = static_cast<uint32_t>(std::lround((n * 0.5f + 0.5f) * 1023.0f));
        out |= (qv & 0x3FF) << shift;
        shift -= 10;
    }
    w.Put(out);
}
inline Quat GetQuat(ByteReader& r) {
    uint32_t in = r.Get<uint32_t>();
    int big = static_cast<int>(in >> 30);
    float c[4];
    float sum = 0;
    int shift = 20;
    for (int i = 0; i < 4; ++i) {
        if (i == big) continue;
        float n = (((in >> shift) & 0x3FF) / 1023.0f - 0.5f) * 2.0f;
        c[i] = n / 1.41421356f;
        sum += c[i] * c[i];
        shift -= 10;
    }
    c[big] = std::sqrt(std::max(0.0f, 1.0f - sum));
    return Quat{c[0], c[1], c[2], c[3]}.Normalized();
}

} // namespace coop
