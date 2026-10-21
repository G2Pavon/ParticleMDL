#include "ParticleSystem.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>

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
constexpr std::uint64_t kVarianceStream = 0xD1B54A32D192ED03ull;
constexpr std::uint64_t kClusterStream = 0x94D049BB133111EBull;

// Seed 0 is the sequence every effect has always had; other seeds are other sequences.
std::uint64_t seed_of(int seed)
{
    return kParticleSeed ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)) * 0x9E3779B97F4A7C15ull);
}

// The random numbers of one particle, in the order they have always been drawn. Effects made with
// one seed drew them all from one sequence; now the values of the system come from the sequence of
// the system seed and those of the particle from that of the particle seed, so equal seeds give the
// effect one seed gave, and each seed changes only its own side.
struct ParticleDraws {
    double direction, speed_jitter, burst_jitter, radius_draw, height;   // system: how it leaves
    Vec3 spin_jitter;                                                     // particle
    Vec2 turb_freq, turb_phase;                                           // system: its wobble

    explicit ParticleDraws(ParticleRng& rng)
        : direction(rng.unit()), speed_jitter(rng.signed_unit()), burst_jitter(rng.signed_unit()), radius_draw(rng.unit()),
          height(rng.signed_unit())
    {
        spin_jitter.x = rng.signed_unit();
        spin_jitter.y = rng.signed_unit();
        spin_jitter.z = rng.signed_unit();
        turb_freq.x = 3.0 + rng.signed_unit() * 2.0;
        turb_freq.y = 2.0 + rng.signed_unit() * 2.0;
        turb_phase.x = rng.unit() * 2.0 * kPi;
        turb_phase.y = rng.unit() * 2.0 * kPi;
    }
};

// The draws added later, from a sequence of their own so the ones above stayed the same.
struct VarianceDraws {
    double lifespan;   // system
    double size;       // particle

    explicit VarianceDraws(ParticleRng& rng) : lifespan(rng.unit()), size(rng.signed_unit()) {}
};

} // namespace

ParticleSystem::ParticleSystem(const Settings& settings) : settings_(validated(settings))
{
    const Settings& s = settings_;
    emit_rot_ = {radians(s.emitter_tilt.x), radians(s.emitter_tilt.y), radians(s.emitter_tilt.z)};
    has_micro_rotation_ = s.particle_spin != Vec3{} || s.spin_spread != 0.0 || s.align_to_velocity;

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
    const int num_particles = std::min(s.count, kMaxBones / bones_per_particle(s.geometry, s.scale_mode));
    bones_ = BoneLayout(num_particles, vertex_bones ? static_cast<int>(corners_.size()) : 0);
    // bones_per_particle() is what the settings are checked with before there is a system.
    assert(1 + bones_.corners_per_particle() == bones_per_particle(s.geometry, s.scale_mode));

    // Deterministic pseudo-random seeds: the same settings always give the same effect.
    particles_.reserve(num_particles);
    ParticleRng system_rng(seed_of(s.system_seed)), particle_rng(seed_of(s.particle_seed));
    ParticleRng system_extra(seed_of(s.system_seed) ^ kVarianceStream), particle_extra(seed_of(s.particle_seed) ^ kVarianceStream);
    const double angle_rad = radians(s.emission_angle);
    for (int i = 0; i < num_particles; ++i) {
        Particle p;

        // The same draws in the same order for every particle, so a particle's
        // values do not depend on how many particles there are.
        const ParticleDraws system(system_rng), own(particle_rng);
        const VarianceDraws system_variance(system_extra), own_variance(particle_extra);
        const double direction = system.direction;
        const double speed_jitter = system.speed_jitter;
        const double burst_jitter = system.burst_jitter;
        const double radius_draw = system.radius_draw;
        const double radius = std::sqrt(radius_draw);   // uniform over the disk's area
        const double height = system.height;

        // Births are evenly spaced over the loop for a steady flow; a one-shot effect is born in a burst.
        const double phase = s.loop ? static_cast<double>(i + 1) / num_particles : 0.0;
        p.birth = s.loop ? 0.0 : s.burst_window * s.duration * i / num_particles;
        const double p_speed = s.speed + (speed_jitter * s.spread);
        const double p_burst = s.burst + (burst_jitter * s.spread);

        Vec3 spawn, vel;
        switch (s.emitter_shape) {
        case EmitterShape::Disk: {
            const double angle = (direction * angle_rad) - (angle_rad / 2.0);
            vel = {std::cos(angle) * p_speed, std::sin(angle) * p_speed, p_burst};
            const double r_dist = s.hollow_ring ? s.spawn_radius : s.spawn_radius * radius;
            spawn = {std::cos(angle) * r_dist, std::sin(angle) * r_dist, s.spawn_height * height};
            break;
        }
        case EmitterShape::Sphere:
        case EmitterShape::Hemisphere: {
            // A direction uniform over the sphere: the height is uniform in [-1, 1], the angle around it too.
            const double up = s.emitter_shape == EmitterShape::Hemisphere ? std::abs(height) : height;
            const double ring = std::sqrt(1.0 - up * up), around = direction * 2.0 * kPi;
            const Vec3 out{ring * std::cos(around), ring * std::sin(around), up};
            const double r_dist = s.hollow_ring ? s.spawn_radius : s.spawn_radius * std::cbrt(radius_draw);   // uniform in volume
            spawn = out * r_dist;
            vel = out * p_speed + Vec3{0, 0, p_burst};
            break;
        }
        case EmitterShape::Cone: {
            // A direction uniform over the cap of half-angle emission_angle / 2 around +Z.
            const double half = std::min(angle_rad / 2.0, kPi);
            const double up = 1.0 - ((height + 1.0) / 2.0) * (1.0 - std::cos(half));
            const double ring = std::sqrt(std::max(0.0, 1.0 - up * up)), around = direction * 2.0 * kPi;
            const Vec3 out{ring * std::cos(around), ring * std::sin(around), up};
            const double r_dist = s.hollow_ring ? s.spawn_radius : s.spawn_radius * radius;
            spawn = {std::cos(around) * r_dist, std::sin(around) * r_dist, 0.0};
            vel = out * p_speed + Vec3{0, 0, p_burst};
            break;
        }
        case EmitterShape::Box: {
            spawn = {s.spawn_radius * (2.0 * direction - 1.0), s.spawn_radius * (2.0 * radius_draw - 1.0),
                     s.spawn_height * height};
            // Away from the middle; straight up when it starts exactly in it.
            const double away = (spawn.x != 0.0 || spawn.y != 0.0) ? std::atan2(spawn.y, spawn.x) : direction * 2.0 * kPi;
            vel = {std::cos(away) * p_speed, std::sin(away) * p_speed, p_burst};
            break;
        }
        case EmitterShape::Line: {
            const double fan = (radius_draw - 0.5) * angle_rad;   // around +Y
            spawn = {s.spawn_radius * (2.0 * direction - 1.0), 0.0, 0.0};
            vel = {std::sin(fan) * p_speed, std::cos(fan) * p_speed, p_burst};
            break;
        }
        }

        if (emit_rot_.x != 0 || emit_rot_.y != 0 || emit_rot_.z != 0) {
            spawn = apply_static_rotation(spawn, emit_rot_.x, emit_rot_.y, emit_rot_.z);
            vel = apply_static_rotation(vel, emit_rot_.x, emit_rot_.y, emit_rot_.z);
        }

        vel = vel + s.velocity;

        p.spawn = spawn;
        p.velocity = vel;
        p.phase_offset = phase;
        p.spin = {radians(s.particle_spin.x) + radians(s.spin_spread) * own.spin_jitter.x,
                  radians(s.particle_spin.y) + radians(s.spin_spread) * own.spin_jitter.y,
                  radians(s.particle_spin.z) + radians(s.spin_spread) * own.spin_jitter.z};
        p.turb_freq = system.turb_freq;
        p.turb_phase = system.turb_phase;
        p.lifespan = std::max(s.lifespan * (1.0 - s.lifespan_variance * system_variance.lifespan), kMinLifespan);
        p.size = 1.0 + s.size_variance * own_variance.size;
        particles_.push_back(p);
    }

    // Clusters: each particle carries several copies of the quads, each moved and turned at random.
    if (s.cluster_size > 1 && !vertex_bones) {
        ParticleRng cluster(seed_of(s.particle_seed) ^ kClusterStream);
        cluster_quads_.resize(particles_.size());
        for (std::vector<Quad>& quads : cluster_quads_) {
            quads.reserve(quads_.size() * static_cast<std::size_t>(s.cluster_size));
            for (int copy = 0; copy < s.cluster_size; ++copy) {
                // A point uniform inside the ball, and a turn about every axis.
                const double ring_around = cluster.unit() * 2.0 * kPi, up = cluster.signed_unit();
                const double reach = s.cluster_radius * std::cbrt(cluster.unit());
                const double ring = std::sqrt(1.0 - up * up);
                const Vec3 offset = Vec3{ring * std::cos(ring_around), ring * std::sin(ring_around), up} * reach;
                const double turn_x = cluster.unit() * 2.0 * kPi, turn_y = cluster.unit() * 2.0 * kPi,
                             turn_z = cluster.unit() * 2.0 * kPi;
                for (Quad quad : quads_) {
                    for (QuadVertex& v : quad) {
                        v.pos = apply_static_rotation(v.pos, turn_x, turn_y, turn_z) + offset;
                        v.normal = apply_static_rotation(v.normal, turn_x, turn_y, turn_z);
                    }
                    quads.push_back(quad);
                }
            }
        }
    }

    // Fit the unscaled motion into volume_width x volume_depth x volume_height,
    // sampled at the exported frames: exactly the positions the game will show.
    constexpr double inf = std::numeric_limits<double>::infinity();
    double max_x = 0.001, max_y = 0.001;
    min_z_raw_ = inf;
    max_z_raw_ = -inf;
    const int fitted_frames = s.loop ? loop_steps_ : loop_steps_ + 1;   // a loop's closing frame repeats frame 0
    for (int f = 0; f < fitted_frames; ++f) {
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
    hide_z_ = min_z_raw_ * volume_scale_.z -
              (std::max(s.quad_width, s.quad_height) * (1.0 + s.size_variance) + (cluster_quads_.empty() ? 0.0 : s.cluster_radius));
    drop_acceleration_ = std::max(std::abs(s.gravity * volume_scale_.z), 600.0);
}

int ParticleSystem::vertex_count() const noexcept
{
    const int copies = cluster_quads_.empty() ? 1 : settings_.cluster_size;
    return num_particles() * copies * corners_for(settings_.geometry);
}

int ParticleSystem::normal_count() const
{
    // studiomdl keeps a normal for each bone and direction, in the order it reads them, and takes a
    // direction less than 2 degrees from one it has on the same bone for that one (Sven Co-op's
    // studiomdl: 1.9 degrees apart gives one normal, 2.1 two). A back face reuses its front's normal.
    const double same = std::cos(radians(2.0));
    std::vector<std::pair<int, Vec3>> kept;
    auto count = [&](const std::vector<Quad>& quads) {
        kept.clear();
        for (int qi = 0; qi < static_cast<int>(quads.size()); ++qi)
            for (int k = 0; k < 4; ++k) {
                const int bone = bones_.vertex_bones() ? corner_of(qi, k) : 0;   // which of this particle's bones
                const Vec3 n = quads[static_cast<std::size_t>(qi)][static_cast<std::size_t>(k)].normal;
                const bool known = std::ranges::any_of(kept, [&](const std::pair<int, Vec3>& e) {
                    return e.first == bone && e.second.x * n.x + e.second.y * n.y + e.second.z * n.z > same;
                });
                if (!known)
                    kept.emplace_back(bone, n);
            }
        return static_cast<int>(kept.size());
    };
    if (cluster_quads_.empty())
        return num_particles() * count(quads_);   // every particle has the same quads on its own bones
    int total = 0;
    for (const std::vector<Quad>& quads : cluster_quads_)
        total += count(quads);
    return total;
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
    double p_time;
    if (settings_.loop) {
        p_time = floor_mod(loop_time + (p.phase_offset * loop), loop);
    } else {
        p_time = loop_time - p.birth;   // no wrap: it is born once
        if (p_time < 0.0) {
            st.phase = LifeState::Phase::Unborn;
            return st;
        }
    }
    st.eval_time = p_time;
    if ((p_time / loop) <= p.lifespan)
        return st;

    st.phase = LifeState::Phase::Lingering;
    switch (settings_.end_behavior) {
    case EndBehavior::DropThroughFloor:
    case EndBehavior::HideUnderground:
    case EndBehavior::HideInCore:
        st.phase = LifeState::Phase::Dead;
        break;
    case EndBehavior::StopAndStay:
        st.eval_time = p.lifespan * loop;
        break;
    case EndBehavior::EndlessLoop:
        break;
    }
    return st;
}

long long ParticleSystem::generation(const Particle& p, double loop_time) const
{
    if (!settings_.loop)
        return 0;   // born once
    const double loop = loop_duration();
    const double x = loop_time + (p.phase_offset * loop);
    // x minus its position in the loop is a whole number of loops.
    return std::llround((x - floor_mod(x, loop)) / loop);
}

ParticlePose ParticleSystem::pose_at(const Particle& p, double loop_time) const
{
    const LifeState st = life_state(p, loop_time);
    const bool vertex_bones = settings_.scale_mode == ScaleMode::VertexBone;
    if (st.unborn())
        return {{0.0, 0.0, hide_z_}, {}, vertex_bones ? 0.0 : p.size, false};   // waits out of sight, whatever the end behavior
    if (st.moved_by_motion())
        return {position_at(p, st.eval_time), rotation_at(p, st.eval_time),
                vertex_bones ? scale_factor_at(p, st.eval_time, st.past_lifespan()) * p.size : p.size, true};

    // Parked out of sight. Bones have no scale, so only vertex-bone quads can
    // collapse; static quads rely on where they are parked. The quad keeps the turn it
    // died with, so it does not turn on its way down (and a constant costs little to store).
    const double death = p.lifespan * loop_duration();
    ParticlePose hidden{{0.0, 0.0, hide_z_}, rotation_at(p, death), vertex_bones ? 0.0 : p.size, false};
    switch (settings_.end_behavior) {
    case EndBehavior::HideInCore:
        hidden.pos = {};
        hidden.visible = !vertex_bones;   // a static quad stays visible at the center
        return hidden;
    case EndBehavior::DropThroughFloor: {
        const Vec3 at_death = position_at(p, death);
        const double fall = st.eval_time - death;
        const double z = at_death.z - 0.5 * drop_acceleration_ * fall * fall;
        if (z <= hide_z_) {
            hidden.pos = {at_death.x, at_death.y, hide_z_};
            return hidden;
        }
        return {{at_death.x, at_death.y, z}, hidden.rot, vertex_bones ? settings_.scale_end * p.size : p.size, true};
    }
    default:   // Hide Underground
        return hidden;
    }
}

namespace {

// Height and vertical velocity after flying freely for `tau`: gravity, and a drag that works on the velocity.
struct Flight {
    double z, v;
};

Flight fly(double z0, double v0, double gravity, double drag, double tau)
{
    if (drag > 0.0) {
        // The closed form of dv/dt = g - k v, as in ParticleSystem::motion().
        const double k = drag, x_kt = k * tau;
        const double reach = -std::expm1(-x_kt) / k;
        const double deficit = x_kt < 0.05 ? tau * (x_kt / 2.0 - x_kt * x_kt / 6.0 + x_kt * x_kt * x_kt / 24.0 -
                                                    x_kt * x_kt * x_kt * x_kt / 120.0 +
                                                    x_kt * x_kt * x_kt * x_kt * x_kt / 720.0)
                                             : tau - reach;
        return {z0 + (v0 * reach) + (gravity / k) * deficit, gravity / k + (v0 - gravity / k) * std::exp(-x_kt)};
    }
    return {z0 + (v0 * tau) + (0.5 * gravity * (tau * tau)), v0 + gravity * tau};
}

// The height at time t of a particle that bounces off a floor: after each hit it keeps `bounce` of its
// speed, upward. It lies still on the floor once a bounce is too small to see.
double bounced_height(double z0, double v0, double gravity, double drag, double t, double floor, double bounce)
{
    if (z0 < floor)
        return fly(z0, v0, gravity, drag, t).z;   // it starts under the floor: there is nothing to hit
    constexpr int kMaxBounces = 64;
    constexpr double kRestSpeed = 0.5;
    double z = z0, v = v0, remaining = t;
    for (int hit = 0; hit < kMaxBounces; ++hit) {
        if (fly(z, v, gravity, drag, remaining).z >= floor)
            return fly(z, v, gravity, drag, remaining).z;
        // It reaches the floor within `remaining`: find when (the height crosses the floor once, downward).
        double lo = 0.0, hi = remaining;
        for (int i = 0; i < 80; ++i) {
            const double mid = (lo + hi) / 2.0;
            (fly(z, v, gravity, drag, mid).z >= floor ? lo : hi) = mid;
        }
        const double arrival = hi;
        const double speed = -fly(z, v, gravity, drag, arrival).v * bounce;   // upward after the hit
        remaining -= arrival;
        z = floor;
        v = speed;
        if (speed < kRestSpeed)
            return floor;
    }
    return floor;
}

} // namespace

Vec3 ParticleSystem::motion(const Particle& p, double t, Vec3 scale) const
{
    const Settings& s = settings_;
    const double turb_x = std::sin((t * p.turb_freq.x) + p.turb_phase.x) * s.turbulence * t;
    const double turb_y = std::cos((t * p.turb_freq.y) + p.turb_phase.y) * s.turbulence * t;

    double x, y, z;
    if (s.drag > 0.0) {
        // dv/dt = gravity - drag * v, whose position has a closed form: v(t) = g/k + (v0 - g/k) e^(-kt),
        // so x(t) = x0 + g/k * t + (v0 - g/k) * (1 - e^(-kt)) / k. It is written as
        //   x0 + v0 * reach + g/k * (t - reach),   reach = (1 - e^(-kt)) / k,
        // because for a small drag g/k is huge and the two terms of the first form cancel. Both
        // reach and t - reach are computed without that cancellation.
        const double k = s.drag, x_kt = k * t;
        const double reach = -std::expm1(-x_kt) / k;
        const double deficit = x_kt < 0.05 ? t * (x_kt / 2.0 - x_kt * x_kt / 6.0 + x_kt * x_kt * x_kt / 24.0 -
                                                  x_kt * x_kt * x_kt * x_kt / 120.0 +
                                                  x_kt * x_kt * x_kt * x_kt * x_kt / 720.0)
                                           : t - reach;
        x = p.spawn.x + (p.velocity.x * reach) + turb_x;
        y = p.spawn.y + (p.velocity.y * reach) + turb_y;
        z = p.spawn.z + (p.velocity.z * reach) + (s.gravity / k) * deficit;
    } else {
        x = p.spawn.x + (p.velocity.x * t) + turb_x;
        y = p.spawn.y + (p.velocity.y * t) + turb_y;
        z = p.spawn.z + (p.velocity.z * t) + (0.5 * s.gravity * (t * t));
    }

    if (s.bounce > 0.0)
        z = bounced_height(p.spawn.z, p.velocity.z, s.gravity, s.drag, t, s.floor_height, s.bounce);

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
    if (settings_.align_to_velocity) {
        // Z along the direction of travel, taken from the motion that is shown (the system's spin included).
        const double h = loop_duration() / (static_cast<double>(loop_steps_) * 16.0);
        const double before = std::max(0.0, t - h);
        const Vec3 d = position_at(p, t + h) - position_at(p, before);
        const double speed = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const Quat roll = axis_rotation(t * p.spin.z, 2);
        const Quat aligned = speed < 1e-12 ? roll : quat_from_to({0, 0, 1}, d * (1.0 / speed)) * roll;
        const Vec3 angles = euler_from_quat(aligned);
        return {floor_mod(angles.x, kPi * 2), floor_mod(angles.y, kPi * 2), floor_mod(angles.z, kPi * 2)};
    }
    return {floor_mod(t * p.spin.x, kPi * 2), floor_mod(t * p.spin.y, kPi * 2), floor_mod(t * p.spin.z, kPi * 2)};
}

double ParticleSystem::scale_progress(const Particle& p, double t) const noexcept
{
    return t / (p.lifespan * loop_duration());
}

double ParticleSystem::scale_factor_at(const Particle& p, double t, bool dead) const noexcept
{
    if (dead)
        return settings_.scale_end;
    return settings_.scale_start + (settings_.scale_end - settings_.scale_start) * scale_progress(p, t);
}

} // namespace gs
