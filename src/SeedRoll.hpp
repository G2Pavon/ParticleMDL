#pragma once

#include "Settings.hpp"

namespace gs {

// The two seeds also vary the settings of their panel. Each value that is in use (not 0) is moved
// at random by up to kRollAmount of itself and kept inside its slider's range; a value at 0 stays
// 0, so a seed never turns on what the effect does not use, and so do an emission angle of 360 (all
// around) and a lifespan of 1 (never dies). Choices (shape, geometry, end
// behavior, loop, checkboxes), the frame rate and the texture are never changed.
//   System: the animation (count, duration, lifespan and its variance, burst window), the
//     volume, the system velocity and rotation, the emitter and the physics.
//   Particle: the vertex-bone scaling, the geometry (size, size variance, cluster, mesh
//     rotation) and the per-particle spin.
enum class SeedSide { System, Particle };

inline constexpr double kRollAmount = 0.25;

// Sets the seed of `side` to `seed` and that side's values to `base`'s, varied by the seed; seed 0
// gives `base`'s values unchanged. The other side's values are left as they are in `s`.
void roll_settings(Settings& s, const Settings& base, SeedSide side, int seed);

// Whether the values a seed of `side` varies are the same in `a` and `b`.
bool same_rolled_values(const Settings& a, const Settings& b, SeedSide side);

} // namespace gs
