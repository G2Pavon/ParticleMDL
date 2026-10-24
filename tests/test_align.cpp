#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

double length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 unit(Vec3 v)
{
    return v * (1.0 / length(v));
}

double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// The direction a particle is shown moving in, by differences of the shown position.
Vec3 shown_direction(const ParticleSystem& sys, const Particle& p, double t)
{
    const double h = 1e-5;
    return unit(sys.position_at(p, t + h) - sys.position_at(p, t - h));
}

struct Rng {
    std::uint32_t state = 12345;
    double unit() { return (state = state * 1664525u + 1013904223u) / 4294967296.0; }
};

Settings aligned()
{
    Settings s;
    s.count = 12;
    s.geometry = Geometry::SingleQuad;
    s.align_to_velocity = true;
    s.turbulence = 0;
    s.speed = 80;
    s.burst = 60;
    s.spread = 40;
    s.gravity = 0;
    s.spawn_radius = 20;
    return s;
}

} // namespace

TEST_CASE("quat_from_to turns one direction into another")
{
    Rng rng;
    for (int i = 0; i < 300; ++i) {
        const Vec3 a = unit({rng.unit() - 0.5, rng.unit() - 0.5, rng.unit() - 0.5});
        const Vec3 b = unit({rng.unit() - 0.5, rng.unit() - 0.5, rng.unit() - 0.5});
        const Vec3 got = rotate(quat_from_to(a, b), a);
        CHECK(got.x == doctest::Approx(b.x).epsilon(1e-9).scale(1.0));
        CHECK(got.y == doctest::Approx(b.y).epsilon(1e-9).scale(1.0));
        CHECK(got.z == doctest::Approx(b.z).epsilon(1e-9).scale(1.0));
    }
}

TEST_CASE("quat_from_to handles the same direction and the opposite one")
{
    const Vec3 z{0, 0, 1};
    CHECK(quat_from_to(z, z) == Quat{});
    for (const Vec3 from : {Vec3{0, 0, 1}, Vec3{1, 0, 0}, Vec3{0, -1, 0}, Vec3{0.6, 0.0, 0.8}}) {
        const Vec3 back = rotate(quat_from_to(from, from * -1.0), from);
        CHECK(back.x == doctest::Approx(-from.x).scale(1.0));
        CHECK(back.y == doctest::Approx(-from.y).scale(1.0));
        CHECK(back.z == doctest::Approx(-from.z).scale(1.0));
    }
}

TEST_CASE("euler_from_quat gives angles that apply_static_rotation turns into the same rotation")
{
    Rng rng;
    for (int i = 0; i < 2000; ++i) {
        const double a = rng.unit() * 2 * kPi, b = rng.unit() * 2 * kPi, c = rng.unit() * 2 * kPi;
        const Quat q = quat_from_euler(a, b, c);
        const Vec3 e = euler_from_quat(q);
        for (const Vec3 v : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{0.3, -0.8, 0.5}}) {
            const Vec3 want = rotate(q, v), got = ParticleSystem::apply_static_rotation(v, e.x, e.y, e.z);
            CHECK(got.x == doctest::Approx(want.x).epsilon(1e-9).scale(1.0));
            CHECK(got.y == doctest::Approx(want.y).epsilon(1e-9).scale(1.0));
            CHECK(got.z == doctest::Approx(want.z).epsilon(1e-9).scale(1.0));
        }
    }
}

TEST_CASE("euler_from_quat copes with the singularity at y = +-90 degrees")
{
    for (const double y : {kPi / 2, -kPi / 2}) {
        for (const double other : {0.0, 0.7, 2.5}) {
            const Quat q = quat_from_euler(other, y, 0.3);
            const Vec3 e = euler_from_quat(q);
            CHECK(std::isfinite(e.x));
            CHECK(std::isfinite(e.z));
            const Vec3 want = rotate(q, {0.2, 0.5, -0.4}), got = ParticleSystem::apply_static_rotation({0.2, 0.5, -0.4}, e.x, e.y, e.z);
            CHECK(got.x == doctest::Approx(want.x).scale(1.0));
            CHECK(got.y == doctest::Approx(want.y).scale(1.0));
            CHECK(got.z == doctest::Approx(want.z).scale(1.0));
        }
    }
}

TEST_CASE("an aligned quad has its Z axis along a straight path")
{
    const ParticleSystem sys(aligned());
    for (const Particle& p : sys.particles())
        for (const double t : {0.3, 1.0, 2.2}) {
            const Vec3 r = sys.rotation_at(p, t);
            const Vec3 axis = ParticleSystem::apply_static_rotation({0, 0, 1}, r.x, r.y, r.z);
            const Vec3 want = shown_direction(sys, p, t);
            CHECK(axis.x == doctest::Approx(want.x).epsilon(1e-6).scale(1.0));
            CHECK(axis.y == doctest::Approx(want.y).epsilon(1e-6).scale(1.0));
            CHECK(axis.z == doctest::Approx(want.z).epsilon(1e-6).scale(1.0));
        }
}

TEST_CASE("an aligned quad follows the bend of the path under gravity")
{
    Settings s = aligned();
    s.gravity = -300;
    s.burst = 150;
    const ParticleSystem sys(s);
    const Particle& p = sys.particles().front();
    double previous_z = 2;
    bool turned = false;
    for (const double t : {0.05, 0.4, 0.8, 1.2, 1.6, 2.0}) {
        const Vec3 r = sys.rotation_at(p, t);
        const Vec3 axis = ParticleSystem::apply_static_rotation({0, 0, 1}, r.x, r.y, r.z);
        const Vec3 want = shown_direction(sys, p, t);
        CHECK(dot(axis, want) > 0.999999);
        if (axis.z < previous_z - 0.05)
            turned = true;
        previous_z = axis.z;
    }
    CHECK(turned);   // it points up at first and down at the end
}

TEST_CASE("spin Z rolls an aligned quad around its direction, spin X and Y do nothing")
{
    Settings s = aligned();
    s.particle_spin = {200, 300, 90};
    const ParticleSystem sys(s);
    const Particle& p = sys.particles().front();

    const Vec3 r0 = sys.rotation_at(p, 0.4), r1 = sys.rotation_at(p, 1.1);
    const Vec3 axis0 = ParticleSystem::apply_static_rotation({0, 0, 1}, r0.x, r0.y, r0.z);
    const Vec3 axis1 = ParticleSystem::apply_static_rotation({0, 0, 1}, r1.x, r1.y, r1.z);
    CHECK(dot(axis0, shown_direction(sys, p, 0.4)) > 0.999999);
    CHECK(dot(axis1, shown_direction(sys, p, 1.1)) > 0.999999);

    // The quad's own X axis turns around the direction between the two times.
    const Vec3 x0 = ParticleSystem::apply_static_rotation({1, 0, 0}, r0.x, r0.y, r0.z);
    const Vec3 x1 = ParticleSystem::apply_static_rotation({1, 0, 0}, r1.x, r1.y, r1.z);
    CHECK(length(x0 - x1) > 0.1);

    // Without the X and Y spin it is the same.
    Settings only_z = s;
    only_z.particle_spin = {0, 0, 90};
    const ParticleSystem other(only_z);
    const Vec3 q = other.rotation_at(other.particles().front(), 0.4);
    CHECK(q.x == doctest::Approx(r0.x));
    CHECK(q.y == doctest::Approx(r0.y));
    CHECK(q.z == doctest::Approx(r0.z));
}

TEST_CASE("a particle that does not move keeps a quad that is not turned")
{
    Settings s = aligned();
    s.speed = 0;
    s.burst = 0;
    s.spread = 0;
    s.spawn_radius = 0;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        const Vec3 r = sys.rotation_at(p, 0.7);
        CHECK(std::isfinite(r.x));
        CHECK(std::isfinite(r.y));
        CHECK(std::isfinite(r.z));
        CHECK(r.x == doctest::Approx(0.0).scale(1.0));
        CHECK(r.y == doctest::Approx(0.0).scale(1.0));
        CHECK(r.z == doctest::Approx(0.0).scale(1.0));
    }
}

TEST_CASE("aligned effects export within the limits and with no jump drawn")
{
    Settings s = aligned();
    s.gravity = -200;
    s.scale_mode = ScaleMode::VertexBone;
    s.count = 25;
    s.quad_width = 2;
    s.quad_height = 30;
    const ParticleSystem sys(s);
    CHECK(analyze_export(sys).blockers.empty());

    // The exporter turns the bone with angles that studiomdl reads back as the same rotation.
    const BakedPoses baked = bake(sys);
    const AnimationTable table = animation_table(sys, baked);
    CHECK(table.bones == 25 * 5);
    for (int f = 0; f < baked.steps(); ++f)
        for (int i = 0; i < baked.particles(); ++i)
            for (const double value : table.at(f, i * 5))
                CHECK(std::isfinite(value));
}

TEST_CASE("the setting is saved and absent from old files")
{
    Settings s;
    s.align_to_velocity = true;
    CHECK(settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{}).settings.align_to_velocity);
    nlohmann::json old = to_json(Settings{});
    old.erase("align_to_velocity");
    CHECK_FALSE(settings_from_json(old, Settings{}).settings.align_to_velocity);
}
