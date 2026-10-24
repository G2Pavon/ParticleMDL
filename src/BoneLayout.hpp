#pragma once

#include <string>

namespace gs {

// The skeleton studiomdl is given: each particle's bone followed by its corner bones, in particle
// order. A particle's bone is a root bone (no parent; the engine takes any number of them) and
// carries its quads; with vertex-bone scaling each distinct corner of the quads has a bone of its
// own, hung on the particle's, that moves the corner to scale the quads. There is no shared root,
// which would carry nothing and cost a bone. Plain arithmetic: nothing is stored per bone.
class BoneLayout {
public:
    BoneLayout() = default;
    BoneLayout(int particles, int corners_per_particle) noexcept;

    int count() const noexcept { return particles_ * stride(); }
    int particles() const noexcept { return particles_; }
    int corners_per_particle() const noexcept { return corners_; }   // 0 without vertex bones
    bool vertex_bones() const noexcept { return corners_ > 0; }

    int particle_bone(int particle) const noexcept { return particle * stride(); }
    int corner_bone(int particle, int corner) const noexcept { return particle_bone(particle) + 1 + corner; }

    int parent(int bone) const noexcept;   // -1 for a particle's bone, which is a root
    std::string name(int bone) const;      // "p_<bone>" or "c_<bone>", as in the SMD

private:
    int stride() const noexcept { return 1 + corners_; }   // bones of one particle

    int particles_ = 0;
    int corners_ = 0;
};

} // namespace gs
