#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "SeedRoll.hpp"

using namespace gs;

namespace {

// An effect that uses every value either seed varies.
Settings all_in_use()
{
    Settings s;
    s.count = 40;
    s.duration = 3.0;
    s.lifespan = 0.7;
    s.lifespan_variance = 0.3;
    s.loop = false;
    s.burst_window = 0.4;
    s.volume_width = 200;
    s.volume_depth = 150;
    s.volume_height = 300;
    s.velocity = {40, -30, 20};
    s.spin = {10, 20, -30};
    s.emission_angle = 180;
    s.spawn_radius = 50;
    s.spawn_height = 30;
    s.emitter_tilt = {15, -20, 25};
    s.speed = 80;
    s.burst = 100;
    s.spread = 40;
    s.gravity = -200;
    s.drag = 1.0;
    s.bounce = 0.5;
    s.floor_height = -40;
    s.turbulence = 30;
    s.vortex_spin = 90;
    s.vortex_pull = 40;
    s.scale_start = 1.5;
    s.scale_end = 0.5;
    s.quad_width = 10;
    s.quad_height = 12;
    s.size_variance = 0.4;
    s.cluster_size = 4;
    s.cluster_radius = 30;
    s.mesh_rotation = {30, 40, 50};
    s.particle_spin = {60, -70, 80};
    s.spin_spread = 45;
    return s;
}

std::vector<double> system_values(const Settings& s)
{
    return {double(s.count), s.duration, s.lifespan, s.lifespan_variance, s.burst_window, s.volume_width, s.volume_depth,
            s.volume_height, s.velocity.x, s.velocity.y, s.velocity.z, s.spin.x, s.spin.y, s.spin.z, s.emission_angle,
            s.spawn_radius, s.spawn_height, s.emitter_tilt.x, s.emitter_tilt.y, s.emitter_tilt.z, s.speed, s.burst,
            s.spread, s.gravity, s.drag, s.bounce, s.floor_height, s.turbulence, s.vortex_spin, s.vortex_pull};
}

std::vector<double> particle_values(const Settings& s)
{
    return {s.scale_start, s.scale_end, s.quad_width, s.quad_height, s.size_variance, double(s.cluster_size), s.cluster_radius,
            s.mesh_rotation.x, s.mesh_rotation.y, s.mesh_rotation.z, s.particle_spin.x, s.particle_spin.y,
            s.particle_spin.z, s.spin_spread};
}

} // namespace

TEST_CASE("seed 0 leaves the values as they are and sets the seed")
{
    const Settings base = all_in_use();
    for (const SeedSide side : {SeedSide::System, SeedSide::Particle}) {
        Settings s = base;
        roll_settings(s, base, side, 0);
        CHECK(s == base);
    }
    Settings s = base;
    roll_settings(s, base, SeedSide::Particle, 9);
    roll_settings(s, base, SeedSide::Particle, 0);
    CHECK(s == base);   // back to the values before the roll
}

TEST_CASE("a seed varies every value of its side that is in use, and the same seed in the same way")
{
    const Settings base = all_in_use();
    for (const SeedSide side : {SeedSide::System, SeedSide::Particle}) {
        CAPTURE(side == SeedSide::System);
        auto values = side == SeedSide::System ? system_values : particle_values;
        Settings a = base, b = base, c = base;
        roll_settings(a, base, side, 7);
        roll_settings(b, base, side, 7);
        roll_settings(c, base, side, 8);
        CHECK(a == b);
        CHECK((side == SeedSide::System ? a.system_seed : a.particle_seed) == 7);
        const std::vector<double> before = values(base), seven = values(a), eight = values(c);
        const std::size_t whole_number = side == SeedSide::System ? 0 : 5;   // the count, the cluster size
        for (std::size_t i = 0; i < before.size(); ++i) {
            CAPTURE(i);
            if (i != whole_number) {   // a whole number may round to the same one
                CHECK(seven[i] != before[i]);
                CHECK(seven[i] != eight[i]);
            }
            // By at most a quarter of itself (a whole unit for a count).
            CHECK(std::abs(seven[i] - before[i]) <= kRollAmount * std::abs(before[i]) + 0.5);
        }
    }
}

TEST_CASE("a seed changes nothing but its own side's values")
{
    Settings base = all_in_use();
    base.texture.hardness = 2.0;
    base.fps = 24;
    for (const SeedSide side : {SeedSide::System, SeedSide::Particle}) {
        Settings s = base;
        roll_settings(s, base, side, 11);
        const SeedSide other = side == SeedSide::System ? SeedSide::Particle : SeedSide::System;
        CHECK(same_rolled_values(s, base, other));
        CHECK_FALSE(same_rolled_values(s, base, side));
        // Undo this side's values and seed: what is left is untouched (choices, fps, texture, the rest).
        roll_settings(s, base, side, 0);
        CHECK(s == base);
    }
}

TEST_CASE("values that are off, and a full fan or an endless life, stay as they are")
{
    Settings base;   // much of it at 0: no velocity, spin, tilt, drag, bounce, turbulence, vortex
    base.emission_angle = 360;
    base.lifespan = 1.0;
    Settings s = base;
    roll_settings(s, base, SeedSide::System, 5);
    CHECK(s.velocity == Vec3{});
    CHECK(s.spin == Vec3{});
    CHECK(s.emitter_tilt == Vec3{});
    CHECK(s.drag == 0.0);
    CHECK(s.bounce == 0.0);
    CHECK(s.turbulence == 0.0);
    CHECK(s.vortex_spin == 0.0);
    CHECK(s.emission_angle == 360.0);
    CHECK(s.lifespan == 1.0);
    CHECK(s.speed != base.speed);   // what is in use still varies

    Settings p = base;
    roll_settings(p, base, SeedSide::Particle, 5);
    CHECK(p.cluster_size == 1);   // no cluster stays no cluster
    CHECK(p.particle_spin == Vec3{});
    CHECK(p.quad_width != base.quad_width);
}

TEST_CASE("rolled values stay inside the slider ranges and the count inside the bones")
{
    Settings base = all_in_use();
    base.count = 126;
    base.lifespan = 0.95;
    base.velocity.x = 480;
    for (int seed = 1; seed <= 200; ++seed) {
        Settings s = base;
        roll_settings(s, base, SeedSide::System, seed);
        CHECK(s.count >= 1);
        CHECK(s.count <= max_particles(s.geometry, s.scale_mode));
        CHECK(s.lifespan <= 1.0);
        CHECK(s.velocity.x <= 500.0);
        Settings checked = s;
        CHECK_FALSE(validate(checked));   // already valid
    }
}

TEST_CASE("the values a seed varies come from the base, not from what the side holds now")
{
    const Settings base = all_in_use();
    Settings once = base, twice = base;
    roll_settings(once, base, SeedSide::System, 4);
    roll_settings(twice, base, SeedSide::System, 9);
    roll_settings(twice, base, SeedSide::System, 4);
    CHECK(once == twice);   // no drift from going through another seed
}
