#include "Baking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gs {

namespace {

double length(Vec3 v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// A jump between frame `from` and the next frame `to`.
struct Seam {
    int from, to;
};

bool hides_with_scale(const Settings& s) noexcept
{
    return s.end_behavior == EndBehavior::HideUnderground || s.end_behavior == EndBehavior::HideInCore;
}

// Where in a particle's frames its pose jumps: a new life starts (death and
// rebirth with a hidden stretch in between, or the wrap of a loop), or the
// particle is parked and brought back.
std::vector<Seam> find_seams(const ParticleSystem& sys, const Particle& p)
{
    const int steps = sys.loop_steps();
    const double loop = sys.loop_duration();
    const bool dead_is_a_jump = hides_with_scale(sys.settings());

    std::vector<Seam> seams;
    for (int f = 0; f < steps; ++f) {
        const double t0 = sys.frame_time(f);
        const double t1 = loop * (f + 1) / steps;   // not wrapped: the closing frame is one loop on
        const bool new_life = sys.generation(p, t0) != sys.generation(p, t1);
        const bool toggled = sys.life_state(p, t0).dead != sys.life_state(p, t1).dead;
        if (new_life || (dead_is_a_jump && toggled))
            seams.push_back({f, (f + 1) % steps});
    }
    return seams;
}

bool underground(const ParticlePose& pose, double hide_z) noexcept
{
    return pose.pos.z <= hide_z + 1e-9;
}

void place_underground(ParticlePose& pose, double x, double y, double hide_z) noexcept
{
    pose.pos = {x, y, hide_z};
    pose.visible = false;
}

} // namespace

BakedPoses bake(const ParticleSystem& sys)
{
    const Settings& s = sys.settings();
    BakedPoses b;
    b.steps_ = sys.loop_steps();
    b.particles_ = sys.num_particles();
    b.loop_ = sys.loop_duration();
    b.poses_.resize(static_cast<std::size_t>(b.steps_) * b.particles_);

    for (int f = 0; f < b.steps_; ++f)
        for (int i = 0; i < b.particles_; ++i)
            b.at_mut(f, i) = sys.pose_at(sys.particles()[i], sys.frame_time(f));

    const bool vertex_bones = s.scale_mode == ScaleMode::VertexBone;
    const bool dives = s.end_behavior == EndBehavior::HideUnderground || s.end_behavior == EndBehavior::DropThroughFloor;
    const double hide_z = sys.hide_z();

    for (int i = 0; i < b.particles_; ++i) {
        const std::vector<Seam> seams = find_seams(sys, sys.particles()[i]);
        if (seams.empty())
            continue;

        if (vertex_bones) {
            // Collapsed on both sides of the jump, so nothing is drawn across it.
            for (const Seam& seam : seams) {
                b.at_mut(seam.from, i).scale = 0.0;
                b.at_mut(seam.to, i).scale = 0.0;
            }
            continue;
        }

        // Where the particle was and will be, before this loop changes anything.
        std::vector<ParticlePose> ideal(static_cast<std::size_t>(b.steps_));
        for (int f = 0; f < b.steps_; ++f)
            ideal[static_cast<std::size_t>(f)] = b.at(f, i);

        if (!dives) {
            for (const Seam& seam : seams) {
                const ParticlePose& a = ideal[static_cast<std::size_t>(seam.from)];
                const ParticlePose& c = ideal[static_cast<std::size_t>(seam.to)];
                if (a.visible && c.visible)
                    b.unhidden_jump_ = std::max(b.unhidden_jump_, length(c.pos - a.pos));
            }
            continue;
        }

        // Rising: the frame before a rebirth waits right below where the particle reappears.
        for (const Seam& seam : seams) {
            const ParticlePose& a = ideal[static_cast<std::size_t>(seam.from)];
            const ParticlePose& c = ideal[static_cast<std::size_t>(seam.to)];
            if (underground(a, hide_z) && !underground(c, hide_z))
                place_underground(b.at_mut(seam.from, i), c.pos.x, c.pos.y, hide_z);
        }
        // Diving: the frame after a death sits right below where the particle was. This
        // goes second, so with a single hidden frame the dive (the longer way) wins.
        for (const Seam& seam : seams) {
            const ParticlePose& a = ideal[static_cast<std::size_t>(seam.from)];
            const ParticlePose& c = ideal[static_cast<std::size_t>(seam.to)];
            const bool a_under = underground(a, hide_z), c_under = underground(c, hide_z);
            if (!a_under && c_under) {
                place_underground(b.at_mut(seam.to, i), a.pos.x, a.pos.y, hide_z);
            } else if (!a_under && !c_under) {
                // Reborn with no hidden frame between: the two frames around the jump
                // are given up, so the jump itself happens underground.
                place_underground(b.at_mut(seam.from, i), a.pos.x, a.pos.y, hide_z);
                place_underground(b.at_mut(seam.to, i), c.pos.x, c.pos.y, hide_z);
            }
        }
    }
    return b;
}

SampledPose to_sampled(const ParticlePose& pose)
{
    return {pose.pos, quat_from_euler(pose.rot.x, pose.rot.y, pose.rot.z), pose.scale, pose.visible};
}

SampledPose BakedPoses::sample(int particle, double loop_time) const
{
    const double f = floor_mod(loop_time, loop_) / loop_ * steps_;
    const int i = std::min(static_cast<int>(f), steps_ - 1);
    const double t = f - i;

    const ParticlePose& a = at(i, particle);
    const ParticlePose& c = at(i + 1, particle);   // the closing frame wraps to frame 0
    SampledPose out;
    out.pos = a.pos + (c.pos - a.pos) * t;
    out.scale = a.scale + (c.scale - a.scale) * t;
    out.rot = slerp(quat_from_euler(a.rot.x, a.rot.y, a.rot.z), quat_from_euler(c.rot.x, c.rot.y, c.rot.z), t);
    out.visible = a.visible || c.visible;
    return out;
}

ParticleSystem::Bounds animation_bounds(const ParticleSystem& sys, const BakedPoses& baked)
{
    constexpr double inf = std::numeric_limits<double>::infinity();
    ParticleSystem::Bounds b{{inf, inf, inf}, {-inf, -inf, -inf}};
    for (int f = 0; f < baked.steps(); ++f) {   // the closing frame repeats frame 0
        for (int i = 0; i < baked.particles(); ++i) {
            const ParticlePose& pose = baked.at(f, i);
            for (const Quad& q : sys.quads()) {
                for (const QuadVertex& v : q) {
                    const Vec3 c = pose.pos + ParticleSystem::apply_static_rotation(v.pos * pose.scale, pose.rot.x,
                                                                                    pose.rot.y, pose.rot.z);
                    b.min = {std::min(b.min.x, c.x), std::min(b.min.y, c.y), std::min(b.min.z, c.z)};
                    b.max = {std::max(b.max.x, c.x), std::max(b.max.y, c.y), std::max(b.max.z, c.z)};
                }
            }
        }
    }
    return b;
}

} // namespace gs
