#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Baking.hpp"
#include "OrbitCamera.hpp"
#include "ParticleSystem.hpp"

namespace gs {

struct Rgba8 {
    std::uint8_t r, g, b, a;

    friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

struct SceneLine {
    Point2 a, b;
    Rgba8 color;
    float thickness;
};

struct SceneFace {
    double depth;   // mean camera depth of the corners
    std::array<Point2, 4> pts;
    std::array<Point2, 4> uvs;
};

struct SceneOptions {
    bool grid = true;
    bool axes = true;
    bool bounds = true;
};

// Preview display list: draw the lines first, then the faces in order.
struct Scene {
    std::vector<SceneLine> lines;     // behind the particles: grid, axes, bounds
    std::vector<SceneFace> faces;     // back to front (painter's algorithm)
};

// Corners of the fitted volume at loop time `time`: the box the motion was
// scaled into (already in world orientation, emitter tilt included), turned
// by the system spin. Bottom four first, counter-clockwise.
std::array<Vec3, 8> bounds_corners(const ParticleSystem& sys, double time);

// Projects grid, axes, volume bounds and every particle face at loop time
// `time`: the particles from the baked frames, blended the way the engine plays
// them, so the preview shows what the game will. Reuses the storage of `out`.
void build_scene(const ParticleSystem& sys, const BakedPoses& baked, const OrbitCamera& camera, double time,
                 Point2 center, const SceneOptions& options, Scene& out);

} // namespace gs
