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
    bool sky = false;   // the game's sky behind everything (the renderer holds its images)
};

// A piece of the sky: part of one face of the cube around the camera, showing image `side` (an index
// into kSkySides).
struct SkyQuad {
    int side;
    std::array<Point2, 4> pts;
    std::array<Point2, 4> uvs;
};

// Preview display list: draw the lines first, then the faces in order.
struct Scene {
    std::vector<SkyQuad> sky;         // behind everything
    std::vector<SceneLine> lines;     // behind the particles: grid, axes, bounds
    std::vector<SceneFace> faces;     // back to front (painter's algorithm)
};

// Corners of the fitted volume at loop time `time`: the box the motion was
// scaled into (already in world orientation, emitter tilt included), turned
// by the system spin. Bottom four first, counter-clockwise.
std::array<Vec3, 8> bounds_corners(const ParticleSystem& sys, double time);

// The sky around the camera, each face in kSkyGrid x kSkyGrid pieces: small enough that drawing a
// piece with its texture stretched flat across it (as a 2D renderer does) looks like the perspective
// it should have. Pieces partly behind the camera are left out; they are off the screen.
inline constexpr int kSkyGrid = 16;
void add_sky(std::vector<SkyQuad>& out, const OrbitCamera& camera, Point2 center);

// Projects the sky (options.sky), grid, axes, volume bounds and every particle face at loop time
// `time`: the particles from the baked frames, blended the way the engine plays
// them, so the preview shows what the game will. Reuses the storage of `out`.
void build_scene(const ParticleSystem& sys, const BakedPoses& baked, const OrbitCamera& camera, double time,
                 Point2 center, const SceneOptions& options, Scene& out);

} // namespace gs
