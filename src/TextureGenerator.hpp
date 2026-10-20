#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Settings.hpp"

namespace gs {

// 8-bit image with `Channels` interleaved channels, row-major, top row first.
template <int Channels>
struct Image {
    static constexpr int channels = Channels;

    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;

    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h * Channels) {}

    std::size_t pixel_count() const noexcept { return static_cast<std::size_t>(width) * height; }
    std::uint8_t* pixel(std::size_t index) noexcept { return &pixels[index * Channels]; }
    const std::uint8_t* pixel(std::size_t index) const noexcept { return &pixels[index * Channels]; }
    std::uint8_t* pixel(int x, int y) noexcept { return pixel(static_cast<std::size_t>(y) * width + x); }
    const std::uint8_t* pixel(int x, int y) const noexcept { return pixel(static_cast<std::size_t>(y) * width + x); }
};

using RgbImage = Image<3>;
using RgbaImage = Image<4>;

// Procedural sprite patterns. dx/dy are in [-1, 1] across the sprite.
double pattern_intensity(TexPattern pattern, double dx, double dy, double dist) noexcept;

// Export texture (tex_res x tex_res), color premultiplied by intensity.
RgbImage make_export_texture(const TextureSettings& s);

// A masked texture: the full colour where the pattern is strong enough, transparent elsewhere.
inline constexpr double kMaskThreshold = 0.35;
struct MaskedTexture {
    RgbImage colors;
    std::vector<bool> opaque;   // per pixel
};
MaskedTexture make_masked_texture(const TextureSettings& s);

// The palette entry GoldSrc treats as transparent in a masked texture, and the colour it has.
inline constexpr int kTransparentIndex = 255;
inline constexpr std::array<std::uint8_t, 3> kTransparentColor{0, 0, 255};

struct IndexedImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> indices;                  // row-major, top row first
    std::vector<std::array<std::uint8_t, 3>> palette;   // at most 256 entries
    bool masked = false;                                // index 255 is transparent
};

// Reduces to <= 256 colors: exact palette when possible, median cut otherwise. With `opaque`
// (one flag per pixel) the pixels that are not opaque get the transparent index 255 and the
// others at most 255 colours.
IndexedImage quantize(const RgbImage& img, const std::vector<bool>* opaque = nullptr);
// Imported images: k-means refinement of the median-cut palette and
// Floyd-Steinberg dithering. Slower; better colours and gradients.
IndexedImage quantize_refined(const RgbImage& img, const std::vector<bool>* opaque = nullptr);
// What the BMP holds and the preview shows: the procedural pattern, or the
// imported image when TextureSettings::file is set. Throws if it cannot be read.
IndexedImage export_texture(const TextureSettings& s);
RgbImage expand(const IndexedImage& img);
RgbaImage to_rgba(const RgbImage& img);   // opaque
RgbaImage expand_rgba(const IndexedImage& img);   // the transparent index of a masked image has alpha 0

// 8-bit indexed, uncompressed BMP as GoldSrc's studiomdl expects.
std::vector<std::uint8_t> encode_indexed_bmp(const IndexedImage& img);
std::vector<std::uint8_t> encode_indexed_bmp(const RgbImage& img);   // quantizes first

} // namespace gs
