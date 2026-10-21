#pragma once

#include <array>
#include <vector>

#include "MathUtil.hpp"
#include "Settings.hpp"

namespace gs {

struct QuadVertex {
    Vec3 pos;
    Vec3 normal;
    int u = 0, v = 0;
};
using Quad = std::array<QuadVertex, 4>;

// One particle's seed values; trivially copyable.
struct Particle {
    int base_bone = 0;          // its corner bones (vertex bones) follow directly
    double phase_offset = 0;    // looping: birth time as a fraction of the loop
    double birth = 0;           // one-shot: when it starts, in seconds
    double lifespan = 1.0;      // fraction of the duration it lives
    double size = 1.0;          // multiplies the size of its quads
    Vec3 spawn;                 // unscaled start position
    Vec3 velocity;              // unscaled, per second
    Vec3 spin;                  // rad/s
    Vec2 turb_freq, turb_phase;   // of the sideways wobble, per axis
};

// Where a particle is in its life cycle at a given loop time.
struct LifeState {
    enum class Phase {
        Unborn,      // one-shot only: before its birth
        Alive,       // within the lifespan
        Lingering,   // past it, but still moved by the motion equations (Endless Loop, Stop & Stay)
        Dead,        // past it and handed to the end behavior (hidden, parked, falling)
    };
    Phase phase = Phase::Alive;
    double eval_time = 0;   // time fed into the motion equations

    bool past_lifespan() const noexcept { return phase == Phase::Lingering || phase == Phase::Dead; }
    bool moved_by_motion() const noexcept { return phase == Phase::Alive || phase == Phase::Lingering; }
    bool unborn() const noexcept { return phase == Phase::Unborn; }
};

// Everything needed to draw (or export) one particle at one moment.
struct ParticlePose {
    Vec3 pos;              // final, volume-scaled position
    Vec3 rot;              // euler angles for ParticleSystem::apply_static_rotation
    double scale = 1.0;    // quad scale (vertex bones); 0 collapses the quad
    bool visible = true;   // false: parked out of sight by its end behavior
};

// The particle simulation: deterministic particle seeds, a bounds
// pre-pass that fits the effect into the requested volume, and the per-frame
// evaluation shared by the preview and the exporter. Immutable once built.
class ParticleSystem {
public:
    // Works on a validated copy of the settings (see validate()).
    explicit ParticleSystem(const Settings& settings);

    // Rotates about Z, then Y, then X.
    static Vec3 apply_static_rotation(Vec3 p, double rad_x, double rad_y, double rad_z);
    // System rotation (deg/s) accumulated over eval_time.
    Vec3 apply_macro_spin(Vec3 p, double eval_time) const;

    LifeState life_state(const Particle& p, double loop_time) const;
    // Which life of the particle `loop_time` falls in. A time within one loop
    // of the particle's birth gives the same number; the next birth adds one.
    long long generation(const Particle& p, double loop_time) const;
    // The particle as drawn at `loop_time`, end behavior included. The preview
    // and the exporter both use this, so the game shows what the editor shows.
    ParticlePose pose_at(const Particle& p, double loop_time) const;
    // Final, volume-scaled position including vortex and system spin.
    Vec3 position_at(const Particle& p, double eval_time) const;
    // Per-particle euler angles (radians, wrapped to [0, 2pi)).
    Vec3 rotation_at(const Particle& p, double eval_time) const;
    // Fraction of a particle's life at eval_time, and its vertex-bone scale then.
    double scale_progress(const Particle& p, double eval_time) const noexcept;
    double scale_factor_at(const Particle& p, double eval_time, bool dead) const noexcept;

    // Box around points; see animation_bounds() in Baking.hpp.
    struct Bounds {
        Vec3 min, max;
    };

    const Settings& settings() const noexcept { return settings_; }
    const std::vector<Particle>& particles() const noexcept { return particles_; }
    const std::vector<Quad>& quads() const noexcept { return quads_; }
    // The quads of one particle, as seen from its bone at size 1: the geometry, or with a cluster all its copies.
    const std::vector<Quad>& quads_of(int particle) const noexcept
    {
        return cluster_quads_.empty() ? quads_ : cluster_quads_[static_cast<std::size_t>(particle)];
    }
    // Vertices studiomdl makes of the model: the distinct corners of every copy of the geometry on every particle.
    int vertex_count() const noexcept;
    // The distinct corner positions of the quads (rotated like them), in order of first use. With
    // vertex bones each has a bone of its own, and quads that meet at a corner share it.
    const std::vector<Vec3>& corners() const noexcept { return corners_; }
    // Which of corners() the given vertex of the given quad is.
    int corner_of(int quad, int vertex) const noexcept { return corner_of_[static_cast<std::size_t>(quad)][static_cast<std::size_t>(vertex)]; }
    int num_particles() const noexcept { return static_cast<int>(particles_.size()); }
    // Vertex-bone mode: bones that move each distinct quad corner, numbered right after
    // the particle's own bone; 0 without vertex bones.
    int corners_per_particle() const noexcept { return corners_per_particle_; }
    static int corner_bone(const Particle& p, int corner) noexcept { return p.base_bone + 1 + corner; }
    // Bones of the exported skeleton: the root (bone 0), then each particle's bone followed by its corner bones.
    int bone_count() const noexcept { return 1 + num_particles() * (1 + corners_per_particle_); }

    double loop_duration() const noexcept { return settings_.duration; }
    double export_fps() const noexcept { return export_fps_; }
    bool looping() const noexcept { return settings_.loop; }
    // Steps in the duration. GoldSrc plays a looping sequence over numframes - 1
    // steps and studiomdl overwrites its last frame with the first, so a looping
    // sequence holds one more frame that repeats frame 0. A one-shot sequence has
    // that one more frame as its real end, at the full duration.
    int loop_steps() const noexcept { return loop_steps_; }
    int export_frames() const noexcept { return loop_steps_ + 1; }
    // Time of an exported frame; in a loop the closing frame is frame 0 again.
    double frame_time(int frame) const noexcept
    {
        return settings_.loop ? (frame % loop_steps_) * loop_duration() / loop_steps_
                              : frame * loop_duration() / loop_steps_;
    }
    Vec3 emitter_rotation() const noexcept { return emit_rot_; }   // radians
    bool has_micro_rotation() const noexcept { return has_micro_rotation_; }

    // Z extent of the unscaled motion, and the scales that fit the motion
    // into vol_width x vol_height.
    double min_z_raw() const noexcept { return min_z_raw_; }
    double max_z_raw() const noexcept { return max_z_raw_; }
    Vec3 volume_scale() const noexcept { return volume_scale_; }
    // Height dead particles are parked at: below the lowest point of the
    // effect by more than a quad's size.
    double hide_z() const noexcept { return hide_z_; }
    // Downward acceleration of dead particles with Drop Through Floor.
    double drop_acceleration() const noexcept { return drop_acceleration_; }

private:
    // Linear motion + gravity + turbulence + vortex, then scaled (no system spin).
    Vec3 motion(const Particle& p, double t, Vec3 scale) const;

    Settings settings_;
    Vec3 emit_rot_;
    bool has_micro_rotation_ = false;
    int corners_per_particle_ = 0;
    std::vector<Vec3> corners_;
    std::vector<std::array<int, 4>> corner_of_;   // per quad and vertex
    double export_fps_ = 30.0;
    int loop_steps_ = 1;
    std::vector<Quad> quads_;
    std::vector<std::vector<Quad>> cluster_quads_;   // per particle, only with a cluster
    std::vector<Particle> particles_;
    double min_z_raw_ = 0, max_z_raw_ = 0;
    Vec3 volume_scale_{1, 1, 1};
    double hide_z_ = 0;
    double drop_acceleration_ = 0;
};

} // namespace gs
