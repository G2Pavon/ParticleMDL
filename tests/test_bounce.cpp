#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

// One particle dropped from rest at height 0 onto a floor 100 below, with gravity 200: it arrives after
// exactly one second at a speed of 200.
Settings drop(double bounce)
{
    Settings s;
    s.count = 1;
    s.geometry = Geometry::SingleQuad;
    s.duration = 10.0;
    s.fps = 10;
    s.lifespan = 1.0;
    s.end_behavior = EndBehavior::EndlessLoop;
    s.speed = 0;
    s.burst = 0;
    s.spread = 0;
    s.spawn_radius = 0;
    s.spawn_height = 0;
    s.turbulence = 0;
    s.gravity = -200;
    s.bounce = bounce;
    s.floor_height = -100;
    return s;
}

// The height of the particle in the units of the motion (the system scales it to the volume).
double height(const ParticleSystem& sys, double t)
{
    return sys.position_at(sys.particles().front(), t).z / sys.volume_scale().z;
}

} // namespace

TEST_CASE("without bounce a particle falls through the floor")
{
    const ParticleSystem sys(drop(0.0));
    CHECK(height(sys, 0.5) == doctest::Approx(-25.0));
    CHECK(height(sys, 1.0) == doctest::Approx(-100.0));
    CHECK(height(sys, 2.0) == doctest::Approx(-400.0));
}

TEST_CASE("a particle bounces back up and loses height with every bounce")
{
    const ParticleSystem sys(drop(0.5));
    CHECK(height(sys, 0.5) == doctest::Approx(-25.0));     // before the first hit nothing differs
    CHECK(height(sys, 1.0) == doctest::Approx(-100.0).epsilon(1e-6));   // the hit
    // It leaves at 0.5 * 200 = 100: up for half a second to a height of 100^2 / (2 * 200) = 25 above the floor.
    CHECK(height(sys, 1.25) == doctest::Approx(-100.0 + 100 * 0.25 - 100 * 0.25 * 0.25).epsilon(1e-6));
    CHECK(height(sys, 1.5) == doctest::Approx(-75.0).epsilon(1e-6));
    CHECK(height(sys, 2.0) == doctest::Approx(-100.0).epsilon(1e-6));   // the second hit, one second after the first
    // Then 50 up: the apex a quarter of a second later, 6.25 above the floor.
    CHECK(height(sys, 2.25) == doctest::Approx(-93.75).epsilon(1e-6));
}

TEST_CASE("each apex is the square of the bounce lower")
{
    for (const double e : {0.3, 0.6, 0.9}) {
        CAPTURE(e);
        const ParticleSystem sys(drop(e));
        // Apex n is at the n-th impact time plus v_n / g, where v_n = e^n * 200.
        double hit = 1.0, v = 200.0, previous_apex = 100.0;
        for (int n = 1; n <= 4; ++n) {
            v *= e;
            const double apex_time = hit + v / 200.0;
            const double apex_height = v * v / (2.0 * 200.0);   // above the floor
            CHECK(height(sys, apex_time) == doctest::Approx(-100.0 + apex_height).epsilon(1e-6).scale(100.0));
            CHECK(apex_height / previous_apex == doctest::Approx(e * e));
            previous_apex = apex_height;
            hit += 2.0 * v / 200.0;
        }
    }
}

TEST_CASE("a particle never goes below the floor, and comes to rest on it")
{
    const ParticleSystem sys(drop(0.6));
    for (double t = 0.0; t <= 10.0; t += 0.013)
        CHECK(height(sys, t) >= -100.0 - 1e-6);
    // The bounces add up to a finite time (1 + 2 * 0.6 / (1 - 0.6) = 4 s), and then it lies still.
    CHECK(height(sys, 6.0) == doctest::Approx(-100.0).epsilon(1e-9));
    CHECK(height(sys, 9.9) == doctest::Approx(-100.0).epsilon(1e-9));
}

TEST_CASE("the sideways motion is not changed by a bounce")
{
    Settings plain = drop(0.0), bouncing = drop(0.7);
    plain.speed = bouncing.speed = 40;
    const ParticleSystem a(plain), b(bouncing);
    for (const double t : {0.7, 1.4, 3.0}) {
        const Vec3 p = a.position_at(a.particles().front(), t), q = b.position_at(b.particles().front(), t);
        CHECK(q.x / b.volume_scale().x == doctest::Approx(p.x / a.volume_scale().x).epsilon(1e-9));
        CHECK(q.y / b.volume_scale().y == doctest::Approx(p.y / a.volume_scale().y).epsilon(1e-9));
    }
}

TEST_CASE("a particle that starts under the floor is left alone")
{
    Settings s = drop(0.7);
    s.floor_height = 50;   // the particle starts at 0, under it
    const ParticleSystem a(s);
    s.bounce = 0.0;
    const ParticleSystem b(s);
    for (const double t : {0.5, 2.0, 5.0})
        CHECK(height(a, t) == doctest::Approx(height(b, t)));
}

TEST_CASE("with drag the bounce agrees with stepping the motion")
{
    for (const double drag : {0.4, 1.5}) {
        CAPTURE(drag);
        Settings s = drop(0.7);
        s.drag = drag;
        s.burst = 120;   // thrown up first
        const ParticleSystem sys(s);
        const Particle& p = sys.particles().front();

        // Stepped: dv/dt = g - k v, reflected when it goes below the floor.
        double z = p.spawn.z, v = p.velocity.z, t = 0;
        const double dt = 2e-6;
        for (const double until : {0.5, 1.2, 2.0, 3.5}) {
            while (t < until) {
                v += (-200.0 - drag * v) * dt;
                z += v * dt;
                if (z < -100.0 && v < 0) {
                    z = -100.0 + (-100.0 - z);
                    v = -v * 0.7;
                }
                t += dt;
            }
            CHECK(height(sys, until) == doctest::Approx(z).epsilon(0.01).scale(100.0));
        }
    }
}

TEST_CASE("a bouncing effect fits its volume, exports and has no drawn jump")
{
    Settings s = drop(0.6);
    s.count = 30;
    s.spawn_radius = 40;
    s.spawn_height = 60;
    s.speed = 50;
    s.spread = 30;
    s.burst = 80;
    s.duration = 3.0;
    s.floor_height = -80;   // under every start (they are within +-60), so every particle has a floor to meet
    const ParticleSystem sys(s);
    CHECK(analyze_export(sys).blockers.empty());
    // The motion's lowest point is the floor or above, so the volume is fitted to what is shown.
    CHECK(sys.min_z_raw() >= -80.0 - 1e-9);
}

TEST_CASE("bounce settings are saved, validated and absent from old files")
{
    Settings s;
    s.bounce = 0.4;
    s.floor_height = -35;
    const SettingsLoad back = settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{});
    CHECK(back.settings.bounce == 0.4);
    CHECK(back.settings.floor_height == -35);

    nlohmann::json old = to_json(Settings{});
    old.erase("bounce");
    old.erase("floor_height");
    CHECK(settings_from_json(old, Settings{}).settings.bounce == 0.0);

    Settings wild;
    wild.bounce = 7;
    wild.floor_height = -1e9;
    CHECK(validate(wild));
    CHECK(wild.bounce == kMaxBounce);
    CHECK(wild.floor_height == -kMaxFloorHeight);
}
