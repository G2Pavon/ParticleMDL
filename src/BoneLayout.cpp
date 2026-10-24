#include "BoneLayout.hpp"

namespace gs {

BoneLayout::BoneLayout(int particles, int corners_per_particle) noexcept
    : particles_(particles), corners_(corners_per_particle)
{
}

int BoneLayout::parent(int bone) const noexcept
{
    const int within = bone % stride();   // 0 for the particle's bone, else its corners
    return within == 0 ? -1 : bone - within;
}

std::string BoneLayout::name(int bone) const
{
    return (bone % stride() == 0 ? "p_" : "c_") + std::to_string(bone);
}

} // namespace gs
