#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Exporter.hpp"
#include "SeedRoll.hpp"

using namespace gs;

namespace {

// An effect that uses every value either seed varies.
Settings all_in_use()
{
    Settings s;
    s.geometry = Geometry::SingleQuad;   // the cheapest to export: the rolls export many times
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

struct Range {
    double lo, hi;
};

// The slider ranges of the values in system_values() and particle_values(), in the same order.
std::vector<Range> system_ranges(const Settings& s)
{
    return {{1, double(max_particles(s.geometry, s.scale_mode))}, {0.5, 10}, {0.1, 1}, {0, 0.9}, {0, 1}, {1, 1024}, {1, 1024},
            {1, 1024}, {-500, 500}, {-500, 500}, {-500, 500}, {-360, 360}, {-360, 360}, {-360, 360}, {1, 360},
            {0, 500}, {0, 400}, {-180, 180}, {-180, 180}, {-180, 180}, {-200, 400}, {-300, 300}, {0, 150},
            {-600, 200}, {0, 5}, {0, 0.95}, {-300, 300}, {0, 200}, {-720, 720}, {-300, 300}};
}

std::vector<Range> particle_ranges(const Settings&)
{
    return {{0.1, 5}, {0, 5}, {1, 100}, {1, 100}, {0, 0.9}, {1, 16}, {0, 200}, {-180, 180}, {-180, 180}, {-180, 180},
            {-360, 360}, {-360, 360}, {-360, 360}, {0, 360}};
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

TEST_CASE("a seed draws every value of its side anywhere in its range, and the same seed the same")
{
    const Settings base = all_in_use();
    for (const SeedSide side : {SeedSide::System, SeedSide::Particle}) {
        CAPTURE(side == SeedSide::System);
        auto values = side == SeedSide::System ? system_values : particle_values;
        const std::vector<Range> ranges = side == SeedSide::System ? system_ranges(base) : particle_ranges(base);
        Settings a = base, b = base;
        roll_settings(a, base, side, 7);
        roll_settings(b, base, side, 7);
        CHECK(a == b);
        CHECK((side == SeedSide::System ? a.system_seed : a.particle_seed) == 7);

        // Over many seeds each value lands all over its range, not near where it was.
        std::vector<double> low(ranges.size(), 1e300), high(ranges.size(), -1e300);
        for (int seed = 1; seed <= 100; ++seed) {
            Settings s = base;
            roll_settings(s, base, side, seed);
            const std::vector<double> v = values(s);
            for (std::size_t i = 0; i < v.size(); ++i) {
                CHECK(v[i] >= ranges[i].lo);
                CHECK(v[i] <= ranges[i].hi);
                low[i] = std::min(low[i], v[i]);
                high[i] = std::max(high[i], v[i]);
            }
        }
        const std::size_t fitted = side == SeedSide::System ? 0 : 5;   // the count, the cluster: see below
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            CAPTURE(i);
            if (i == fitted)
                continue;
            const double span = ranges[i].hi - ranges[i].lo;
            CHECK(low[i] <= ranges[i].lo + 0.15 * span);
            CHECK(high[i] >= ranges[i].hi - 0.15 * span);
        }
    }
}

TEST_CASE("what a seed draws does not depend on the values before")
{
    const Settings on = all_in_use();
    for (const SeedSide side : {SeedSide::System, SeedSide::Particle}) {
        Settings off = on;
        roll_settings(off, Settings{}, side, 0);   // this side's values from the defaults, much of them 0
        REQUIRE_FALSE(same_rolled_values(off, on, side));
        Settings from_off = off, from_on = on;
        roll_settings(from_off, off, side, 21);
        roll_settings(from_on, on, side, 21);
        CHECK(same_rolled_values(from_off, from_on, side));
    }
}

TEST_CASE("a feature that 0 turns off is on with some seeds and off with others")
{
    const Settings base = all_in_use();
    int drag_on = 0, bounce_on = 0, spin_on = 0, cluster_on = 0, quads_differ = 0;
    for (int seed = 1; seed <= 60; ++seed) {
        Settings s = base;
        roll_settings(s, base, SeedSide::System, seed);
        roll_settings(s, base, SeedSide::Particle, seed);
        drag_on += s.drag != 0.0;
        bounce_on += s.bounce != 0.0;
        spin_on += s.particle_spin.z != 0.0;
        cluster_on += s.cluster_size > 1;
        quads_differ += s.quad_width != base.quad_width;   // not optional: always drawn
    }
    for (const int on : {drag_on, bounce_on, spin_on, cluster_on}) {
        CHECK(on > 15);
        CHECK(on < 45);
    }
    CHECK(quads_differ == 60);
}

TEST_CASE("a seed never makes an effect studiomdl would refuse")
{
    // Large effects: many particles, long and fast, clustered boxes with vertex bones off.
    Settings big = all_in_use();
    big.geometry = Geometry::VolumetricBox;
    big.fps = 60;
    for (int seed = 1; seed <= 12; ++seed) {
        CAPTURE(seed);
        for (const SeedSide side : {SeedSide::System, SeedSide::Particle}) {
            Settings s = big;
            roll_settings(s, big, side, seed);
            validate(s);
            const ExportReport r = analyze_export(ParticleSystem(s));
            CHECK(r.blockers.empty());
            if (side == SeedSide::Particle)
                CHECK(s.count <= big.count);   // lowered if it must be, never raised
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

TEST_CASE("rolled values stay inside the slider ranges and the count inside the bones")
{
    const Settings base = all_in_use();
    for (int seed = 1; seed <= 30; ++seed) {
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
