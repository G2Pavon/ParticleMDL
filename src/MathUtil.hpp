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

struct Vec2 {
    double x = 0.0, y = 0.0;

    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    friend constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend constexpr Vec3 operator*(Vec3 a, double k) noexcept { return {a.x * k, a.y * k, a.z * k}; }
    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

// Unit quaternion (w + xi + yj + zk). GoldSrc blends bone rotations as quaternions.
struct Quat {
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;

    friend constexpr Quat operator*(const Quat& a, const Quat& b) noexcept
    {
        return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
    }
    friend constexpr bool operator==(const Quat&, const Quat&) = default;
};

inline Quat axis_rotation(double angle, int axis) noexcept
{
    Quat q{std::cos(angle / 2.0), 0.0, 0.0, 0.0};
    const double s = std::sin(angle / 2.0);
    (axis == 0 ? q.x : axis == 1 ? q.y : q.z) = s;
    return q;
}

// The rotation ParticleSystem::apply_static_rotation applies: about Z, then Y, then X.
inline Quat quat_from_euler(double rad_x, double rad_y, double rad_z) noexcept
{
    return axis_rotation(rad_x, 0) * axis_rotation(rad_y, 1) * axis_rotation(rad_z, 2);
}

// The shortest rotation that turns the direction `from` into the direction `to` (both unit vectors).
inline Quat quat_from_to(Vec3 from, Vec3 to) noexcept
{
    const double c = from.x * to.x + from.y * to.y + from.z * to.z;
    Vec3 axis{from.y * to.z - from.z * to.y, from.z * to.x - from.x * to.z, from.x * to.y - from.y * to.x};
    const double s = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (s < 1e-12) {
        if (c > 0.0)
            return {};   // the same direction
        // Opposite directions: half a turn about any axis at right angles to `from`.
        const Vec3 side = std::abs(from.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        Vec3 perp{from.y * side.z - from.z * side.y, from.z * side.x - from.x * side.z, from.x * side.y - from.y * side.x};
        const double n = std::sqrt(perp.x * perp.x + perp.y * perp.y + perp.z * perp.z);
        return {0.0, perp.x / n, perp.y / n, perp.z / n};
    }
    const double angle = std::atan2(s, c);
    axis = axis * (std::sin(angle / 2.0) / s);
    return {std::cos(angle / 2.0), axis.x, axis.y, axis.z};
}

// The Euler angles (x, y, z) with apply_static_rotation(v, x, y, z) == rotate(q, v): the same rotation
// as a turn about Z, then Y, then X. At the singularity (y = +-90 degrees) the turn is all put in x.
inline Vec3 euler_from_quat(const Quat& q) noexcept
{
    // The matrix of the rotation; only the entries the angles come from.
    const double m01 = 2.0 * (q.x * q.y - q.w * q.z), m00 = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    const double m02 = 2.0 * (q.x * q.z + q.w * q.y);
    const double m12 = 2.0 * (q.y * q.z - q.w * q.x), m22 = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
    const double y = std::asin(m02 > 1.0 ? 1.0 : m02 < -1.0 ? -1.0 : m02);
    if (std::abs(m02) < 1.0 - 1e-12)
        return {std::atan2(-m12, m22), y, std::atan2(-m01, m00)};
    const double m11 = 1.0 - 2.0 * (q.x * q.x + q.z * q.z), m21 = 2.0 * (q.y * q.z + q.w * q.x);
    return {std::atan2(m21, m11), y, 0.0};
}

inline Vec3 rotate(const Quat& q, Vec3 v) noexcept
{
    // v' = v + 2w (u x v) + 2 u x (u x v), with u the vector part.
    const Vec3 u{q.x, q.y, q.z};
    const auto cross = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    const Vec3 t = cross(u, v) * 2.0;
    return v + t * q.w + cross(u, t);
}

// Shortest-path spherical interpolation, the way the engine blends two frames.
inline Quat slerp(const Quat& a, Quat b, double t) noexcept
{
    double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot < 0.0) {
        b = {-b.w, -b.x, -b.y, -b.z};
        dot = -dot;
    }
    double ka = 1.0 - t, kb = t;
    if (dot < 0.9995) {
        const double theta = std::acos(dot), sin_theta = std::sin(theta);
        ka = std::sin((1.0 - t) * theta) / sin_theta;
        kb = std::sin(t * theta) / sin_theta;
    }
    Quat q{ka * a.w + kb * b.w, ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z};
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

} // namespace gs
