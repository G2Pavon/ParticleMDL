#include "Settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gs {

int quads_for(Geometry g) noexcept
{
    switch (g) {
    case Geometry::CrossedQuads: return 2;
    case Geometry::TriPlanarQuads: return 3;
    case Geometry::VolumetricBox: return 6;
    case Geometry::SingleQuad: break;
    }
    return 1;
}

int corners_for(Geometry g) noexcept
{
    return g == Geometry::VolumetricBox ? 8 : quads_for(g) * 4;
}

int bones_per_particle(Geometry g, ScaleMode s) noexcept
{
    return s == ScaleMode::VertexBone ? 1 + corners_for(g) : 1;
}

int max_particles(Geometry g, ScaleMode s) noexcept
{
    return kMaxBones / bones_per_particle(g, s);
}

void enforce_bone_limits(Settings& s) noexcept
{
    s.count = std::min(s.count, max_particles(s.geometry, s.scale_mode));
}

namespace {

template <typename T>
bool clamp_value(T& v, T lo, T hi)
{
    const T fixed = std::clamp(v, lo, hi);
    if (fixed == v)
        return false;
    v = fixed;
    return true;
}

// Non-finite numbers and out-of-range enums fall back to the default.
struct ResetInvalid {
    const Settings& defaults;
    bool changed = false;

    template <typename T>
    void operator()(std::string_view key, T& value)
    {
        if constexpr (std::is_same_v<T, double>) {
            if (!std::isfinite(value))
                reset(key, value);
        } else if constexpr (std::is_same_v<T, Vec3>) {
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                reset(key, value);
        } else if constexpr (std::is_enum_v<T>) {
            if (!enum_valid(value))
                reset(key, value);
        }
    }

    template <typename T>
    void reset(std::string_view key, T& value)
    {
        for_each_field(defaults, [&](std::string_view k, const auto& d) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(d)>, T>)
                if (k == key)
                    value = d;
        });
        changed = true;
    }
};

// The supported size closest to `size` (the smaller one on a tie).
int nearest_texture_size(int size)
{
    return *std::ranges::min_element(kTextureSizes, {}, [size](int s) { return std::abs(std::int64_t{s} - size); });
}

} // namespace

bool validate(Settings& s)
{
    const Settings defaults;
    ResetInvalid reset{defaults};
    for_each_field(s, reset);
    bool changed = reset.changed;

    changed |= clamp_value(s.count, 1, max_particles(s.geometry, s.scale_mode));
    changed |= clamp_value(s.duration, kMinDuration, kMaxDuration);
    changed |= clamp_value(s.lifespan, kMinLifespan, 1.0);
    changed |= clamp_value(s.fps, kMinFps, kMaxFps);
    changed |= clamp_value(s.burst_window, 0.0, kMaxBurstWindow);
    changed |= clamp_value(s.texture.hardness, kMinHardness, kMaxHardness);
    changed |= clamp_value(s.scale_start, 0.0, kMaxScale);
    changed |= clamp_value(s.scale_end, 0.0, kMaxScale);
    changed |= clamp_value(s.volume_width, 1.0, kMaxVolume);
    changed |= clamp_value(s.volume_depth, 1.0, kMaxVolume);
    changed |= clamp_value(s.volume_height, 1.0, kMaxVolume);
    changed |= clamp_value(s.quad_width, 1.0, kMaxQuadSize);
    changed |= clamp_value(s.quad_height, 1.0, kMaxQuadSize);
    if (const int size = nearest_texture_size(s.texture.size); size != s.texture.size) {
        s.texture.size = size;
        changed = true;
    }
    return changed;
}

Settings validated(Settings s)
{
    validate(s);
    return s;
}

Settings blank_settings()
{
    // State of the "New" button; every other field keeps its Settings default.
    return {
        .count = 1,
        .lifespan = 1.0,
        .volume_width = 64,
        .volume_depth = 64,
        .volume_height = 64,
        .speed = 0,
        .burst = 0,
        .spread = 0,
        .gravity = 0,
        .texture = {.core_color = {255, 255, 255}},
        .geometry = Geometry::SingleQuad,
    };
}

} // namespace gs
