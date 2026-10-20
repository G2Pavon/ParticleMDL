#pragma once

#include <cstdint>
#include <filesystem>

#include "TextureGenerator.hpp"

namespace gs {

// Larger pictures are refused: a texture is at most 256 x 256, and decoding a huge
// one would only use memory. A 4096 x 4096 picture is 64 MB once decoded.
inline constexpr int kMaxImageDimension = 4096;
inline constexpr std::uintmax_t kMaxImageFileBytes = 64u << 20;

// Decodes an image file (PNG, JPG, BMP, TGA, ...) to 8-bit RGBA. Throws
// std::runtime_error with the reason when the file cannot be read.
RgbaImage load_image_file(const std::filesystem::path& path);

// The image fitted inside a size x size texture: aspect kept, centred, the
// rest black. Transparent pixels are premultiplied over black, so they add
// nothing in additive blending.
RgbImage fit_to_square(const RgbaImage& img, int size);
// The same, keeping the alpha: colours times alpha, then the alpha (0 outside the picture).
RgbaImage fit_to_square_rgba(const RgbaImage& img, int size);

} // namespace gs
