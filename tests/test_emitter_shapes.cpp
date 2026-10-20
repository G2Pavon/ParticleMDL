#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

double length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// 126 particles with no jitter, so every velocity is exactly speed (plus burst) in the emitted direction.
Settings emitter(EmitterShape shape)
{
    Settings s;
    s.count = 126;
    s.geometry = Geometry::SingleQuad;
    s.emitter_shape = shape;
    s.spawn_radius = 100;
    s.spawn_height = 40;
    s.speed = 60;
    s.burst = 0;
    s.spread = 0;
    return s;
}

} // namespace

TEST_CASE("a sphere starts particles inside a ball and sends them straight outward")
{
    Settings s = emitter(EmitterShape::Sphere);
    const ParticleSystem sys(s);
    REQUIRE(sys.particles().size() == 126);

    int inner = 0, below = 0;
    for (const Particle& p : sys.particles()) {
        const double r = length(p.spawn);
        CHECK(r <= 100.0 + 1e-9);
        CHECK(length(p.velocity) == doctest::Approx(60.0));
        if (r > 1e-6)
            CHECK(length(cross(p.spawn, p.velocity)) / (r * 60.0) < 1e-9);   // parallel: outward
        if (r < 50)
            ++inner;
        if (p.spawn.z < 0)
            ++below;
    }
    // Uniform in volume: an eighth of the particles are inside half the radius; both hemispheres are used.
    CHECK(inner > 126 * 0.03);
    CHECK(inner < 126 * 0.25);
    CHECK(below > 30);
    CHECK(below < 96);
}

TEST_CASE("a hollow sphere starts particles on its surface")
{
    Settings s = emitter(EmitterShape::Sphere);
    s.hollow_ring = true;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles())
        CHECK(length(p.spawn) == doctest::Approx(100.0));
}

TEST_CASE("the burst adds to the outward speed along Z")
{
    Settings s = emitter(EmitterShape::Sphere);
    s.hollow_ring = true;
    s.burst = 25;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        const Vec3 outward = p.spawn * (1.0 / 100.0);
        const Vec3 expected = outward * 60.0 + Vec3{0, 0, 25};
        CHECK(p.velocity.x == doctest::Approx(expected.x));
        CHECK(p.velocity.y == doctest::Approx(expected.y));
        CHECK(p.velocity.z == doctest::Approx(expected.z));
    }
}

TEST_CASE("a hemisphere stays above the equator")
{
    Settings s = emitter(EmitterShape::Hemisphere);
    int high = 0;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        CHECK(p.spawn.z >= -1e-9);
        CHECK(p.velocity.z >= -1e-9);
        CHECK(length(p.spawn) <= 100.0 + 1e-9);
        if (p.velocity.z > 30)
            ++high;
    }
    CHECK(high > 20);
}

TEST_CASE("a cone sends particles inside its opening and none outside")
{
    Settings s = emitter(EmitterShape::Cone);
    s.emission_angle = 60;
    s.spawn_radius = 0;
    double widest = 0;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        const double angle = std::acos(std::clamp(p.velocity.z / length(p.velocity), -1.0, 1.0)) * 180.0 / kPi;
        CHECK(angle <= 30.0 + 1e-6);
        CHECK(length(p.velocity) == doctest::Approx(60.0));
        CHECK(length(p.spawn) == 0.0);
        widest = std::max(widest, angle);
    }
    CHECK(widest > 20.0);   // the whole opening is used, not just its axis
}

TEST_CASE("a cone of 360 degrees covers every direction")
{
    Settings s = emitter(EmitterShape::Cone);
    s.emission_angle = 360;
    int down = 0, up = 0;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        (p.velocity.z < 0 ? down : up)++;
        CHECK(length(p.velocity) == doctest::Approx(60.0));
    }
    CHECK(down > 30);
    CHECK(up > 30);
}

TEST_CASE("a narrow cone points along its axis")
{
    Settings s = emitter(EmitterShape::Cone);
    s.emission_angle = 1;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles())
        CHECK(p.velocity.z > 59.99);
}

TEST_CASE("a box starts particles inside its extents and sends them away from the middle")
{
    Settings s = emitter(EmitterShape::Box);
    s.spawn_radius = 100;
    s.spawn_height = 40;
    double widest = 0;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        CHECK(std::abs(p.spawn.x) <= 100.0 + 1e-9);
        CHECK(std::abs(p.spawn.y) <= 100.0 + 1e-9);
        CHECK(std::abs(p.spawn.z) <= 40.0 + 1e-9);
        CHECK(std::hypot(p.velocity.x, p.velocity.y) == doctest::Approx(60.0));
        CHECK(p.velocity.x * p.spawn.x + p.velocity.y * p.spawn.y >= 0.0);                    // away
        CHECK(std::abs(p.velocity.x * p.spawn.y - p.velocity.y * p.spawn.x) < 1e-6 * 100.0);  // along the radius
        widest = std::max(widest, std::abs(p.spawn.x));
    }
    CHECK(widest > 80.0);
}

TEST_CASE("a line starts particles along X and fans them out around Y")
{
    Settings s = emitter(EmitterShape::Line);
    s.emission_angle = 90;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        CHECK(std::abs(p.spawn.x) <= 100.0 + 1e-9);
        CHECK(p.spawn.y == 0.0);
        CHECK(p.spawn.z == 0.0);
        CHECK(std::hypot(p.velocity.x, p.velocity.y) == doctest::Approx(60.0));
        CHECK(p.velocity.y > 0);
        CHECK(std::abs(std::atan2(p.velocity.x, p.velocity.y)) * 180.0 / kPi <= 45.0 + 1e-6);
    }
}

TEST_CASE("the emitter tilt turns every shape")
{
    Settings s = emitter(EmitterShape::Cone);
    s.emission_angle = 1;
    s.emitter_tilt = {90, 0, 0};   // +Z becomes -Y
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        CHECK(p.velocity.y < -59.9);
        CHECK(std::abs(p.velocity.z) < 1.0);
    }
}

TEST_CASE("every shape exports within the limits and is deterministic")
{
    for (const EmitterShape shape : {EmitterShape::Disk, EmitterShape::Sphere, EmitterShape::Hemisphere, EmitterShape::Cone,
                                     EmitterShape::Box, EmitterShape::Line}) {
        CAPTURE(enum_id(shape));
        Settings s;
        s.emitter_shape = shape;
        s.spawn_radius = 80;
        const ExportReport report = analyze_export(ParticleSystem(s));
        CHECK(report.blockers.empty());

        const ParticleSystem a(s), b(s);
        for (std::size_t i = 0; i < a.particles().size(); ++i) {
            CHECK(a.particles()[i].spawn == b.particles()[i].spawn);
            CHECK(a.particles()[i].velocity == b.particles()[i].velocity);
        }
    }
}

TEST_CASE("the disk emitter is the default and the shape is saved by name")
{
    CHECK(Settings{}.emitter_shape == EmitterShape::Disk);

    Settings s;
    s.emitter_shape = EmitterShape::Hemisphere;
    const nlohmann::json j = to_json(s);
    CHECK(j.at("emitter_shape") == "hemisphere");
    CHECK(settings_from_json(j, Settings{}).settings.emitter_shape == EmitterShape::Hemisphere);

    // A file written before shapes existed has no such key: it is a disk.
    nlohmann::json old = j;
    old.erase("emitter_shape");
    CHECK(settings_from_json(old, Settings{}).settings.emitter_shape == EmitterShape::Disk);

    const SettingsLoad bad = settings_from_json(nlohmann::json{{"emitter_shape", "torus"}}, Settings{});
    CHECK(bad.settings.emitter_shape == EmitterShape::Disk);
    CHECK(bad.warnings.size() == 1);

    Settings invalid;
    invalid.emitter_shape = static_cast<EmitterShape>(77);
    CHECK(validate(invalid));
    CHECK(invalid.emitter_shape == EmitterShape::Disk);
}
