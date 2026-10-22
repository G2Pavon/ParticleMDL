#include "SeedRoll.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <type_traits>
#include <vector>

#include "Exporter.hpp"

namespace gs {

namespace {

// Optional values are those whose rest value turns a feature off (no drag, no tilt, no cluster):
// a seed turns each on or leaves it off, at random, so not every effect it makes has every feature.
enum class Kind { Always, Optional };

// Every value a seed sets, with the range of its slider in the editor, in a fixed order: each
// value gets the same random numbers whatever the others are.
template <typename S, typename F>
void for_each_rolled(S& s, SeedSide side, F&& f)
{
    constexpr Kind always = Kind::Always, optional = Kind::Optional;
    if (side == SeedSide::System) {
        f(s.count, 1.0, static_cast<double>(max_particles(s.geometry, s.scale_mode)), always);
        f(s.duration, 0.5, 10.0, always);
        f(s.lifespan, 0.1, 1.0, always);
        f(s.lifespan_variance, 0.0, 0.9, optional);
        f(s.burst_window, 0.0, 1.0, always);
        f(s.volume_width, 1.0, kMaxVolume, always);
        f(s.volume_depth, 1.0, kMaxVolume, always);
        f(s.volume_height, 1.0, kMaxVolume, always);
        for (auto* v : {&s.velocity.x, &s.velocity.y, &s.velocity.z})
            f(*v, -500.0, 500.0, optional);
        f(s.emission_angle, 1.0, 360.0, always);
        f(s.spawn_radius, 0.0, 500.0, always);
        f(s.spawn_height, 0.0, 400.0, always);
        for (auto* v : {&s.emitter_tilt.x, &s.emitter_tilt.y, &s.emitter_tilt.z})
            f(*v, -180.0, 180.0, optional);
        f(s.speed, -200.0, 400.0, always);
        f(s.burst, -300.0, 300.0, always);
        f(s.spread, 0.0, 150.0, always);
        f(s.gravity, -600.0, 200.0, always);
        f(s.drag, 0.0, 5.0, optional);
        f(s.bounce, 0.0, 0.95, optional);
        f(s.floor_height, -300.0, 300.0, always);
        f(s.turbulence, 0.0, 200.0, optional);
        f(s.vortex_spin, -720.0, 720.0, optional);
        f(s.vortex_pull, -300.0, 300.0, optional);
    } else {
        f(s.scale_start, 0.1, 5.0, always);
        f(s.scale_end, 0.0, 5.0, always);
        f(s.quad_width, 1.0, 100.0, always);
        f(s.quad_height, 1.0, 100.0, always);
        f(s.size_variance, 0.0, 0.9, optional);
        f(s.cluster_size, 1.0, 16.0, optional);
        f(s.cluster_radius, 0.0, 200.0, always);
        for (auto* v : {&s.mesh_rotation.x, &s.mesh_rotation.y, &s.mesh_rotation.z})
            f(*v, -180.0, 180.0, optional);
        for (auto* v : {&s.particle_spin.x, &s.particle_spin.y, &s.particle_spin.z})
            f(*v, -360.0, 360.0, optional);
        f(s.spin_spread, 0.0, 360.0, optional);
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
    double unit() { return static_cast<double>(engine_() >> 11) * 0x1.0p-53; }   // [0, 1)

private:
    std::mt19937_64 engine_;
};

ExportReport report_for(Settings s)
{
    validate(s);
    return analyze_export(ParticleSystem(s));
}

// The largest value of `field` in [lo, hi] that the export accepts, the others staying as they are;
// lo if none does. More particles or copies only ever make an export larger, and about in
// proportion, so the search starts where the size at hi says the limit is and needs few exports.
int largest_that_exports(Settings s, int Settings::*field, int lo, int hi)
{
    auto fits = [&](int v) {
        s.*field = v;
        return report_for(s).blockers.empty();
    };
    s.*field = hi;
    const ExportReport at_hi = report_for(s);
    if (at_hi.blockers.empty())
        return hi;
    if (at_hi.frames > kMaxStudioFrames)
        return lo;   // the frame count does not depend on it
    double share = 1.0;
    if (at_hi.sequence_bytes > kMaxSequenceBytes)
        share = std::min(share, static_cast<double>(kMaxSequenceBytes) / static_cast<double>(at_hi.sequence_bytes));
    if (at_hi.vertices > kMaxStudioVertices)
        share = std::min(share, static_cast<double>(kMaxStudioVertices) / at_hi.vertices);
    if (at_hi.normals > kMaxStudioNormals)
        share = std::min(share, static_cast<double>(kMaxStudioNormals) / at_hi.normals);

    // good fits (or is lo, the least there is); bad does not fit.
    int good = lo, bad = hi;
    const int guess = std::clamp(static_cast<int>(hi * share), lo, hi - 1);
    const int step = std::max(1, hi / 32);
    if (fits(guess)) {
        good = guess;
        const int up = std::min(bad - 1, guess + step);
        if (up > good)
            (fits(up) ? good : bad) = up;
    } else {
        bad = guess;
        const int down = std::max(lo, guess - step);
        if (down < bad)
            (fits(down) ? good : bad) = down;
    }
    while (bad - good > 1) {
        const int mid = good + (bad - good) / 2;
        (fits(mid) ? good : bad) = mid;
    }
    return good;
}

} // namespace

void roll_settings(Settings& s, const Settings& base, SeedSide side, int seed)
{
    if (seed == 0) {
        std::vector<double> from;
        for_each_rolled(base, side, [&](const auto& v, double, double, Kind) { from.push_back(static_cast<double>(v)); });
        std::size_t i = 0;
        for_each_rolled(s, side, [&](auto& v, double, double, Kind) { v = static_cast<std::remove_reference_t<decltype(v)>>(from[i++]); });
    } else {
        RollRng rng(side, seed);
        // The particle count and the cluster size are what make an export too large: they are
        // drawn last, among the values that keep it within studiomdl's limits.
        int* const sized = side == SeedSide::System ? &s.count : &s.cluster_size;
        double sized_draw = 0.0;
        bool sized_on = true;
        for_each_rolled(s, side, [&](auto& v, double lo, double hi, Kind kind) {
            using T = std::remove_reference_t<decltype(v)>;
            const double on = rng.unit(), u = rng.unit();   // both drawn for every value, so each keeps its own
            const bool off = kind == Kind::Optional && on < 0.5;
            if (static_cast<void*>(&v) == static_cast<void*>(sized)) {
                sized_draw = u;
                sized_on = !off;
            }
            if (off)
                v = static_cast<T>(std::clamp(0.0, lo, hi));   // its rest value: 0, or no cluster
            else if constexpr (std::is_integral_v<T>)   // every whole number in the range equally likely
                v = static_cast<T>(std::min(lo + std::floor(u * (hi - lo + 1.0)), hi));
            else
                v = lo + u * (hi - lo);
        });
        if (sized_on) {
            const int lo = side == SeedSide::System ? 1 : 2;   // a cluster that is on has copies
            const int hi = side == SeedSide::System ? max_particles(s.geometry, s.scale_mode) : 16;
            const int fits = largest_that_exports(s, side == SeedSide::System ? &Settings::count : &Settings::cluster_size, lo, hi);
            *sized = std::min(lo + static_cast<int>(sized_draw * (fits - lo + 1)), fits);
        }
        // What the particle side adds (spin, a larger quad) can still be too much for the particles
        // the system has: then there are fewer of them, never more.
        if (side == SeedSide::Particle && !report_for(s).blockers.empty())
            s.count = largest_that_exports(s, &Settings::count, 1, s.count);
    }
    (side == SeedSide::System ? s.system_seed : s.particle_seed) = seed;
}

bool same_rolled_values(const Settings& a, const Settings& b, SeedSide side)
{
    std::vector<double> values;
    for_each_rolled(a, side, [&](const auto& v, double, double, Kind) { values.push_back(static_cast<double>(v)); });
    std::size_t i = 0;
    bool same = true;
    for_each_rolled(b, side, [&](const auto& v, double, double, Kind) { same = same && values[i++] == static_cast<double>(v); });
    return same;
}

} // namespace gs
