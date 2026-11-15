#pragma once
#include <cmath>

namespace coop {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    float Dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    Quat Normalized() const {
        float l = std::sqrt(Dot(*this));
        if (l < 1e-8f) return {};
        return {x / l, y / l, z / l, w / l};
    }
};

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// nlerp com correcao de hemisferio: suficiente para deltas de 50 ms e muito
// mais barato/estavel que slerp entre x87 e NEON.
inline Quat Nlerp(const Quat& a, Quat b, float t) {
    if (a.Dot(b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
    return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.Normalized();
}

} // namespace coop
