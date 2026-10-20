#include "Presets.hpp"

#include <algorithm>
#include <array>

namespace gs {

std::span<const Preset> builtin_presets()
{
    // clang-format off
    static const std::array kPresets = {
        Preset{"Fountain", {
            .count = 64, .duration = 3.0, .lifespan = 0.85, .fps = 30, .end_behavior = EndBehavior::HideUnderground,
            .volume_width = 128, .volume_depth = 128, .volume_height = 256, .velocity = {0, 0, 0}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 0, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 60, .burst = 120, .spread = 30, .gravity = -150, .turbulence = 0, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::RadialOrb, .color_mode = ColorMode::SolidColor, .core_color = {255, 170, 85}, .edge_color = {0, 0, 0}, .texture_size = 64, .hardness = 0.8, .texture_file = {},
            .scale_mode = ScaleMode::Static, .scale_start = 1.0, .scale_end = 1.0,
            .geometry = Geometry::CrossedQuads, .quad_width = 6, .quad_height = 6, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {0, 0, 0}, .spin_spread = 0}},
        Preset{"Campfire Sparks", {
            .count = 25, .duration = 4.0, .lifespan = 0.8, .fps = 30, .end_behavior = EndBehavior::HideUnderground,
            .volume_width = 64, .volume_depth = 64, .volume_height = 128, .velocity = {0, 0, 0}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 0, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 15, .burst = 30, .spread = 10, .gravity = 20, .turbulence = 25, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::SoftCloud, .color_mode = ColorMode::SolidColor, .core_color = {255, 100, 20}, .edge_color = {0, 0, 0}, .texture_size = 64, .hardness = 1.2, .texture_file = {},
            .scale_mode = ScaleMode::Static, .scale_start = 1.5, .scale_end = 0.1,
            .geometry = Geometry::SingleQuad, .quad_width = 3, .quad_height = 3, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {45, 45, 90}, .spin_spread = 90}},
        Preset{"Orbs", {
            .count = 70, .duration = 2.7, .lifespan = 0.85, .fps = 15, .end_behavior = EndBehavior::HideUnderground,
            .volume_width = 256, .volume_depth = 256, .volume_height = 128, .velocity = {0, 0, 0}, .spin = {0, 30, 0},
            .emission_angle = 360, .spawn_radius = 0, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 35, .burst = 70, .spread = 25, .gravity = -40, .turbulence = 60, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::RadialOrb, .color_mode = ColorMode::Gradient, .core_color = {255, 255, 255}, .edge_color = {100, 0, 255}, .texture_size = 64, .hardness = 0.5, .texture_file = {},
            .scale_mode = ScaleMode::Static, .scale_start = 1.0, .scale_end = 1.0,
            .geometry = Geometry::TriPlanarQuads, .quad_width = 10, .quad_height = 10, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {45, 45, 90}, .spin_spread = 90}},
        Preset{"Vortex", {
            .count = 25, .duration = 4.0, .lifespan = 1.0, .fps = 15, .end_behavior = EndBehavior::HideInCore,
            .volume_width = 512, .volume_depth = 512, .volume_height = 512, .velocity = {0, 0, 0}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 300, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = true,
            .speed = 0, .burst = 0, .spread = 0, .gravity = 0, .turbulence = 0, .vortex_spin = 180, .vortex_pull = 80,
            .pattern = TexPattern::RadialOrb, .color_mode = ColorMode::Gradient, .core_color = {255, 255, 255}, .edge_color = {50, 0, 150}, .texture_size = 64, .hardness = 0.5, .texture_file = {},
            .scale_mode = ScaleMode::VertexBone, .scale_start = 2.0, .scale_end = 0.1,
            .geometry = Geometry::SingleQuad, .quad_width = 15, .quad_height = 15, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {45, 45, 90}, .spin_spread = 90}},
        Preset{"Ambient Ash", {
            .count = 80, .duration = 4.0, .lifespan = 0.95, .fps = 15, .end_behavior = EndBehavior::EndlessLoop,
            .volume_width = 512, .volume_depth = 512, .volume_height = 512, .velocity = {20, 10, -10}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 300, .spawn_height = 150, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 5, .burst = 10, .spread = 15, .gravity = 10, .turbulence = 80, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::SoftCloud, .color_mode = ColorMode::SolidColor, .core_color = {180, 180, 180}, .edge_color = {0, 0, 0}, .texture_size = 64, .hardness = 0.8, .texture_file = {},
            .scale_mode = ScaleMode::Static, .scale_start = 1.0, .scale_end = 1.0,
            .geometry = Geometry::TriPlanarQuads, .quad_width = 3, .quad_height = 3, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {45, 45, 90}, .spin_spread = 90}},
        Preset{"Laser Burst", {
            .count = 80, .duration = 1.0, .lifespan = 1.0, .fps = 30, .end_behavior = EndBehavior::HideUnderground,
            .volume_width = 512, .volume_depth = 512, .volume_height = 32, .velocity = {0, 0, 0}, .spin = {0, 0, 0},
            .emission_angle = 1, .spawn_radius = 0, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 300, .burst = 0, .spread = 0, .gravity = 0, .turbulence = 0, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::RadialOrb, .color_mode = ColorMode::SolidColor, .core_color = {255, 20, 20}, .edge_color = {0, 0, 0}, .texture_size = 128, .hardness = 2.0, .texture_file = {},
            .scale_mode = ScaleMode::Static, .scale_start = 1.0, .scale_end = 0.0,
            .geometry = Geometry::TriPlanarQuads, .quad_width = 50, .quad_height = 50, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {0, 0, 0}, .spin_spread = 32}},
        Preset{"Smoke", {
            .count = 25, .duration = 3.1, .lifespan = 0.2, .fps = 15, .end_behavior = EndBehavior::EndlessLoop,
            .volume_width = 40, .volume_depth = 40, .volume_height = 126, .velocity = {0, 0, 0}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 291, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 10, .burst = 40, .spread = 20, .gravity = 30, .turbulence = 198, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::SoftCloud, .color_mode = ColorMode::SolidColor, .core_color = {110, 110, 115}, .edge_color = {0, 0, 0}, .texture_size = 64, .hardness = 0.5, .texture_file = {},
            .scale_mode = ScaleMode::VertexBone, .scale_start = 3.1, .scale_end = 1.2,
            .geometry = Geometry::SingleQuad, .quad_width = 21, .quad_height = 22, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {45, 45, 90}, .spin_spread = 90}},
        Preset{"Sun", {
            .count = 50, .duration = 5.0, .lifespan = 0.95, .fps = 15, .end_behavior = EndBehavior::EndlessLoop,
            .volume_width = 1, .volume_depth = 1, .volume_height = 1, .velocity = {15, 10, 0}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 300, .spawn_height = 250, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 10, .burst = 0, .spread = 0, .gravity = 0, .turbulence = 0, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::SoftCloud, .color_mode = ColorMode::Gradient, .core_color = {255, 255, 255}, .edge_color = {255, 100, 20}, .texture_size = 256, .hardness = 0.5, .texture_file = {},
            .scale_mode = ScaleMode::Static, .scale_start = 1.0, .scale_end = 1.0,
            .geometry = Geometry::TriPlanarQuads, .quad_width = 32, .quad_height = 32, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {0, 0, 15}, .spin_spread = 0}},
        Preset{"Waterfall", {
            .count = 14, .duration = 4.7, .lifespan = 0.8, .fps = 30, .end_behavior = EndBehavior::DropThroughFloor,
            .volume_width = 1, .volume_depth = 1, .volume_height = 119, .velocity = {0, 0, -500}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 500, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 0, .burst = 0, .spread = 30, .gravity = -174, .turbulence = 200, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::RainDrop, .color_mode = ColorMode::SolidColor, .core_color = {120, 165, 220}, .edge_color = {0, 0, 0}, .texture_size = 64, .hardness = 0.6, .texture_file = {},
            .scale_mode = ScaleMode::VertexBone, .scale_start = 0.3, .scale_end = 2.7,
            .geometry = Geometry::SingleQuad, .quad_width = 48, .quad_height = 66, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {0, 0, 0}, .spin_spread = 0}},
        Preset{"Fire", {
            .count = 9, .duration = 2.8, .lifespan = 1.0, .fps = 30, .end_behavior = EndBehavior::EndlessLoop,
            .volume_width = 17, .volume_depth = 17, .volume_height = 146, .velocity = {0, 0, -500}, .spin = {0, 0, 0},
            .emission_angle = 360, .spawn_radius = 93, .spawn_height = 0, .emitter_tilt = {0, 0, 0}, .hollow_ring = false,
            .speed = 0, .burst = 300, .spread = 150, .gravity = 200, .turbulence = 200, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::RadialOrb, .color_mode = ColorMode::Gradient, .core_color = {255, 170, 85}, .edge_color = {210, 105, 30}, .texture_size = 64, .hardness = 1.4, .texture_file = {},
            .scale_mode = ScaleMode::VertexBone, .scale_start = 2.4, .scale_end = 0.9,
            .geometry = Geometry::CrossedQuads, .quad_width = 23, .quad_height = 100, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {0, 0, 360}, .spin_spread = 0}},
        Preset{"Torch Flame", {
            .count = 19, .duration = 3.0, .lifespan = 1.0, .fps = 30, .end_behavior = EndBehavior::HideUnderground,
            .volume_width = 1, .volume_depth = 1, .volume_height = 7, .velocity = {0, 0, 0}, .spin = {0, 0, 34},
            .emission_angle = 360, .spawn_radius = 0, .spawn_height = 0, .emitter_tilt = {0, 0, 75}, .hollow_ring = false,
            .speed = 300, .burst = 0, .spread = 0, .gravity = 1, .turbulence = 200, .vortex_spin = 0, .vortex_pull = 0,
            .pattern = TexPattern::RainDrop, .color_mode = ColorMode::SolidColor, .core_color = {255, 170, 85}, .edge_color = {0, 0, 0}, .texture_size = 64, .hardness = 2.0, .texture_file = {},
            .scale_mode = ScaleMode::VertexBone, .scale_start = 2.0, .scale_end = 0.6,
            .geometry = Geometry::SingleQuad, .quad_width = 13, .quad_height = 6, .double_sided = true, .mesh_rotation = {0, 0, 0},
            .particle_spin = {0, 0, 176}, .spin_spread = 0}},
    };
    // clang-format on
    return kPresets;
}

const Preset* find_preset(std::string_view name)
{
    const auto presets = builtin_presets();
    const auto it = std::ranges::find(presets, name, &Preset::name);
    return it != presets.end() ? &*it : nullptr;
}

void apply_preset(Settings& s, const Preset& preset)
{
    std::string model_name = std::move(s.model_name);
    s = preset.settings;
    s.model_name = std::move(model_name);
    enforce_bone_limits(s);
}

} // namespace gs
