#include <doctest/doctest.h>

#include <cmath>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

// Many particles with no jitter, so every velocity is exactly speed (plus burst) in the emitted direction.
Settings disk()
{
    Settings s;
    s.count = 126;
    s.geometry = Geometry::SingleQuad;
    s.spawn_radius = 100;
    s.spawn_height = 40;
    s.speed = 60;
    s.burst = 0;
    s.spread = 0;
    return s;
}

} // namespace

TEST_CASE("particles start on the disk, within its height, and leave horizontally the way they start")
{
    const ParticleSystem sys(disk());
    bool inside_edge = false;
    for (const Particle& p : sys.particles()) {
        const double r = std::hypot(p.spawn.x, p.spawn.y);
        CHECK(r <= 100.0 + 1e-9);
        CHECK(std::abs(p.spawn.z) <= 40.0 + 1e-9);
        inside_edge |= r < 90.0;
        CHECK(std::hypot(p.velocity.x, p.velocity.y) == doctest::Approx(60.0));
        CHECK(p.velocity.z == doctest::Approx(0.0));
        if (r > 1e-6)   // outward along the line it starts on
            CHECK((p.spawn.x * p.velocity.x + p.spawn.y * p.velocity.y) / (r * 60.0) == doctest::Approx(1.0));
    }
    CHECK(inside_edge);   // the disk is filled, not only its edge
}

TEST_CASE("a hollow ring starts every particle on the edge")
{
    Settings s = disk();
    s.hollow_ring = true;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles())
        CHECK(std::hypot(p.spawn.x, p.spawn.y) == doctest::Approx(100.0));
}

TEST_CASE("a narrow fan sends every particle around +X")
{
    Settings s = disk();
    s.emission_angle = 30;
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles())
        CHECK(std::abs(std::atan2(p.velocity.y, p.velocity.x)) <= 15.0 * kDegToRad + 1e-9);
}

TEST_CASE("the emitter tilt turns the disk")
{
    Settings s = disk();
    s.emission_angle = 1;
    s.burst = 50;
    s.emitter_tilt = {0, 0, 90};   // +X becomes +Y
    const ParticleSystem sys(s);
    for (const Particle& p : sys.particles()) {
        CHECK(p.velocity.y > 59.9);
        CHECK(std::abs(p.velocity.x) < 1.0);
        CHECK(p.velocity.z == doctest::Approx(50.0));
    }
}

TEST_CASE("the disk exports within the limits and is deterministic")
{
    Settings s;
    s.spawn_radius = 80;
    CHECK(analyze_export(ParticleSystem(s)).blockers.empty());
    const ParticleSystem a(s), b(s);
    for (std::size_t i = 0; i < a.particles().size(); ++i) {
        CHECK(a.particles()[i].spawn == b.particles()[i].spawn);
        CHECK(a.particles()[i].velocity == b.particles()[i].velocity);
    }
}

TEST_CASE("the emitter shape of earlier versions is ignored, with a warning unless it was the disk")
{
    nlohmann::json j = to_json(Settings{});
    CHECK_FALSE(j.contains("emitter_shape"));

    j["emitter_shape"] = "disk";
    const SettingsLoad quiet = settings_from_json(j, Settings{});
    CHECK(quiet.warnings.empty());
    CHECK(quiet.settings == Settings{});

    j["emitter_shape"] = "sphere";
    const SettingsLoad loud = settings_from_json(j, Settings{});
    REQUIRE(loud.warnings.size() == 1);
    CHECK(loud.warnings[0] == "emitter_shape \"sphere\": ParticleMDL emits from a disk only (ignored)");
    CHECK(loud.settings == Settings{});
}
