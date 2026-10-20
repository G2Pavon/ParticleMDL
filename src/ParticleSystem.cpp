#include "ParticleSystem.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>

namespace gs {

namespace {

std::vector<Quad> raw_quads(Geometry geo, double w, double h)
{
    auto V = [](double x, double y, double z, Vec3 n, int u, int v) {
        return QuadVertex{{x, y, z}, n, u, v};
    };
    const Vec3 px{1, 0, 0}, nx{-1, 0, 0}, py{0, 1, 0}, ny{0, -1, 0}, pz{0, 0, 1}, nz{0, 0, -1};

    const Quad front = {V(-w, 0, -h, py, 0, 1), V(w, 0, -h, py, 1, 1), V(w, 0, h, py, 1, 0), V(-w, 0, h, py, 0, 0)};
    const Quad side = {V(0, -w, -h, px, 0, 1), V(0, w, -h, px, 1, 1), V(0, w, h, px, 1, 0), V(0, -w, h, px, 0, 0)};
    const Quad flat = {V(-w, -w, 0, pz, 0, 1), V(w, -w, 0, pz, 1, 1), V(w, w, 0, pz, 1, 0), V(-w, w, 0, pz, 0, 0)};

    switch (geo) {
    case Geometry::CrossedQuads:
        return {front, side};
    case Geometry::TriPlanarQuads:
        return {front, side, flat};
    case Geometry::VolumetricBox:
        return {
            {V(-w, -w, -h, ny, 0, 1), V(w, -w, -h, ny, 1, 1), V(w, -w, h, ny, 1, 0), V(-w, -w, h, ny, 0, 0)},
            {V(w, w, -h, py, 0, 1), V(-w, w, -h, py, 1, 1), V(-w, w, h, py, 1, 0), V(w, w, h, py, 0, 0)},
            {V(-w, w, -h, nx, 0, 1), V(-w, -w, -h, nx, 1, 1), V(-w, -w, h, nx, 1, 0), V(-w, w, h, nx, 0, 0)},
            {V(w, -w, -h, px, 0, 1), V(w, w, -h, px, 1, 1), V(w, w, h, px, 1, 0), V(w, -w, h, px, 0, 0)},
            {V(-w, w, h, pz, 0, 1), V(w, w, h, pz, 1, 1), V(w, -w, h, pz, 1, 0), V(-w, -w, h, pz, 0, 0)},
            {V(-w, -w, -h, nz, 0, 1), V(w, -w, -h, nz, 1, 1), V(w, w, -h, nz, 1, 0), V(-w, w, -h, nz, 0, 0)},
        };
    case Geometry::SingleQuad:
        break;
    }
    return {front};
}

// Random numbers for particle seeding. std::mt19937_64's output sequence is
// fixed by the C++ standard; its doubles are made here rather than with the
// <random> distributions, whose results differ between standard libraries
// and would make exports depend on the compiler.
class ParticleRng {
public:
    explicit ParticleRng(std::uint64_t seed) : engine_(seed) {}
    double unit() { return static_cast<double>(engine_() >> 11) * 0x1.0p-53; }   // [0, 1)
    double signed_unit() { return 2.0 * unit() - 1.0; }             // [-1, 1)

private:
    std::mt19937_64 engine_;
};

constexpr std::uint64_t kParticleSeed = 0x5EED'0F'9A27'1C1Eull;

} // namespace

ParticleSystem::ParticleSystem(const Settings& settings) : settings_(validated(settings))
{
    const Settings& s = settings_;
    emit_rot_ = {radians(s.emitter_tilt.x), radians(s.emitter_tilt.y), radians(s.emitter_tilt.z)};
    has_micro_rotation_ = s.particle_spin != Vec3{} || s.spin_spread != 0.0;

    export_fps_ = s.fps;
    loop_steps_ = std::max(1, static_cast<int>(std::lround(s.duration * export_fps_)));

    // Quad geometry, pre-rotated by the static mesh rotation.
    const double mrx = radians(s.mesh_rotation.x), mry = radians(s.mesh_rotation.y), mrz = radians(s.mesh_rotation.z);
    quads_ = raw_quads(s.geometry, s.quad_width / 2.0, s.quad_height / 2.0);

    // Corners that are the same point before the rotation are the same corner after it.
    std::vector<Vec3> unrotated;
    for (const Quad& q : quads_) {
        std::array<int, 4> index{};
        for (std::size_t k = 0; k < 4; ++k) {
            const auto found = std::ranges::find(unrotated, q[k].pos);
            index[k] = static_cast<int>(found - unrotated.begin());
            if (found == unrotated.end())
                unrotated.push_back(q[k].pos);
        }
        corner_of_.push_back(index);
    }
    for (const Vec3& corner : unrotated)
        corners_.push_back(apply_static_rotation(corner, mrx, mry, mrz));

    for (Quad& q : quads_) {
        for (QuadVertex& v : q) {
            v.pos = apply_static_rotation(v.pos, mrx, mry, mrz);
            v.normal = apply_static_rotation(v.normal, mrx, mry, mrz);
        }
    }

    const bool vertex_bones = s.scale_mode == ScaleMode::VertexBone;
    corners_per_particle_ = vertex_bones ? static_cast<int>(corners_.size()) : 0;
    const int num_particles = std::min(s.count, kMaxBones / bones_per_particle(s.geometry, s.scale_mode));

    // Deterministic pseudo-random seeds: the same settings always give the same effect.
    particles_.reserve(num_particles);
    ParticleRng rng(kParticleSeed);
    int current_bone_id = 1;
    const double angle_rad = radians(s.emission_angle);
    for (int i = 0; i < num_particles; ++i) {
        Particle p;
        p.base_bone = current_bone_id;
        current_bone_id += 1 + corners_per_particle_;

        // The same draws in the same order for every particle, so a particle's
        // values do not depend on how many particles there are.
        const double direction = rng.unit();
        const double speed_jitter = rng.signed_unit();
        const double burst_jitter = rng.signed_unit();
        const double radius = std::sqrt(rng.unit());   // uniform over the disk's area
        const double height = rng.signed_unit();
        const double rot_jitter_x = rng.signed_unit();
        const double rot_jitter_y = rng.signed_unit();
        const double rot_jitter_z = rng.signed_unit();
        const Vec2 turb_freq{3.0 + rng.signed_unit() * 2.0, 2.0 + rng.signed_unit() * 2.0};
        const Vec2 turb_phase{rng.unit() * 2.0 * kPi, rng.unit() * 2.0 * kPi};

        // Births are evenly spaced over the loop for a steady flow.
        const double phase = static_cast<double>(i + 1) / num_particles;
        const double angle = (direction * angle_rad) - (angle_rad / 2.0);

        const double p_speed = s.speed + (speed_jitter * s.spread);
        Vec3 vel{std::cos(angle) * p_speed, std::sin(angle) * p_speed, s.burst + (burst_jitter * s.spread)};

        const double r_dist = s.hollow_ring ? s.spawn_radius : s.spawn_radius * radius;
        Vec3 spawn{std::cos(angle) * r_dist, std::sin(angle) * r_dist, s.spawn_height * height};

        if (emit_rot_.x != 0 || emit_rot_.y != 0 || emit_rot_.z != 0) {
            spawn = apply_static_rotation(spawn, emit_rot_.x, emit_rot_.y, emit_rot_.z);
            vel = apply_static_rotation(vel, emit_rot_.x, emit_rot_.y, emit_rot_.z);
        }

        vel = vel + s.velocity;

        p.spawn = spawn;
        p.velocity = vel;
        p.phase_offset = phase;
        p.spin = {radians(s.particle_spin.x) + radians(s.spin_spread) * rot_jitter_x,
                  radians(s.particle_spin.y) + radians(s.spin_spread) * rot_jitter_y,
                  radians(s.particle_spin.z) + radians(s.spin_spread) * rot_jitter_z};
        p.turb_freq = turb_freq;
        p.turb_phase = turb_phase;
        particles_.push_back(p);
    }

    // Fit the unscaled motion into volume_width x volume_depth x volume_height,
    // sampled at the exported frames: exactly the positions the game will show.
    constexpr double inf = std::numeric_limits<double>::infinity();
    double max_x = 0.001, max_y = 0.001;
    min_z_raw_ = inf;
    max_z_raw_ = -inf;
    for (int f = 0; f < loop_steps_; ++f) {   // the closing frame repeats frame 0
        const double ct = frame_time(f);
        for (const Particle& p : particles_) {
            const LifeState st = life_state(p, ct);
            if (!st.moved_by_motion())
                continue;
            const Vec3 m = motion(p, st.eval_time, {1.0, 1.0, 1.0});
            max_x = std::max(max_x, std::abs(m.x));
            max_y = std::max(max_y, std::abs(m.y));
            min_z_raw_ = std::min(min_z_raw_, m.z);
            max_z_raw_ = std::max(max_z_raw_, m.z);
        }
    }

    // No live sample at all leaves the range empty. Validated settings always
    // sample t=0, where the last particle is born, so this is a guard.
    if (min_z_raw_ > max_z_raw_)
        min_z_raw_ = max_z_raw_ = 0.0;
    if (max_z_raw_ == min_z_raw_)
        max_z_raw_ = min_z_raw_ + 0.1;
    volume_scale_ = {(s.volume_width / 2.0) / max_x, (s.volume_depth / 2.0) / max_y,
                     s.volume_height / (max_z_raw_ - min_z_raw_)};

    // A static quad (or box) extends at most max(w, h) * 0.87 from its bone.
    hide_z_ = min_z_raw_ * volume_scale_.z - std::max(s.quad_width, s.quad_height);
    drop_acceleration_ = std::max(std::abs(s.gravity * volume_scale_.z), 600.0);
}

Vec3 ParticleSystem::apply_static_rotation(Vec3 p, double rad_x, double rad_y, double rad_z)
{
    if (rad_x == 0 && rad_y == 0 && rad_z == 0)
        return p;

    const double cz = std::cos(rad_z), sz = std::sin(rad_z);
    const double cy = std::cos(rad_y), sy = std::sin(rad_y);
    const double cx = std::cos(rad_x), sx = std::sin(rad_x);

    const double x1 = p.x * cz - p.y * sz, y1 = p.x * sz + p.y * cz;
    const double z1 = p.z;
    const double x2 = x1 * cy + z1 * sy, z2 = -x1 * sy + z1 * cy;
    const double y2 = y1;
    const double y3 = y2 * cx - z2 * sx, z3 = y2 * sx + z2 * cx;
    return {x2, y3, z3};
}

Vec3 ParticleSystem::apply_macro_spin(Vec3 p, double eval_time) const
{
    const Settings& s = settings_;
    if (s.spin.x == 0 && s.spin.y == 0 && s.spin.z == 0)
        return p;
    return apply_static_rotation(p, eval_time * radians(s.spin.x), eval_time * radians(s.spin.y),
                                 eval_time * radians(s.spin.z));
}

LifeState ParticleSystem::life_state(const Particle& p, double loop_time) const
{
    LifeState st;
    const double loop = loop_duration();
    const double p_time = floor_mod(loop_time + (p.phase_offset * loop), loop);
    st.eval_time = p_time;
    if ((p_time / loop) <= settings_.lifespan)
        return st;

    st.phase = LifeState::Phase::Lingering;
    switch (settings_.end_behavior) {
    case EndBehavior::DropThroughFloor:
    case EndBehavior::HideUnderground:
    case EndBehavior::HideInCore:
        st.phase = LifeState::Phase::Dead;
        break;
    case EndBehavior::StopAndStay:
        st.eval_time = settings_.lifespan * loop;
        break;
    case EndBehavior::EndlessLoop:
        break;
    }
    return st;
}

long long ParticleSystem::generation(const Particle& p, double loop_time) const
{
    const double loop = loop_duration();
    const double x = loop_time + (p.phase_offset * loop);
    // x minus its position in the loop is a whole number of loops.
    return std::llround((x - floor_mod(x, loop)) / loop);
}

ParticlePose ParticleSystem::pose_at(const Particle& p, double loop_time) const
{
    const LifeState st = life_state(p, loop_time);
    const bool vertex_bones = settings_.scale_mode == ScaleMode::VertexBone;
    if (st.moved_by_motion())
        return {position_at(p, st.eval_time), rotation_at(p, st.eval_time),
                vertex_bones ? scale_factor_at(st.eval_time, st.past_lifespan()) : 1.0, true};

    // Parked out of sight. Bones have no scale, so only vertex-bone quads can
    // collapse; static quads rely on where they are parked.
    ParticlePose hidden{{0.0, 0.0, hide_z_}, {}, vertex_bones ? 0.0 : 1.0, false};
    switch (settings_.end_behavior) {
    case EndBehavior::HideInCore:
        hidden.pos = {};
        hidden.visible = !vertex_bones;   // a static quad stays visible at the center
        return hidden;
    case EndBehavior::DropThroughFloor: {
        const double death = settings_.lifespan * loop_duration();
        const Vec3 at_death = position_at(p, death);
        const double fall = st.eval_time - death;
        const double z = at_death.z - 0.5 * drop_acceleration_ * fall * fall;
        if (z <= hide_z_) {
            hidden.pos = {at_death.x, at_death.y, hide_z_};
            return hidden;
        }
        return {{at_death.x, at_death.y, z}, rotation_at(p, death), vertex_bones ? settings_.scale_end : 1.0, true};
    }
    default:   // Hide Underground
        return hidden;
    }
}

Vec3 ParticleSystem::motion(const Particle& p, double t, Vec3 scale) const
{
    const Settings& s = settings_;
    const double turb_x = std::sin((t * p.turb_freq.x) + p.turb_phase.x) * s.turbulence * t;
    const double turb_y = std::cos((t * p.turb_freq.y) + p.turb_phase.y) * s.turbulence * t;

    double x = p.spawn.x + (p.velocity.x * t) + turb_x;
    double y = p.spawn.y + (p.velocity.y * t) + turb_y;
    const double z = p.spawn.z + (p.velocity.z * t) + (0.5 * s.gravity * (t * t));

    // In the same (unscaled) units as every other motion parameter, so the
    // volume fit sees exactly the motion that is exported.
    if (s.vortex_spin != 0 || s.vortex_pull != 0) {
        double dist = std::hypot(x, y);
        double ang = std::atan2(y, x);
        dist = std::max(0.001, dist - (s.vortex_pull * t));
        ang += radians(s.vortex_spin) * t;
        x = std::cos(ang) * dist;
        y = std::sin(ang) * dist;
    }
    return {x * scale.x, y * scale.y, z * scale.z};
}

Vec3 ParticleSystem::position_at(const Particle& p, double t) const
{
    return apply_macro_spin(motion(p, t, volume_scale_), t);
}

Vec3 ParticleSystem::rotation_at(const Particle& p, double t) const
{
    if (!has_micro_rotation_)
        return {0.0, 0.0, 0.0};
    return {floor_mod(t * p.spin.x, kPi * 2), floor_mod(t * p.spin.y, kPi * 2), floor_mod(t * p.spin.z, kPi * 2)};
}

double ParticleSystem::scale_progress(double t) const noexcept
{
    return t / (settings_.lifespan * loop_duration());
}

double ParticleSystem::scale_factor_at(double t, bool dead) const noexcept
{
    if (dead)
        return settings_.scale_end;
    return settings_.scale_start + (settings_.scale_end - settings_.scale_start) * scale_progress(t);
}

} // namespace gs
