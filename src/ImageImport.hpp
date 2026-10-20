#pragma once

#include <filesystem>

#include "TextureGenerator.hpp"

namespace gs {

// Decodes an image file (PNG, JPG, BMP, TGA, ...) to 8-bit RGBA. Throws
// std::runtime_error with the reason when the file cannot be read.
RgbaImage load_image_file(const std::filesystem::path& path);

// The image fitted inside a size x size texture: aspect kept, centred, the
// rest black. Transparent pixels are premultiplied over black, so they add
// nothing in additive blending.
RgbImage fit_to_square(const RgbaImage& img, int size);

} // namespace gs
