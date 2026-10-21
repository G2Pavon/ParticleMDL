#include "SeedRoll.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <type_traits>
#include <vector>

namespace gs {

namespace {

// Every value a seed varies, with the range of its slider in the editor, in a fixed order: each
// value gets the same random number whatever the others are. `whole` marks a value whose top
// means something of its own (a fan all around, a particle that never dies): there it stays.
constexpr bool whole = true;
template <typename S, typename F>
void for_each_rolled(S& s, SeedSide side, F&& f)
{
    if (side == SeedSide::System) {
        f(s.count, 1.0, static_cast<double>(max_particles(s.geometry, s.scale_mode)));
        f(s.duration, 0.5, 10.0);
        f(s.lifespan, 0.1, 1.0, whole);
        f(s.lifespan_variance, 0.0, 0.9);
        f(s.burst_window, 0.0, 1.0);
        f(s.volume_width, 1.0, 1024.0);
        f(s.volume_depth, 1.0, 1024.0);
        f(s.volume_height, 1.0, 1024.0);
        for (auto* v : {&s.velocity.x, &s.velocity.y, &s.velocity.z})
            f(*v, -500.0, 500.0);
        for (auto* v : {&s.spin.x, &s.spin.y, &s.spin.z})
            f(*v, -360.0, 360.0);
        f(s.emission_angle, 1.0, 360.0, whole);
        f(s.spawn_radius, 0.0, 500.0);
        f(s.spawn_height, 0.0, 400.0);
        for (auto* v : {&s.emitter_tilt.x, &s.emitter_tilt.y, &s.emitter_tilt.z})
            f(*v, -180.0, 180.0);
        f(s.speed, -200.0, 400.0);
        f(s.burst, -300.0, 300.0);
        f(s.spread, 0.0, 150.0);
        f(s.gravity, -600.0, 200.0);
        f(s.drag, 0.0, 5.0);
        f(s.bounce, 0.0, 0.95);
        f(s.floor_height, -300.0, 300.0);
        f(s.turbulence, 0.0, 200.0);
        f(s.vortex_spin, -720.0, 720.0);
        f(s.vortex_pull, -300.0, 300.0);
    } else {
        f(s.scale_start, 0.1, 5.0);
        f(s.scale_end, 0.0, 5.0);
        f(s.quad_width, 1.0, 100.0);
        f(s.quad_height, 1.0, 100.0);
        f(s.size_variance, 0.0, 0.9);
        f(s.cluster_size, 1.0, 16.0);
        f(s.cluster_radius, 0.0, 200.0);
        for (auto* v : {&s.mesh_rotation.x, &s.mesh_rotation.y, &s.mesh_rotation.z})
            f(*v, -180.0, 180.0);
        for (auto* v : {&s.particle_spin.x, &s.particle_spin.y, &s.particle_spin.z})
            f(*v, -360.0, 360.0);
        f(s.spin_spread, 0.0, 360.0);
    }
}

// The same fixed-by-the-standard engine as the particles, with its doubles made by hand.
class RollRng {
public:
    RollRng(SeedSide side, int seed)
        : engine_((side == SeedSide::System ? 0x51D3'0C0F'FEE5'A11Dull : 0x9A27'1C1E'5EED'0B0Bull) ^
                  static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)) * 0x9E3779B97F4A7C15ull)
    {
    }
    double signed_unit() { return 2.0 * (static_cast<double>(engine_() >> 11) * 0x1.0p-53) - 1.0; }   // [-1, 1)

private:
    std::mt19937_64 engine_;
};

// Clusters of one copy are off, like a value at 0.
bool in_use(int v) noexcept { return v > 1; }
bool in_use(double v) noexcept { return v != 0.0; }

} // namespace

void roll_settings(Settings& s, const Settings& base, SeedSide side, int seed)
{
    std::vector<double> from;
    for_each_rolled(base, side, [&](const auto& v, double, double, bool = false) { from.push_back(static_cast<double>(v)); });

    RollRng rng(side, seed);
    std::size_t i = 0;
    for_each_rolled(s, side, [&](auto& v, double lo, double hi, bool whole_at_top = false) {
        using T = std::remove_reference_t<decltype(v)>;
        const double was = from[i++];
        const double r = rng.signed_unit();   // drawn for every value, so each keeps its number
        if (seed == 0 || !in_use(static_cast<T>(was)) || (whole_at_top && was >= hi)) {
            v = static_cast<T>(was);
            return;
        }
        const double varied = std::clamp(was + r * kRollAmount * std::abs(was), lo, hi);
        if constexpr (std::is_integral_v<T>)
            v = static_cast<T>(std::max(std::lround(varied), static_cast<long>(lo)));
        else
            v = varied;
    });
    (side == SeedSide::System ? s.system_seed : s.particle_seed) = seed;
}

bool same_rolled_values(const Settings& a, const Settings& b, SeedSide side)
{
    std::vector<double> values;
    for_each_rolled(a, side, [&](const auto& v, double, double, bool = false) { values.push_back(static_cast<double>(v)); });
    std::size_t i = 0;
    bool same = true;
    for_each_rolled(b, side, [&](const auto& v, double, double, bool = false) { same = same && values[i++] == static_cast<double>(v); });
    return same;
}

} // namespace gs
