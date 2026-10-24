#pragma once

#include <string>

namespace gs {

// The skeleton studiomdl is given: each particle's bone followed by its corner bones, in particle
// order. A particle's bone is a root bone (no parent; the engine takes any number of them) and
// carries its quads; with vertex-bone scaling each distinct corner of the quads has a bone of its
// own, hung on the particle's, that moves the corner to scale the quads. Without particle bones
// (corner_bones_only() in Settings.hpp) the corner bones are the roots and carry the whole motion.
// There is no shared root, which would carry nothing and cost a bone. Plain arithmetic: nothing
// is stored per bone.
class BoneLayout {
public:
    BoneLayout() = default;
    BoneLayout(int particles, int corners_per_particle, bool particle_bones = true) noexcept;
    // Folding: each particle's bone and a fold bone hung on it (see folds() in Settings.hpp).
    static BoneLayout folding(int particles) noexcept;

    int count() const noexcept { return particles_ * per_particle(); }
    int per_particle() const noexcept { return (particle_bones_ ? 1 : 0) + corners_ + (fold_ ? 1 : 0); }
    bool fold_bones() const noexcept { return fold_; }
    int fold_bone(int particle) const noexcept { return particle_bone(particle) + 1; }   // only with fold_bones()
    int particles() const noexcept { return particles_; }
    int corners_per_particle() const noexcept { return corners_; }   // 0 without vertex bones
    bool vertex_bones() const noexcept { return corners_ > 0; }
    bool particle_bones() const noexcept { return particle_bones_; }

    // The particle's own bone; only with particle_bones().
    int particle_bone(int particle) const noexcept { return particle * per_particle(); }
    int corner_bone(int particle, int corner) const noexcept
    {
        return particle * per_particle() + (particle_bones_ ? 1 : 0) + corner;
    }

    int parent(int bone) const noexcept;   // -1 for a root: a particle's bone, or any bone without them
    std::string name(int bone) const;      // "p_<bone>", "c_<bone>" or "f_<bone>", as in the SMD

private:
    int particles_ = 0;
    int corners_ = 0;
    bool particle_bones_ = true;
    bool fold_ = false;
};

} // namespace gs
