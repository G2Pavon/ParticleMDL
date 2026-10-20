#pragma once

#include <vector>

#include "ParticleSystem.hpp"

namespace gs {

// A pose as the engine shows it between two baked frames. The rotation is a
// quaternion because that is what the engine blends.
struct SampledPose {
    Vec3 pos;
    Quat rot;
    double scale = 1.0;
    bool visible = true;
};

// What the exported sequence holds: every particle's pose at every frame of one
// loop. GoldSrc only ever sees these frames and draws a straight line (a
// quaternion slerp for rotation) between neighbours, so a particle that jumps
// - dies, is born, wraps around the loop - would streak across the effect.
// Baking prepares those jumps so the streak is hidden or kept short:
//   Vertex-bone scaling: both frames around a jump get scale 0, so the quad
//     collapses, travels as a point and grows back.
//   Static quads, Hide Underground and Drop Through Floor: the quad dives
//     straight down from where it was and rises straight up under where it is
//     reborn, so the travel in between is underground.
//   Static quads, other end behaviors: a full-size quad cannot hide, the jump
//     stays (see unhidden_jump).
// pose_at() stays the ideal motion; this is the same motion as it can be played.
class BakedPoses {
public:
    int frames() const noexcept { return steps_ + 1; }   // the closing frame repeats frame 0
    int steps() const noexcept { return steps_; }
    int particles() const noexcept { return particles_; }
    double loop_duration() const noexcept { return loop_; }

    // `frame` may be the closing frame (steps()).
    const ParticlePose& at(int frame, int particle) const noexcept
    {
        return poses_[static_cast<std::size_t>(frame % steps_) * particles_ + particle];
    }

    // The pose at `loop_time`: linear between the two baked frames around it.
    SampledPose sample(int particle, double loop_time) const;

    // Largest distance a full-size, visible quad travels in one frame across a
    // jump that could not be hidden; 0 when there is none.
    double unhidden_jump() const noexcept { return unhidden_jump_; }

private:
    friend BakedPoses bake(const ParticleSystem& sys);

    ParticlePose& at_mut(int frame, int particle) noexcept
    {
        return poses_[static_cast<std::size_t>(frame) * particles_ + particle];
    }

    int steps_ = 1;
    int particles_ = 0;
    double loop_ = 1.0;
    double unhidden_jump_ = 0.0;
    std::vector<ParticlePose> poses_;   // frame-major, steps_ frames
};

BakedPoses bake(const ParticleSystem& sys);

// The converted ideal pose, for code that treats both kinds the same way.
SampledPose to_sampled(const ParticlePose& pose);

// Smallest box around every quad corner of every particle in every exported
// frame (hidden, parked particles included).
ParticleSystem::Bounds animation_bounds(const ParticleSystem& sys, const BakedPoses& baked);

} // namespace gs
