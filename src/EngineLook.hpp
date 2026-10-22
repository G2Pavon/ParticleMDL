#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "TextureGenerator.hpp"

namespace gs {

// How GoldSrc (and Xash3D) load a studio model's texture, so the preview shows what the game shows,
// with the game's default picture settings: gamma 2.5, texgamma 2.0. The effect is drawn as the game
// draws an Additive entity (render mode 5) at renderamt 255: not lit, its texture added as it is.

// The engine's texture gamma table (gamma.c, BuildGammaTable), worked out the way it does: a model
// texture's palette colours, as loaded.
const std::array<std::uint8_t, 256>& texture_gamma_table();

// The texture as the game loads it: every palette colour through the texture gamma table. The
// exported BMP keeps its own colours; the game does this itself.
IndexedImage with_texture_gamma(IndexedImage image);

// The texture's mipmaps as the engine makes them (gl_image.c, GL_BuildMipMap: each pixel the average
// of the four below it, rounded down), from the full size down to 1 x 1. The first is `image` itself.
std::vector<RgbaImage> engine_mipmaps(const RgbaImage& image);

} // namespace gs
