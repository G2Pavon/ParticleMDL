#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>

#include "Enums.hpp"
#include "MathUtil.hpp"

namespace gs {

inline constexpr std::array<int, 4> kTextureSizes = {64, 128, 256, 512};

// studiomdl refuses a model with MAXSTUDIOBONES (128) bones, so 127 at most, all of them the
// particles' (there is no shared root bone).
inline constexpr int kMaxBones = 127;

struct Rgb {
    std::uint8_t r = 255, g = 255, b = 255;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// Everything the sprite texture is made from. Two equal values give the same texture,
// so comparing them tells whether a texture has to be made again.
struct TextureSettings {
    TexPattern pattern = TexPattern::RadialOrb;
    ColorMode color_mode = ColorMode::Solid;
    Rgb core_color{255, 170, 85};
    Rgb edge_color{0, 0, 0};
    int size = 64;
    double hardness = 0.8;
    std::string file{};   // imported image, a UTF-8 path; empty: the procedural pattern above
    // The game draws it additively: its colour is added to what is behind, so black is invisible.

    friend bool operator==(const TextureSettings&, const TextureSettings&) = default;
};

// Every value the editor exposes. Angles are in degrees, distances in model
// units, times in seconds.
struct Settings {
    std::string model_name = "custom_particles";
    // Written to the .qc as $scale: studiomdl multiplies the whole model, its animation included, by
    // it. An export setting like the name: presets and File > New leave it as it is.
    double model_scale = 1.0;

    // Animation
    int count = 64;
    double duration = 3.0;   // one loop
    double lifespan = 0.85;  // fraction of the loop a particle lives
    int fps = 30;            // exported frames per second
    EndBehavior end_behavior = EndBehavior::HideUnderground;
    // Looping: particles are born evenly over the loop and the sequence repeats. Without it the
    // effect plays once (an explosion, an impact): particles are born in the first burst_window of
    // the duration (0: all at the start) and the sequence ends on its last frame.
    bool loop = true;
    double burst_window = 0;   // fraction of the duration, one-shot only
    // Which random numbers the effect uses: the system seed for where and how the particles leave,
    // their wobble and their lifespans; the particle seed for their spin, size and cluster copies.
    // 0 and 0 are the original effect; any other number gives other values for that side alone.
    // In the editor a seed also varies the values of its panel (see SeedRoll.hpp).
    int system_seed = 0;
    int particle_seed = 0;
    double lifespan_variance = 0;     // each particle lives up to this fraction shorter, at random
    double size_variance = 0;         // each particle is up to this fraction larger or smaller, at random

    // Volume the motion is fitted into
    double volume_width = 128;   // X
    double volume_depth = 128;   // Y
    double volume_height = 256;  // Z

    // Whole system
    Vec3 velocity{}; // added to every particle, units/s
    Vec3 spin{};     // deg/s

    // Emitter: particles start on a disk of spawn_radius (on its edge with hollow_ring), spread
    // over +-spawn_height, and leave in the horizontal fan of emission_angle around +X, the whole
    // tilted by emitter_tilt.
    double emission_angle = 360;   // horizontal fan
    double spawn_radius = 0;
    double spawn_height = 0;
    Vec3 emitter_tilt{};
    bool hollow_ring = false;

    // Physics
    double speed = 60;    // horizontal, units/s
    double burst = 120;   // vertical, units/s
    double spread = 30;   // random +- on speed and burst
    double gravity = -150;
    double drag = 0;          // 1/s: how fast the velocity dies away
    double bounce = 0;        // 0: no floor. Otherwise the share of its speed a particle keeps when it hits the floor
    double floor_height = 0;  // where that floor is, in the units of the motion
    double turbulence = 0;
    double vortex_spin = 0;   // deg/s
    double vortex_pull = 0;   // units/s toward the axis

    // Texture
    TextureSettings texture;

    // Vertex-bone scaling over a particle's life
    ScaleMode scale_mode = ScaleMode::Static;
    double scale_start = 1.0;
    double scale_end = 1.0;

    // Geometry
    Geometry geometry = Geometry::CrossedQuads;
    double quad_width = 6;
    double quad_height = 6;
    bool double_sided = true;
    // A cluster puts this many copies of the geometry on each particle's bone, spread at random
    // inside cluster_radius and turned at random: many quads for the price of one bone. The
    // copies move together, so it needs static quads (vertex bones scale each quad on its own).
    int cluster_size = 1;
    double cluster_radius = 20;
    Vec3 mesh_rotation{};

    // Per-particle spin
    // The quad's Z axis points the way the particle travels (sparks, rain, tracers). Spin Z then
    // rolls it around that direction; Spin X and Y have no effect.
    bool align_to_velocity = false;
    Vec3 particle_spin{};     // deg/s
    double spin_spread = 0;   // random +-, deg/s

    friend bool operator==(const Settings&, const Settings&) = default;
};

// Every setting with its key in settings files, in file order: the one list
// the JSON reader and writer and validate() work from.
template <typename S, typename F>
    requires std::same_as<std::remove_const_t<S>, Settings>
void for_each_field(S& s, F&& f)
{
    f("model_name", s.model_name);
    f("model_scale", s.model_scale);
    f("count", s.count);
    f("duration", s.duration);
    f("lifespan", s.lifespan);
    f("fps", s.fps);
    f("end_behavior", s.end_behavior);
    f("loop", s.loop);
    f("burst_window", s.burst_window);
    f("system_seed", s.system_seed);
    f("particle_seed", s.particle_seed);
    f("lifespan_variance", s.lifespan_variance);
    f("size_variance", s.size_variance);
    f("volume_width", s.volume_width);
    f("volume_depth", s.volume_depth);
    f("volume_height", s.volume_height);
    f("velocity", s.velocity);
    f("spin", s.spin);
    f("emission_angle", s.emission_angle);
    f("spawn_radius", s.spawn_radius);
    f("spawn_height", s.spawn_height);
    f("emitter_tilt", s.emitter_tilt);
    f("hollow_ring", s.hollow_ring);
    f("speed", s.speed);
    f("burst", s.burst);
    f("spread", s.spread);
    f("gravity", s.gravity);
    f("drag", s.drag);
    f("bounce", s.bounce);
    f("floor_height", s.floor_height);
    f("turbulence", s.turbulence);
    f("vortex_spin", s.vortex_spin);
    f("vortex_pull", s.vortex_pull);
    f("pattern", s.texture.pattern);
    f("color_mode", s.texture.color_mode);
    f("core_color", s.texture.core_color);
    f("edge_color", s.texture.edge_color);
    f("texture_size", s.texture.size);
    f("hardness", s.texture.hardness);
    f("texture_file", s.texture.file);
    f("scale_mode", s.scale_mode);
    f("scale_start", s.scale_start);
    f("scale_end", s.scale_end);
    f("geometry", s.geometry);
    f("quad_width", s.quad_width);
    f("quad_height", s.quad_height);
    f("double_sided", s.double_sided);
    f("cluster_size", s.cluster_size);
    f("cluster_radius", s.cluster_radius);
    f("mesh_rotation", s.mesh_rotation);
    f("align_to_velocity", s.align_to_velocity);
    f("particle_spin", s.particle_spin);
    f("spin_spread", s.spin_spread);
}

// The values that only take whole numbers (units, degrees, degrees per second, speeds): all but the
// duration and the fractions (lifespan and its variance, burst window, size variance, drag, bounce,
// hardness, the vertex-bone scales and the model scale). The int settings are whole already.
template <typename S, typename F>
    requires std::same_as<std::remove_const_t<S>, Settings>
void for_each_whole_value(S& s, F&& f)
{
    for (auto* v : {&s.volume_width, &s.volume_depth, &s.volume_height, &s.velocity.x, &s.velocity.y, &s.velocity.z,
                    &s.spin.x, &s.spin.y, &s.spin.z, &s.emission_angle, &s.spawn_radius, &s.spawn_height,
                    &s.emitter_tilt.x, &s.emitter_tilt.y, &s.emitter_tilt.z, &s.speed, &s.burst, &s.spread, &s.gravity,
                    &s.floor_height, &s.turbulence, &s.vortex_spin, &s.vortex_pull, &s.quad_width, &s.quad_height,
                    &s.cluster_radius, &s.mesh_rotation.x, &s.mesh_rotation.y, &s.mesh_rotation.z, &s.particle_spin.x,
                    &s.particle_spin.y, &s.particle_spin.z, &s.spin_spread})
        f(*v);
}

// Rounds every value of for_each_whole_value() to the nearest whole number; true if one changed.
bool round_whole_values(Settings& s) noexcept;

int quads_for(Geometry g) noexcept;
// Distinct corner positions of a geometry. The six quads of a box share eight; the others share none.
int corners_for(Geometry g) noexcept;
// Bones of one particle: its own, and with vertex bones one for each corner. The settings are
// checked with this before there is a system; ParticleSystem builds its BoneLayout to agree.
int bones_per_particle(Geometry g, ScaleMode s) noexcept;
int max_particles(Geometry g, ScaleMode s) noexcept;
void enforce_bone_limits(Settings& s) noexcept;

// Hard limits that keep the simulation, the textures and the export well
// defined. They are wider than the UI slider ranges: values typed past a
// slider (Ctrl+click) are kept as long as they stay inside these limits.
inline constexpr double kMinDuration = 0.1, kMaxDuration = 60.0;
inline constexpr double kMinLifespan = 0.01;
inline constexpr int kMinFps = 1, kMaxFps = 60;
inline constexpr double kMaxBurstWindow = 1.0;
inline constexpr double kMaxVariance = 0.9;
inline constexpr int kMaxClusterSize = 64;
inline constexpr double kMaxClusterRadius = 2000.0;
inline constexpr double kMaxDrag = 10.0;
inline constexpr double kMaxBounce = 0.95;
inline constexpr double kMaxFloorHeight = 2000.0;
inline constexpr double kMinHardness = 0.05, kMaxHardness = 20.0;
inline constexpr double kMaxScale = 100.0;
inline constexpr double kMinModelScale = 0.01, kMaxModelScale = 100.0;
inline constexpr double kMaxVolume = 256;   // the volume the effect is fitted into, per axis
inline constexpr double kMaxQuadSize = 4096;

// Moves every field into its valid range (non-finite numbers fall back to the
// default). Returns true when something changed.
bool validate(Settings& s);
Settings validated(Settings s);

// "New" button state.
Settings blank_settings();

} // namespace gs
