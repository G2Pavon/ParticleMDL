#pragma once

#include "Settings.hpp"

namespace gs {

// The two seeds also set the values of their panel: each one is drawn at random anywhere in its
// slider's range, whatever it was. A value whose 0 turns a feature off (drag, bounce, turbulence,
// vortex, system velocity, emitter tilt, the variances, mesh rotation, particle spin, cluster) is
// first turned on or off at random. The particle count and the cluster size are drawn among the
// values the export accepts, so a seed never makes an effect studiomdl would refuse; a particle seed
// that would still exceed a limit lowers the particle count, the only value of the other side it
// can change. Choices (geometry, end behavior, loop, checkboxes), the frame rate and the
// texture are never changed.
//   System: the animation (count, duration, lifespan and its variance, burst window), the
//     volume, the system velocity, the emitter and the physics. Not the system rotation.
//   Particle: the vertex-bone scaling, the geometry (size, size variance, cluster, mesh
//     rotation) and the per-particle spin.
enum class SeedSide { System, Particle };

// Sets the seed of `side` to `seed` and that side's values to the ones the seed draws; seed 0 gives
// `base`'s values instead. The other side's values are left as they are in `s` (but for the count,
// see above).
void roll_settings(Settings& s, const Settings& base, SeedSide side, int seed);

// Whether the values a seed of `side` varies are the same in `a` and `b`.
bool same_rolled_values(const Settings& a, const Settings& b, SeedSide side);

} // namespace gs
