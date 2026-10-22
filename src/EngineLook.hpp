#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "MathUtil.hpp"
#include "Settings.hpp"
#include "TextureGenerator.hpp"

namespace gs {

// How GoldSrc (and Xash3D) colour a studio model, so the preview shows what the game shows. All of it
// with the game's default picture settings: gamma 2.5, brightness 0, texgamma 2.0, lightgamma 2.5.

// The engine's gamma tables (gamma.c, BuildGammaTable), worked out the way it does.
struct GammaTables {
    std::array<std::uint8_t, 256> texture;   // a model texture's palette colours, as loaded
    std::array<std::uint16_t, 1024> light;   // model lighting, from 4 x illumination to 0..1023
};
const GammaTables& gamma_tables();

// The texture as the game loads it: every palette colour through the texture gamma table. The
// exported BMP keeps its own colours; the game does this itself.
IndexedImage with_texture_gamma(IndexedImage image);

// How the effect's entity is set up in the map.
enum class EntityRenderMode {
    Additive,   // rendermode Additive (5): not lit by the map, faded by renderamt
    Normal,     // rendermode Normal (0): its additive texture lit by the map's light where it stands
};
struct EntityLook {
    EntityRenderMode mode = EntityRenderMode::Additive;
    int renderamt = 255;               // 0..255, Additive only
    Rgb light{128, 128, 128};          // Normal only: the map's light at the entity (the floor below it)

    friend bool operator==(const EntityLook&, const EntityLook&) = default;
};

// The colour and alpha the engine gives the vertices of a face whose normal (in the world) is
// `normal`: the texture is multiplied by the colour and added to the picture times the alpha (blend
// GL_SRC_ALPHA, GL_ONE for Additive; GL_ONE, GL_ONE for Normal, which is the same with alpha 255).
struct FaceColor {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    friend bool operator==(const FaceColor&, const FaceColor&) = default;
};
FaceColor face_color(const EntityLook& look, Vec3 normal);

// The texture's mipmaps as the engine makes them (gl_image.c, GL_BuildMipMap: each pixel the average
// of the four below it, rounded down), from the full size down to 1 x 1. The first is `image` itself.
std::vector<RgbaImage> engine_mipmaps(const RgbaImage& image);

} // namespace gs
