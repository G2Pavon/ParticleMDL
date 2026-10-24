#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "Baking.hpp"
#include "Exporter.hpp"
#include "Presets.hpp"

using namespace gs;

namespace {

constexpr EndBehavior kAllEnds[] = {EndBehavior::DropThroughFloor, EndBehavior::HideUnderground,
                                    EndBehavior::HideInCore, EndBehavior::StopAndStay, EndBehavior::EndlessLoop};

double length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double horizontal(Vec3 v)
{
    return std::hypot(v.x, v.y);
}

Settings moving(EndBehavior end, ScaleMode scale, double lifespan, int fps)
{
    Settings s;
    s.count = 8;
    s.duration = 2.0;
    s.fps = fps;
    s.lifespan = lifespan;
    s.end_behavior = end;
    s.scale_mode = scale;
    s.scale_start = 1.0;
    s.scale_end = 0.5;
    s.geometry = Geometry::SingleQuad;
    s.speed = 60;
    s.burst = 120;
    s.spread = 30;
    s.gravity = -100;
    s.turbulence = 20;
    s.particle_spin = {0, 0, 90};
    return s;
}

// Where the particle really is moving on its own, per frame: the segments in
// which it neither dies, nor is born, nor wraps around the loop. Worked out
// from the ideal motion only, so it does not depend on how baking finds jumps.
struct Natural {
    double travel = 0;      // largest 3D step
    double horizontal = 0;  // largest sideways step
};

Natural natural_steps(const ParticleSystem& sys)
{
    Natural n;
    const int steps = sys.loop_steps();
    const double loop = sys.loop_duration();
    for (const Particle& p : sys.particles()) {
        for (int f = 0; f < steps; ++f) {
            const double t0 = sys.frame_time(f), t1 = loop * (f + 1) / steps;
            if (sys.generation(p, t0) != sys.generation(p, t1) ||
                sys.life_state(p, t0).past_lifespan() != sys.life_state(p, t1).past_lifespan())
                continue;
            const Vec3 d = sys.pose_at(p, t1).pos - sys.pose_at(p, t0).pos;
            n.travel = std::max(n.travel, length(d));
            n.horizontal = std::max(n.horizontal, horizontal(d));
        }
    }
    return n;
}

// Counts the segments (frame to next frame, around the loop) the engine would
// draw a quad in, by the rule `drawn`.
template <typename Drawn, typename Check>
int for_each_drawn_segment(const BakedPoses& baked, Drawn drawn, Check check)
{
    int count = 0;
    for (int i = 0; i < baked.particles(); ++i) {
        for (int f = 0; f < baked.steps(); ++f) {
            const ParticlePose& a = baked.at(f, i);
            const ParticlePose& b = baked.at(f + 1, i);
            if (drawn(a, b)) {
                check(a, b);
                ++count;
            }
        }
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// Vertex bones: a quad is never drawn across a jump
// ---------------------------------------------------------------------------

TEST_CASE("with vertex bones no visible quad travels farther than the motion allows")
{
    for (const EndBehavior end : kAllEnds)
        for (const double lifespan : {0.3, 0.6, 0.95, 1.0})
            for (const int fps : {10, 15, 30}) {
                CAPTURE(enum_id(end));
                CAPTURE(lifespan);
                CAPTURE(fps);
                const ParticleSystem sys(moving(end, ScaleMode::VertexBone, lifespan, fps));
                const BakedPoses baked = bake(sys);
                const double limit = natural_steps(sys).travel * 1.5 + 1e-6;

                const int drawn = for_each_drawn_segment(
                    baked, [](const ParticlePose& a, const ParticlePose& b) { return a.scale > 0 || b.scale > 0; },
                    [&](const ParticlePose& a, const ParticlePose& b) { CHECK(length(b.pos - a.pos) <= limit); });
                CHECK(drawn > 0);   // baking does not make everything vanish
            }
}

TEST_CASE("every preset with vertex bones draws no travelling jump")
{
    int checked = 0;
    for (const Preset& preset : builtin_presets()) {
        Settings s;
        apply_preset(s, preset);
        if (s.scale_mode != ScaleMode::VertexBone)
            continue;
        CAPTURE(preset.name);
        const ParticleSystem sys(s);
        const BakedPoses baked = bake(sys);
        const double limit = natural_steps(sys).travel * 1.5 + 1e-6;
        for_each_drawn_segment(
            baked, [](const ParticlePose& a, const ParticlePose& b) { return a.scale > 0 || b.scale > 0; },
            [&](const ParticlePose& a, const ParticlePose& b) { CHECK(length(b.pos - a.pos) <= limit); });
        ++checked;
    }
    CHECK(checked >= 4);
}

TEST_CASE("vertex-bone baking changes only the scale of the frames around a jump")
{
    for (const EndBehavior end : kAllEnds) {
        CAPTURE(enum_id(end));
        const ParticleSystem sys(moving(end, ScaleMode::VertexBone, 0.7, 15));
        const BakedPoses baked = bake(sys);
        int collapsed = 0;
        for (int f = 0; f < baked.steps(); ++f)
            for (int i = 0; i < baked.particles(); ++i) {
                const ParticlePose ideal = sys.pose_at(sys.particles()[i], sys.frame_time(f));
                const ParticlePose& got = baked.at(f, i);
                CHECK(got.pos == ideal.pos);
                CHECK(got.rot == ideal.rot);
                CHECK(got.visible == ideal.visible);
                if (got.scale != ideal.scale) {
                    CHECK(got.scale == 0.0);
                    ++collapsed;
                }
            }
        CHECK(collapsed > 0);
    }
}

TEST_CASE("a particle that is alive for only two frames is never drawn")
{
    // 4 frames per loop (2 fps for 2 s), alive for the first 0.8 s: frames at 0 s and 0.5 s only.
    // The first and the last frame of a life are collapsed, so nothing is left to draw.
    Settings s = moving(EndBehavior::HideUnderground, ScaleMode::VertexBone, 0.4, 2);
    s.duration = 2.0;
    s.count = 2;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    for (int f = 0; f < baked.steps(); ++f)
        for (int i = 0; i < baked.particles(); ++i)
            CHECK(baked.at(f, i).scale == 0.0);
}

// ---------------------------------------------------------------------------
// Static quads
// ---------------------------------------------------------------------------

TEST_CASE("static quads that hide underground dive and rise vertically")
{
    for (const EndBehavior end : {EndBehavior::HideUnderground, EndBehavior::DropThroughFloor})
        for (const double lifespan : {0.4, 0.8, 1.0}) {
            CAPTURE(enum_id(end));
            CAPTURE(lifespan);
            Settings s = moving(end, ScaleMode::Static, lifespan, 30);
            const ParticleSystem sys(s);
            const BakedPoses baked = bake(sys);
            const double limit = natural_steps(sys).horizontal * 1.5 + 1e-6;

            // Whatever the engine draws, it never sweeps sideways farther than the motion does:
            // the jumps are vertical, or happen below the effect.
            const int drawn = for_each_drawn_segment(
                baked, [](const ParticlePose& a, const ParticlePose& b) { return a.visible || b.visible; },
                [&](const ParticlePose& a, const ParticlePose& b) { CHECK(horizontal(b.pos - a.pos) <= limit); });
            CHECK(drawn > 0);
        }
}

namespace {

// Spinning, so a quad that turned while it dives or rises would show it.
Settings spinning(EndBehavior end, double lifespan)
{
    Settings s = moving(end, ScaleMode::Static, lifespan, 30);
    s.particle_spin = {40, 0, 90};
    return s;
}

} // namespace

TEST_CASE("a dying static quad is parked right below where it died, turned as it died")
{
    const ParticleSystem sys(spinning(EndBehavior::HideUnderground, 0.4));
    const BakedPoses baked = bake(sys);
    int dives = 0;
    for (int i = 0; i < baked.particles(); ++i)
        for (int f = 0; f < baked.steps(); ++f) {
            const ParticlePose& a = baked.at(f, i);
            const ParticlePose& b = baked.at(f + 1, i);
            if (a.visible && !b.visible) {
                CHECK(b.pos.x == a.pos.x);
                CHECK(b.pos.y == a.pos.y);
                CHECK(b.pos.z == doctest::Approx(sys.hide_z()));
                // It turns on only as far as it spins until it dies, never back to no turn.
                const Quat qa = quat_from_euler(a.rot.x, a.rot.y, a.rot.z), qb = quat_from_euler(b.rot.x, b.rot.y, b.rot.z);
                const double dot = std::abs(qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z);
                const double turned = 2.0 * std::acos(std::min(1.0, dot));
                CHECK(turned <= radians(std::hypot(40.0, 90.0)) / 30.0 + 1e-9);
                CHECK(a.rot != Vec3{});
                ++dives;
            }
        }
    CHECK(dives == baked.particles());   // each particle dies once per loop
}

TEST_CASE("a reborn static quad waits right below where it reappears, turned the same way")
{
    const ParticleSystem sys(spinning(EndBehavior::HideUnderground, 0.4));
    const BakedPoses baked = bake(sys);
    int rises = 0;
    for (int i = 0; i < baked.particles(); ++i)
        for (int f = 0; f < baked.steps(); ++f) {
            const ParticlePose& a = baked.at(f, i);
            const ParticlePose& b = baked.at(f + 1, i);
            if (!a.visible && b.visible) {
                CHECK(a.pos.x == b.pos.x);
                CHECK(a.pos.y == b.pos.y);
                CHECK(a.pos.z == doctest::Approx(sys.hide_z()));
                CHECK(a.rot == b.rot);
                ++rises;
            }
        }
    CHECK(rises == baked.particles());
}

TEST_CASE("with no hidden frame between lives a static quad gives up the two frames around the jump")
{
    const ParticleSystem sys(moving(EndBehavior::HideUnderground, ScaleMode::Static, 1.0, 30));
    const BakedPoses baked = bake(sys);
    int underground = 0;
    for (int f = 0; f < baked.steps(); ++f)
        for (int i = 0; i < baked.particles(); ++i)
            if (!baked.at(f, i).visible) {
                CHECK(baked.at(f, i).pos.z == doctest::Approx(sys.hide_z()));
                ++underground;
            }
    CHECK(underground == 2 * baked.particles());   // one frame before and one after the wrap, per particle
}

TEST_CASE("static quads that cannot hide are left as the ideal motion")
{
    for (const EndBehavior end : {EndBehavior::HideInCore, EndBehavior::StopAndStay, EndBehavior::EndlessLoop}) {
        CAPTURE(enum_id(end));
        const ParticleSystem sys(moving(end, ScaleMode::Static, 0.6, 15));
        const BakedPoses baked = bake(sys);
        for (int f = 0; f < baked.steps(); ++f)
            for (int i = 0; i < baked.particles(); ++i) {
                const ParticlePose ideal = sys.pose_at(sys.particles()[i], sys.frame_time(f));
                CHECK(baked.at(f, i).pos == ideal.pos);
                CHECK(baked.at(f, i).scale == ideal.scale);
                CHECK(baked.at(f, i).visible == ideal.visible);
            }
        CHECK(baked.unhidden_jump() > 0.0);
    }
}

TEST_CASE("static endless loops warn about the jump that cannot be hidden")
{
    // Small quads flying far: the jump back is many quad sizes.
    Settings far = moving(EndBehavior::EndlessLoop, ScaleMode::Static, 0.9, 15);
    far.quad_width = far.quad_height = 3;
    const ExportReport warned = analyze_export(ParticleSystem(far));
    REQUIRE(warned.warnings.size() == 1);
    CHECK(warned.warnings.front().find("cannot hide a respawn") != std::string::npos);

    Settings stop = far;
    stop.end_behavior = EndBehavior::StopAndStay;
    CHECK(analyze_export(ParticleSystem(stop)).warnings.size() == 1);

    // Quads larger than the jump, or vertex bones, need no warning.
    Settings huge = far;
    huge.quad_width = huge.quad_height = 4096;
    CHECK(analyze_export(ParticleSystem(huge)).warnings.empty());

    Settings bones = far;
    bones.scale_mode = ScaleMode::VertexBone;
    bones.count = 20;
    CHECK(analyze_export(ParticleSystem(bones)).warnings.empty());

    // The behaviors that can hide a static quad are not warned about.
    Settings hide = far;
    hide.end_behavior = EndBehavior::HideUnderground;
    CHECK(analyze_export(ParticleSystem(hide)).warnings.empty());
}

TEST_CASE("a single hidden frame between lives makes the rise slant, and is warned about")
{
    auto slant_warnings = [](const ExportReport& r) {
        return std::ranges::count_if(r.warnings, [](const std::string& w) { return w.find("at a slant") != std::string::npos; });
    };
    // 2 s at 15 fps: dead for 0.09 s, under 1.5 frames, so some particles have one hidden frame.
    const ParticleSystem tight(moving(EndBehavior::HideUnderground, ScaleMode::Static, 0.955, 15));
    CHECK(bake(tight).slanted_rise() > 6.0);
    CHECK(slant_warnings(analyze_export(tight)) == 1);

    // Dead for six frames: every rise is straight up.
    const ParticleSystem roomy(moving(EndBehavior::HideUnderground, ScaleMode::Static, 0.8, 15));
    CHECK(bake(roomy).slanted_rise() == doctest::Approx(0.0));
    CHECK(slant_warnings(analyze_export(roomy)) == 0);

    // Twice the frame rate gives the same particles a second hidden frame.
    const ParticleSystem faster(moving(EndBehavior::HideUnderground, ScaleMode::Static, 0.955, 30));
    CHECK(bake(faster).slanted_rise() == doctest::Approx(0.0));
}

TEST_CASE("particles whose lives are too short to be drawn are warned about")
{
    // 1.6 frames of life: the collapse around birth and death leaves nothing to draw.
    Settings s;
    s.count = 20;
    s.duration = 2.0;
    s.fps = 10;
    s.lifespan = 0.08;
    s.geometry = Geometry::SingleQuad;
    s.scale_mode = ScaleMode::VertexBone;
    s.end_behavior = EndBehavior::HideInCore;
    const ExportReport short_lives = analyze_export(ParticleSystem(s));
    REQUIRE(short_lives.warnings.size() == 1);
    CHECK(short_lives.warnings.front().find("20 of 20 particles are never drawn") != std::string::npos);
    CHECK(short_lives.warnings.front().find("Vertex Bone") != std::string::npos);

    // Three frames of life are enough.
    Settings longer = s;
    longer.lifespan = 0.15;
    CHECK(analyze_export(ParticleSystem(longer)).warnings.empty());
    Settings faster = s;
    faster.fps = 20;
    CHECK(analyze_export(ParticleSystem(faster)).warnings.empty());
}

// ---------------------------------------------------------------------------
// The baked table and sampling
// ---------------------------------------------------------------------------

TEST_CASE("the closing frame repeats frame zero")
{
    const ParticleSystem sys(moving(EndBehavior::HideUnderground, ScaleMode::VertexBone, 0.7, 15));
    const BakedPoses baked = bake(sys);
    CHECK(baked.steps() == sys.loop_steps());
    CHECK(baked.frames() == sys.export_frames());
    for (int i = 0; i < baked.particles(); ++i) {
        CHECK(baked.at(baked.steps(), i).pos == baked.at(0, i).pos);
        CHECK(baked.at(baked.steps(), i).scale == baked.at(0, i).scale);
    }

    const AnimationTable table = animation_table(sys, baked);
    CHECK(table.frames == baked.frames());
    for (int bone = 0; bone < table.bones; ++bone)
        CHECK(table.at(table.frames - 1, bone) == table.at(0, bone));
}

TEST_CASE("the exported animation is built from the baked frames")
{
    const ParticleSystem sys(moving(EndBehavior::EndlessLoop, ScaleMode::VertexBone, 0.7, 15));
    const BakedPoses baked = bake(sys);
    const AnimationTable via_baked = animation_table(sys, baked);
    const AnimationTable direct = animation_table(sys);
    CHECK(via_baked.values == direct.values);

    // A collapsed frame puts the corner bones at -vertex.
    bool found = false;
    for (int f = 0; f < baked.steps() && !found; ++f)
        if (baked.at(f, 0).scale == 0.0) {
            const Vec3 corner = sys.quads()[0][0].pos;
            const auto& row = via_baked.at(f, sys.bones().corner_bone(0, 0));
            CHECK(row[0] == doctest::Approx(-corner.x));
            CHECK(row[1] == doctest::Approx(-corner.y));
            CHECK(row[2] == doctest::Approx(-corner.z));
            found = true;
        }
    CHECK(found);
}

TEST_CASE("sampling at a frame returns that frame and halfway returns the middle")
{
    const ParticleSystem sys(moving(EndBehavior::EndlessLoop, ScaleMode::VertexBone, 0.7, 10));
    const BakedPoses baked = bake(sys);
    const double dt = baked.loop_duration() / baked.steps();
    for (const int f : {0, 3, baked.steps() - 1}) {
        CAPTURE(f);
        const ParticlePose& a = baked.at(f, 1);
        const ParticlePose& b = baked.at(f + 1, 1);

        const SampledPose exact = baked.sample(1, f * dt);
        CHECK(exact.pos.x == doctest::Approx(a.pos.x));
        CHECK(exact.pos.z == doctest::Approx(a.pos.z));
        CHECK(exact.scale == doctest::Approx(a.scale));

        const SampledPose mid = baked.sample(1, (f + 0.5) * dt);
        CHECK(mid.pos.x == doctest::Approx((a.pos.x + b.pos.x) / 2));
        CHECK(mid.pos.y == doctest::Approx((a.pos.y + b.pos.y) / 2));
        CHECK(mid.pos.z == doctest::Approx((a.pos.z + b.pos.z) / 2));
        CHECK(mid.scale == doctest::Approx((a.scale + b.scale) / 2));
    }
}

TEST_CASE("sampling wraps around the loop")
{
    const ParticleSystem sys(moving(EndBehavior::EndlessLoop, ScaleMode::Static, 0.7, 10));
    const BakedPoses baked = bake(sys);
    const double loop = baked.loop_duration();

    const SampledPose start = baked.sample(0, 0.0);
    const SampledPose one_loop = baked.sample(0, loop);
    const SampledPose before_start = baked.sample(0, -loop);
    CHECK(one_loop.pos == start.pos);
    CHECK(before_start.pos == start.pos);

    // Just before the loop ends the pose is nearly the closing frame, which is frame 0.
    const SampledPose last = baked.sample(0, loop - 1e-9);
    CHECK(last.pos.x == doctest::Approx(baked.at(0, 0).pos.x).epsilon(1e-6));
}

TEST_CASE("sampling a pose that is visible only at one end still draws it")
{
    const ParticleSystem sys(moving(EndBehavior::HideUnderground, ScaleMode::Static, 0.4, 30));
    const BakedPoses baked = bake(sys);
    // The dive: a visible frame followed by a hidden one is drawn in between.
    for (int i = 0; i < baked.particles(); ++i)
        for (int f = 0; f < baked.steps(); ++f)
            if (baked.at(f, i).visible && !baked.at(f + 1, i).visible) {
                const double dt = baked.loop_duration() / baked.steps();
                CHECK(baked.sample(i, (f + 0.5) * dt).visible);
            }
}

// ---------------------------------------------------------------------------
// Quaternions
// ---------------------------------------------------------------------------

TEST_CASE("quat_from_euler rotates like apply_static_rotation")
{
    std::uint32_t state = 7;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return (static_cast<double>(state >> 8) / (1 << 24)) * 2 * kPi;
    };
    for (int i = 0; i < 200; ++i) {
        const double rx = next(), ry = next(), rz = next();
        const Vec3 v{0.3, -1.2, 2.0};
        const Vec3 a = ParticleSystem::apply_static_rotation(v, rx, ry, rz);
        const Vec3 b = rotate(quat_from_euler(rx, ry, rz), v);
        CHECK(b.x == doctest::Approx(a.x).epsilon(1e-9));
        CHECK(b.y == doctest::Approx(a.y).epsilon(1e-9));
        CHECK(b.z == doctest::Approx(a.z).epsilon(1e-9));
    }
}

TEST_CASE("slerp halves a rotation")
{
    const Quat a = quat_from_euler(0, 0, 0), b = quat_from_euler(0, 0, kPi / 2);
    const Vec3 v = rotate(slerp(a, b, 0.5), {1, 0, 0});
    CHECK(v.x == doctest::Approx(std::cos(kPi / 4)));
    CHECK(v.y == doctest::Approx(std::sin(kPi / 4)));
    CHECK(length(rotate(slerp(a, b, 0.0), {1, 0, 0}) - Vec3{1, 0, 0}) < 1e-12);
    CHECK(length(rotate(slerp(a, b, 1.0), {1, 0, 0}) - Vec3{0, 1, 0}) < 1e-12);
}

TEST_CASE("slerp takes the short way round, also across a full turn")
{
    // 6.2 rad and 0.1 rad about Z are 0.1832 rad apart, not 6.1.
    const Quat a = quat_from_euler(0, 0, 6.2), b = quat_from_euler(0, 0, 0.1);
    const Vec3 v = rotate(slerp(a, b, 0.5), {1, 0, 0});
    const double mid = 6.2 + (0.1 + 2 * kPi - 6.2) / 2 - 2 * kPi;   // about 0.0084
    CHECK(v.x == doctest::Approx(std::cos(mid)));
    CHECK(v.y == doctest::Approx(std::sin(mid)));

    // A quaternion and its negative are the same rotation.
    const Quat c = quat_from_euler(0.3, 0.2, 0.1);
    const Vec3 same = rotate(slerp(c, Quat{-c.w, -c.x, -c.y, -c.z}, 0.5), {1, 2, 3});
    CHECK(length(same - rotate(c, {1, 2, 3})) < 1e-9);
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

TEST_CASE("the bounding box covers the baked frames, dives included")
{
    const ParticleSystem sys(moving(EndBehavior::HideUnderground, ScaleMode::Static, 1.0, 30));
    const BakedPoses baked = bake(sys);
    const ParticleSystem::Bounds b = animation_bounds(sys, baked);
    for (int f = 0; f < baked.steps(); ++f)
        for (int i = 0; i < baked.particles(); ++i) {
            const Vec3 p = baked.at(f, i).pos;
            CHECK(p.x >= b.min.x);
            CHECK(p.x <= b.max.x);
            CHECK(p.y >= b.min.y);
            CHECK(p.y <= b.max.y);
            CHECK(p.z >= b.min.z);
            CHECK(p.z <= b.max.z);
        }
    CHECK(b.min.z <= sys.hide_z());   // the frames underground are inside the box
}
