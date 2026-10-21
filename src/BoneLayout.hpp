#pragma once

#include <string>

namespace gs {

// The skeleton studiomdl is given: the root (bone 0), then each particle's bone followed by its
// corner bones, in particle order. A particle's bone hangs on the root and carries its quads; with
// vertex-bone scaling each distinct corner of the quads has a bone of its own, hung on the
// particle's, that moves the corner to scale the quads. Plain arithmetic: nothing is stored per bone.
class BoneLayout {
public:
    BoneLayout() = default;
    BoneLayout(int particles, int corners_per_particle) noexcept;

    int count() const noexcept { return 1 + particles_ * stride(); }   // the root included
    int particles() const noexcept { return particles_; }
    int corners_per_particle() const noexcept { return corners_; }   // 0 without vertex bones
    bool vertex_bones() const noexcept { return corners_ > 0; }

    int particle_bone(int particle) const noexcept { return 1 + particle * stride(); }
    int corner_bone(int particle, int corner) const noexcept { return particle_bone(particle) + 1 + corner; }

    int parent(int bone) const noexcept;   // -1 for the root
    std::string name(int bone) const;      // "root", "p_<bone>" or "c_<bone>", as in the SMD

private:
    int stride() const noexcept { return 1 + corners_; }   // bones of one particle

    int particles_ = 0;
    int corners_ = 0;
};

} // namespace gs
