#include <doctest/doctest.h>

#include "ParticleSystem.hpp"

using namespace gs;

namespace {

// A loop of 2 s in which a particle lives for the first 1 s. Straight upward
// motion (z = 100 t) keeps every expected value easy to derive by hand. The
// last particle is born at phase 1, so its own time equals the loop time.
ParticleSystem make_system(EndBehavior end, ScaleMode scale = ScaleMode::Static)
{
    Settings s;
    s.count = 4;
    s.duration = 2.0;
    s.lifespan = 0.5;
    s.fps = 10;
    s.end_behavior = end;
    s.speed = 0;
    s.spread = 0;
    s.burst = 100;
    s.gravity = 0;
    s.turbulence = 0;
    s.geometry = Geometry::SingleQuad;
    s.scale_mode = scale;
    s.scale_start = 1.0;
    s.scale_end = 3.0;
    return ParticleSystem(s);
}

constexpr double kBeforeDeath = 0.9, kAtDeath = 1.0, kAfterDeath = 1.1, kLate = 1.99;

} // namespace

TEST_CASE("a particle is alive up to and including its lifespan")
{
    const ParticleSystem sys = make_system(EndBehavior::HideUnderground);
    const Particle& p = sys.particles().back();

    const LifeState before = sys.life_state(p, kBeforeDeath);
    CHECK(before.kind == LifeState::Kind::Alive);
    CHECK_FALSE(before.dead);
    CHECK(before.eval_time == doctest::Approx(kBeforeDeath));

    // Dying is "ratio > lifespan": exactly at the lifespan it still lives.
    const LifeState at = sys.life_state(p, kAtDeath);
    CHECK(at.kind == LifeState::Kind::Alive);
    CHECK_FALSE(at.dead);
}

TEST_CASE("particles are born evenly over the loop and wrap around it")
{
    const ParticleSystem sys = make_system(EndBehavior::HideUnderground);
    REQUIRE(sys.particles().size() == 4);
    CHECK(sys.particles()[0].phase_offset == doctest::Approx(0.25));
    CHECK(sys.particles()[3].phase_offset == doctest::Approx(1.0));

    // The first particle is 0.5 s ahead: at loop time 1.6 it was born again 0.1 s ago.
    const LifeState st = sys.life_state(sys.particles()[0], 1.6);
    CHECK(st.kind == LifeState::Kind::Alive);
    CHECK(st.eval_time == doctest::Approx(0.1));
}

TEST_CASE("hide behaviors and drop through floor hand a dead particle to the end behavior")
{
    for (EndBehavior end : {EndBehavior::HideUnderground, EndBehavior::HideInCore, EndBehavior::DropThroughFloor}) {
        CAPTURE(enum_id(end));
        const ParticleSystem sys = make_system(end);
        const LifeState st = sys.life_state(sys.particles().back(), kAfterDeath);
        CHECK(st.kind == LifeState::Kind::Dead);
        CHECK(st.dead);
        CHECK(st.eval_time == doctest::Approx(kAfterDeath));
    }
}

TEST_CASE("stop and stay freezes time at the end of life")
{
    const ParticleSystem sys = make_system(EndBehavior::StopAndStay);
    const LifeState st = sys.life_state(sys.particles().back(), kAfterDeath);
    CHECK(st.kind == LifeState::Kind::Alive);
    CHECK(st.dead);
    CHECK(st.eval_time == doctest::Approx(1.0));

    const ParticlePose pose = sys.pose_at(sys.particles().back(), kLate);
    CHECK(pose.visible);
    CHECK(pose.pos.z == doctest::Approx(sys.position_at(sys.particles().back(), 1.0).z));
}

TEST_CASE("endless loop keeps moving past the lifespan")
{
    const ParticleSystem sys = make_system(EndBehavior::EndlessLoop);
    const Particle& p = sys.particles().back();
    const LifeState st = sys.life_state(p, kAfterDeath);
    CHECK(st.kind == LifeState::Kind::Alive);
    CHECK(st.dead);
    CHECK(st.eval_time == doctest::Approx(kAfterDeath));

    const ParticlePose pose = sys.pose_at(p, kAfterDeath);
    CHECK(pose.visible);
    CHECK(pose.pos.z == doctest::Approx(sys.position_at(p, kAfterDeath).z));
}

TEST_CASE("an alive particle is posed by the motion equations")
{
    const ParticleSystem sys = make_system(EndBehavior::HideUnderground);
    const ParticlePose pose = sys.pose_at(sys.particles().back(), 0.5);
    CHECK(pose.visible);
    CHECK(pose.scale == doctest::Approx(1.0));
    CHECK(pose.pos.x == doctest::Approx(0.0));
    CHECK(pose.pos.y == doctest::Approx(0.0));
    // z = 100 * 0.5 in raw units, scaled so the 0..100 range fills the 256 high volume.
    CHECK(pose.pos.z == doctest::Approx(128.0));
}

TEST_CASE("hide underground parks a dead particle below the effect")
{
    const ParticleSystem sys = make_system(EndBehavior::HideUnderground);
    const ParticlePose pose = sys.pose_at(sys.particles().back(), kAfterDeath);
    CHECK_FALSE(pose.visible);
    CHECK(pose.pos.x == doctest::Approx(0.0));
    CHECK(pose.pos.z == doctest::Approx(sys.hide_z()));
    CHECK(pose.pos.z < sys.min_z_raw() * sys.scale_z());
    CHECK(pose.scale == doctest::Approx(1.0));   // static quads rely on the parking height
}

TEST_CASE("hide underground collapses a vertex-bone quad as well")
{
    const ParticleSystem sys = make_system(EndBehavior::HideUnderground, ScaleMode::VertexBone);
    const ParticlePose pose = sys.pose_at(sys.particles().back(), kAfterDeath);
    CHECK_FALSE(pose.visible);
    CHECK(pose.scale == doctest::Approx(0.0));
}

TEST_CASE("hide in core parks at the center: collapsed with vertex bones, visible without")
{
    {
        const ParticleSystem sys = make_system(EndBehavior::HideInCore, ScaleMode::VertexBone);
        const ParticlePose pose = sys.pose_at(sys.particles().back(), kAfterDeath);
        CHECK_FALSE(pose.visible);
        CHECK(pose.scale == doctest::Approx(0.0));
        CHECK(pose.pos == Vec3{});
    }
    {
        const ParticleSystem sys = make_system(EndBehavior::HideInCore, ScaleMode::Static);
        const ParticlePose pose = sys.pose_at(sys.particles().back(), kAfterDeath);
        CHECK(pose.visible);   // a static quad cannot shrink: exported with a warning
        CHECK(pose.pos == Vec3{});
    }
}

TEST_CASE("drop through floor falls from the death position until it is parked")
{
    const ParticleSystem sys = make_system(EndBehavior::DropThroughFloor);
    const Particle& p = sys.particles().back();
    const Vec3 at_death = sys.position_at(p, 1.0);

    const ParticlePose falling = sys.pose_at(p, kAfterDeath);
    CHECK(falling.visible);
    CHECK(falling.pos.x == doctest::Approx(at_death.x));
    CHECK(falling.pos.z == doctest::Approx(at_death.z - 0.5 * sys.drop_acceleration() * 0.1 * 0.1));
    CHECK(falling.pos.z < at_death.z);

    // Late in the loop it fell below the effect and waits at the hiding height.
    const ParticlePose parked = sys.pose_at(p, kLate);
    CHECK_FALSE(parked.visible);
    CHECK(parked.pos.z == doctest::Approx(sys.hide_z()));
}

TEST_CASE("vertex-bone scale follows the life of the particle")
{
    const ParticleSystem sys = make_system(EndBehavior::StopAndStay, ScaleMode::VertexBone);
    const Particle& p = sys.particles().back();
    CHECK(sys.pose_at(p, 0.0).scale == doctest::Approx(1.0));
    CHECK(sys.pose_at(p, 0.5).scale == doctest::Approx(2.0));   // halfway through its 1 s life
    CHECK(sys.pose_at(p, kLate).scale == doctest::Approx(3.0)); // stopped at the end scale
}
