#include "BoneLayout.hpp"

namespace gs {

BoneLayout::BoneLayout(int particles, int corners_per_particle, bool particle_bones) noexcept
    : particles_(particles), corners_(corners_per_particle), particle_bones_(particle_bones || corners_per_particle == 0)
{
}

BoneLayout BoneLayout::folding(int particles) noexcept
{
    BoneLayout b(particles, 0);
    b.fold_ = true;
    return b;
}

int BoneLayout::parent(int bone) const noexcept
{
    if (!particle_bones_)
        return -1;
    const int within = bone % per_particle();   // 0 for the particle's bone, else its corners
    return within == 0 ? -1 : bone - within;
}

std::string BoneLayout::name(int bone) const
{
    const char* kind = particle_bones_ && bone % per_particle() == 0 ? "p_" : fold_ ? "f_" : "c_";
    return kind + std::to_string(bone);
}

} // namespace gs
