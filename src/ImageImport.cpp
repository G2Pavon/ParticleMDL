#include "ImageImport.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <string>

// The one translation unit that holds stb's implementation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

namespace gs {

RgbaImage load_image_file(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        throw std::runtime_error("image file not found: " + path.string());
    int w = 0, h = 0, channels = 0;
    // stb takes a C path; a UTF-8 string is what it expects on every platform here.
    const std::string name = path.string();
    unsigned char* pixels = stbi_load(name.c_str(), &w, &h, &channels, 4);
    if (!pixels)
        throw std::runtime_error("cannot read image " + name + ": " + stbi_failure_reason());
    RgbaImage img(w, h);
    std::copy_n(pixels, img.pixels.size(), img.pixels.begin());
    stbi_image_free(pixels);
    return img;
}

RgbImage fit_to_square(const RgbaImage& img, int size)
{
    RgbImage out(size, size);
    if (img.width <= 0 || img.height <= 0)
        return out;

    // Scale to fit, keeping the aspect ratio.
    const double scale = static_cast<double>(size) / std::max(img.width, img.height);
    const double dst_w = img.width * scale, dst_h = img.height * scale;
    const double left = (size - dst_w) / 2.0, top = (size - dst_h) / 2.0;

    // Premultiplied colour of a source pixel, in 0..1.
    auto premultiplied = [&](int x, int y, int channel) {
        const std::uint8_t* p = img.pixel(x, y);
        return p[channel] / 255.0 * (p[3] / 255.0);
    };
    auto sample = [&](double u, double v, int channel) {
        const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, img.width - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, img.height - 1);
        const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
        const double fx = std::clamp(u - x0, 0.0, 1.0), fy = std::clamp(v - y0, 0.0, 1.0);
        const double top_row = premultiplied(x0, y0, channel) * (1 - fx) + premultiplied(x1, y0, channel) * fx;
        const double bottom = premultiplied(x0, y1, channel) * (1 - fx) + premultiplied(x1, y1, channel) * fx;
        return top_row * (1 - fy) + bottom * fy;
    };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double sx = (x + 0.5 - left) / scale - 0.5, sy = (y + 0.5 - top) / scale - 0.5;
            if (x + 0.5 < left || x + 0.5 > left + dst_w || y + 0.5 < top || y + 0.5 > top + dst_h)
                continue;   // padding stays black
            std::uint8_t* px = out.pixel(x, y);
            for (int c = 0; c < 3; ++c)
                px[c] = static_cast<std::uint8_t>(std::lround(std::clamp(sample(sx, sy, c), 0.0, 1.0) * 255.0));
        }
    }
    return out;
}

} // namespace gs
