#include <doctest/doctest.h>

#include <cmath>

#include <nlohmann/json.hpp>

#include "SeedRoll.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

bool all_whole(const Settings& s)
{
    bool whole = true;
    for_each_whole_value(s, [&](const double& v) { whole = whole && v == std::round(v); });
    return whole;
}

} // namespace

TEST_CASE("validate rounds the values that take whole numbers and leaves the fractions")
{
    Settings s;
    s.speed = 60.4;
    s.gravity = -150.6;
    s.velocity = {1.5, -2.5, 0.2};
    s.quad_width = 6.7;
    s.particle_spin.z = 89.9;
    s.lifespan = 0.85;
    s.duration = 2.37;
    s.drag = 0.45;
    s.texture.hardness = 0.8;
    CHECK(validate(s));
    CHECK(s.speed == 60);
    CHECK(s.gravity == -151);
    CHECK(s.velocity == Vec3{2, -3, 0});   // halves away from zero
    CHECK(s.quad_width == 7);
    CHECK(s.particle_spin.z == 90);
    CHECK(s.lifespan == 0.85);
    CHECK(s.duration == 2.37);
    CHECK(s.drag == 0.45);
    CHECK(s.texture.hardness == 0.8);
    CHECK(all_whole(s));
    CHECK_FALSE(validate(s));   // whole already
}

TEST_CASE("a settings file with fractions where whole numbers go is rounded without a warning")
{
    nlohmann::json j = to_json(Settings{});
    j["speed"] = 72.5;
    j["emission_angle"] = 90.2;
    j["mesh_rotation"] = {10.4, 0, -20.6};
    j["lifespan"] = 0.66;
    const SettingsLoad loaded = settings_from_json(j, Settings{});
    CHECK(loaded.warnings.empty());
    CHECK(loaded.settings.speed == 73);
    CHECK(loaded.settings.emission_angle == 90);
    CHECK(loaded.settings.mesh_rotation == Vec3{10, 0, -21});
    CHECK(loaded.settings.lifespan == 0.66);
}

TEST_CASE("the seeds draw whole numbers where whole numbers go")
{
    const Settings base;
    for (int seed = 1; seed <= 25; ++seed) {
        Settings s = base;
        roll_settings(s, base, SeedSide::System, seed);
        roll_settings(s, base, SeedSide::Particle, seed);
        CHECK(all_whole(s));
    }
}
