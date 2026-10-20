#include "ImageImport.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>
#include <stdexcept>
#include <system_error>
#include <string>

#include "PathUtil.hpp"

// The one translation unit that holds stb's implementation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

namespace gs {

namespace {

struct StbiFree {
    void operator()(unsigned char* pixels) const noexcept { stbi_image_free(pixels); }
};

} // namespace

RgbaImage load_image_file(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        throw std::runtime_error("image file not found: " + to_utf8(path));
    const std::string name = to_utf8(path);
    if (std::filesystem::file_size(path, ec) > kMaxImageFileBytes)
        throw std::runtime_error("image " + name + " is too large (the file may be at most " +
                                 std::to_string(kMaxImageFileBytes >> 20) + " MB)");

    // The file is read here, with a real path, so no file name goes through stb's C API.
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot read image " + name);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        throw std::runtime_error("cannot read image " + name + ": the file is empty");
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("image " + name + " is too large");

    // The size is in the header: refuse a huge picture before decoding it.
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels))
        throw std::runtime_error("cannot read image " + name + ": " + stbi_failure_reason());
    if (w <= 0 || h <= 0 || w > kMaxImageDimension || h > kMaxImageDimension)
        throw std::runtime_error("image " + name + " is too large (" + std::to_string(w) + " x " + std::to_string(h) +
                                 " pixels; at most " + std::to_string(kMaxImageDimension) + " on a side)");

    const std::unique_ptr<unsigned char, StbiFree> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4));
    if (!pixels)
        throw std::runtime_error("cannot read image " + name + ": " + stbi_failure_reason());
    RgbaImage img(w, h);
    std::copy_n(pixels.get(), img.pixels.size(), img.pixels.begin());
    return img;
}

RgbaImage fit_to_square_rgba(const RgbaImage& img, int size)
{
    RgbaImage out(size, size);
    if (img.width <= 0 || img.height <= 0)
        return out;

    // Scale to fit, keeping the aspect ratio.
    const double scale = static_cast<double>(size) / std::max(img.width, img.height);
    const double dst_w = img.width * scale, dst_h = img.height * scale;
    const double left = (size - dst_w) / 2.0, top = (size - dst_h) / 2.0;

    // Premultiplied colour of a source pixel, in 0..1.
    auto premultiplied = [&](int x, int y, int channel) {
        const std::uint8_t* p = img.pixel(x, y);
        return channel == 3 ? p[3] / 255.0 : p[channel] / 255.0 * (p[3] / 255.0);
    };
    // Bilinear sample between source pixels: enough when the picture is enlarged.
    auto sample = [&](double u, double v, int channel) {
        const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, img.width - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, img.height - 1);
        const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
        const double fx = std::clamp(u - x0, 0.0, 1.0), fy = std::clamp(v - y0, 0.0, 1.0);
        const double top_row = premultiplied(x0, y0, channel) * (1 - fx) + premultiplied(x1, y0, channel) * fx;
        const double bottom = premultiplied(x0, y1, channel) * (1 - fx) + premultiplied(x1, y1, channel) * fx;
        return top_row * (1 - fy) + bottom * fy;
    };
    // Average of the source rectangle [u0, u1) x [v0, v1), pixels weighted by how much of
    // them it covers: when the picture is reduced, every source pixel counts.
    auto average = [&](double u0, double v0, double u1, double v1, int channel) {
        u0 = std::max(u0, 0.0);
        v0 = std::max(v0, 0.0);
        u1 = std::min(u1, static_cast<double>(img.width));
        v1 = std::min(v1, static_cast<double>(img.height));
        double sum = 0, weight = 0;
        for (int y = static_cast<int>(std::floor(v0)); y < static_cast<int>(std::ceil(v1)); ++y) {
            const double wy = std::min(v1, y + 1.0) - std::max(v0, static_cast<double>(y));
            for (int x = static_cast<int>(std::floor(u0)); x < static_cast<int>(std::ceil(u1)); ++x) {
                const double w = wy * (std::min(u1, x + 1.0) - std::max(u0, static_cast<double>(x)));
                sum += premultiplied(x, y, channel) * w;
                weight += w;
            }
        }
        return weight > 0 ? sum / weight : 0.0;
    };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (x + 0.5 < left || x + 0.5 > left + dst_w || y + 0.5 < top || y + 0.5 > top + dst_h)
                continue;   // padding stays black
            std::uint8_t* px = out.pixel(x, y);
            for (int c = 0; c < 4; ++c) {
                const double value =
                    scale < 1.0 ? average((x - left) / scale, (y - top) / scale, (x + 1 - left) / scale,
                                          (y + 1 - top) / scale, c)
                                : sample((x + 0.5 - left) / scale - 0.5, (y + 0.5 - top) / scale - 0.5, c);
                px[c] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
            }
        }
    }
    return out;
}

RgbImage fit_to_square(const RgbaImage& img, int size)
{
    const RgbaImage fitted = fit_to_square_rgba(img, size);
    RgbImage out(size, size);
    for (std::size_t i = 0; i < out.pixel_count(); ++i)
        std::copy_n(fitted.pixel(i), 3, out.pixel(i));
    return out;
}

} // namespace gs
