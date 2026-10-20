#pragma once

#include <cmath>

namespace gs {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegToRad = kPi / 180.0;

constexpr double radians(double deg) noexcept
{
    return deg * kDegToRad;
}

// Floored modulo: the result takes the sign of the divisor, so wrapping a time
// or an angle by a positive period always lands in [0, period). (std::fmod
// keeps the sign of the dividend.)
inline double floor_mod(double a, double b) noexcept
{
    double mod = std::fmod(a, b);
    if (mod != 0.0) {
        if ((b < 0.0) != (mod < 0.0))
            mod += b;
    } else {
        mod = std::copysign(0.0, b);
    }
    return mod;
}

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    friend constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend constexpr Vec3 operator*(Vec3 a, double k) noexcept { return {a.x * k, a.y * k, a.z * k}; }
    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

} // namespace gs
