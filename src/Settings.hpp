#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>

#include "Enums.hpp"
#include "MathUtil.hpp"

namespace gs {

inline constexpr std::array<int, 3> kTextureSizes = {64, 128, 256};

// studiomdl refuses a model with MAXSTUDIOBONES (128) bones, so 127 at most:
// the root plus 126 particle bones.
inline constexpr int kMaxBones = 126;

struct Rgb {
    std::uint8_t r = 255, g = 255, b = 255;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// Everything the sprite texture is made from. Two equal values give the same texture,
// so comparing them tells whether a texture has to be made again.
struct TextureSettings {
    TexPattern pattern = TexPattern::RadialOrb;
    ColorMode color_mode = ColorMode::SolidColor;
    Rgb core_color{255, 170, 85};
    Rgb edge_color{0, 0, 0};
    int size = 64;
    double hardness = 0.8;
    std::string file{};   // imported image, a UTF-8 path; empty: the procedural pattern above

    friend bool operator==(const TextureSettings&, const TextureSettings&) = default;
};

// Every value the editor exposes. Angles are in degrees, distances in model
// units, times in seconds.
struct Settings {
    std::string model_name = "custom_particles";

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

    // Volume the motion is fitted into
    double volume_width = 128;   // X
    double volume_depth = 128;   // Y
    double volume_height = 256;  // Z

    // Whole system
    Vec3 velocity{}; // added to every particle, units/s
    Vec3 spin{};     // deg/s

    // Emitter. The shape says where particles start and which way they leave:
    //   Disk        on a disk of spawn_radius, leaving in the horizontal fan of emission_angle
    //   Sphere      in a ball of spawn_radius (on its surface with hollow_ring), leaving outward
    //   Hemisphere  the upper half of the same
    //   Cone        at the origin, leaving within a cone around +Z whose opening is emission_angle
    //   Box         in a box of +-spawn_radius by +-spawn_radius by +-spawn_height, leaving outward
    //   Line        along X, +-spawn_radius, leaving in a fan around +Y of emission_angle
    EmitterShape emitter_shape = EmitterShape::Disk;
    double emission_angle = 360;   // horizontal fan, or the opening of the cone
    double spawn_radius = 0;
    double spawn_height = 0;
    Vec3 emitter_tilt{};
    bool hollow_ring = false;

    // Physics
    double speed = 60;    // horizontal, units/s
    double burst = 120;   // vertical, units/s
    double spread = 30;   // random +- on speed and burst
    double gravity = -150;
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
    Vec3 mesh_rotation{};

    // Per-particle spin
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
    f("count", s.count);
    f("duration", s.duration);
    f("lifespan", s.lifespan);
    f("fps", s.fps);
    f("end_behavior", s.end_behavior);
    f("loop", s.loop);
    f("burst_window", s.burst_window);
    f("volume_width", s.volume_width);
    f("volume_depth", s.volume_depth);
    f("volume_height", s.volume_height);
    f("velocity", s.velocity);
    f("spin", s.spin);
    f("emitter_shape", s.emitter_shape);
    f("emission_angle", s.emission_angle);
    f("spawn_radius", s.spawn_radius);
    f("spawn_height", s.spawn_height);
    f("emitter_tilt", s.emitter_tilt);
    f("hollow_ring", s.hollow_ring);
    f("speed", s.speed);
    f("burst", s.burst);
    f("spread", s.spread);
    f("gravity", s.gravity);
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
    f("mesh_rotation", s.mesh_rotation);
    f("particle_spin", s.particle_spin);
    f("spin_spread", s.spin_spread);
}

int quads_for(Geometry g) noexcept;
// Distinct corner positions of a geometry. The six quads of a box share eight; the others share none.
int corners_for(Geometry g) noexcept;
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
inline constexpr double kMinHardness = 0.05, kMaxHardness = 20.0;
inline constexpr double kMaxScale = 100.0;
inline constexpr double kMaxVolume = 65536;
inline constexpr double kMaxQuadSize = 4096;

// Moves every field into its valid range (non-finite numbers fall back to the
// default). Returns true when something changed.
bool validate(Settings& s);
Settings validated(Settings s);

// "New" button state.
Settings blank_settings();

} // namespace gs
