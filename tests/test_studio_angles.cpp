#include <doctest/doctest.h>

#include <cmath>

#include "Exporter.hpp"

using namespace gs;

namespace {

// Rotations about one axis, counter-clockwise.
Vec3 rot_x(Vec3 v, double a) { return {v.x, v.y * std::cos(a) - v.z * std::sin(a), v.y * std::sin(a) + v.z * std::cos(a)}; }
Vec3 rot_y(Vec3 v, double a) { return {v.x * std::cos(a) + v.z * std::sin(a), v.y, -v.x * std::sin(a) + v.z * std::cos(a)}; }
Vec3 rot_z(Vec3 v, double a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z}; }

// How the engine turns SMD angles into a rotation: Rz(z) * Ry(y) * Rx(x).
Vec3 engine_rotation(Vec3 a, Vec3 v) { return rot_z(rot_y(rot_x(v, a.x), a.y), a.z); }

// A bone's rotation as it ends up in game: the QC sets studiomdl's root turn to 0, so it is the
// engine's reading of the angles.
Vec3 in_game(Vec3 a, Vec3 v) { return engine_rotation(a, v); }

void check_near(Vec3 a, Vec3 b, double eps = 1e-9)
{
    CHECK(a.x == doctest::Approx(b.x).epsilon(eps).scale(1.0));
    CHECK(a.y == doctest::Approx(b.y).epsilon(eps).scale(1.0));
    CHECK(a.z == doctest::Approx(b.z).epsilon(eps).scale(1.0));
}

// The particle bone's intended rotation (ParticleSystem::apply_static_rotation)
// must be what the game shows after studio_angles().
void check_round_trip(Vec3 r)
{
    CAPTURE(r.x);
    CAPTURE(r.y);
    CAPTURE(r.z);
    const Vec3 a = studio_angles(r);
    for (Vec3 v : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{0.3, -0.7, 0.2}})
        check_near(in_game(a, v), ParticleSystem::apply_static_rotation(v, r.x, r.y, r.z));
}

} // namespace

TEST_CASE("studio_angles of no rotation is no rotation")
{
    const Vec3 a = studio_angles({});
    check_near(a, {});
}

TEST_CASE("studio_angles of a rotation about Z stays a rotation about Z")
{
    const Vec3 a = studio_angles({0, 0, radians(30)});
    check_near(a, {0, 0, radians(30)});
}

TEST_CASE("studio_angles keeps a rotation about X about X")
{
    const Vec3 a = studio_angles({0.3, 0, 0});
    check_near(a, {0.3, 0, 0});
}

TEST_CASE("studio_angles round trips single-axis rotations")
{
    for (double deg : {-170.0, -90.0, -45.0, -10.0, 10.0, 45.0, 90.0, 135.0, 179.0}) {
        check_round_trip({radians(deg), 0, 0});
        check_round_trip({0, radians(deg), 0});
        check_round_trip({0, 0, radians(deg)});
    }
}

TEST_CASE("studio_angles round trips general rotations")
{
    // A fixed sweep, so a failure is reproducible.
    std::uint32_t state = 12345;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return (static_cast<double>(state >> 8) / (1 << 24)) * 2 * kPi;
    };
    for (int i = 0; i < 500; ++i)
        check_round_trip({next(), next(), next()});
}

TEST_CASE("studio_angles round trips at gimbal lock")
{
    // R = Ry(+-90): the engine's middle angle reaches +-90 degrees.
    for (double sign : {1.0, -1.0}) {
        const Vec3 r{0, sign * kPi / 2, 0};
        check_round_trip(r);
        check_round_trip({radians(25), r.y, 0});
        check_round_trip({radians(-140), r.y, radians(60)});

        const Vec3 a = studio_angles(r);
        CHECK(std::abs(a.y) == doctest::Approx(kPi / 2));
        CHECK(a.z == doctest::Approx(0.0));   // all the freedom goes into x
    }
}

TEST_CASE("studio_angles stays finite and inside range")
{
    for (double ax = -6.0; ax <= 6.0; ax += 0.75)
        for (double ay = -6.0; ay <= 6.0; ay += 0.75)
            for (double az = -6.0; az <= 6.0; az += 0.75) {
                const Vec3 a = studio_angles({ax, ay, az});
                CHECK(std::isfinite(a.x));
                CHECK(std::isfinite(a.y));
                CHECK(std::isfinite(a.z));
                CHECK(std::abs(a.y) <= kPi / 2 + 1e-12);
            }
}
