#include "BoneLayout.hpp"

namespace gs {

BoneLayout::BoneLayout(int particles, int corners_per_particle) noexcept
    : particles_(particles), corners_(corners_per_particle)
{
}

int BoneLayout::parent(int bone) const noexcept
{
    if (bone == 0)
        return -1;
    const int within = (bone - 1) % stride();   // 0 for the particle's bone, else its corners
    return within == 0 ? 0 : bone - within;
}

std::string BoneLayout::name(int bone) const
{
    if (bone == 0)
        return "root";
    return ((bone - 1) % stride() == 0 ? "p_" : "c_") + std::to_string(bone);
}

} // namespace gs
