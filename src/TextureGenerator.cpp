#include "TextureGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#include "ImageImport.hpp"
#include "MathUtil.hpp"
#include "PathUtil.hpp"

namespace gs {

namespace {

struct Color {
    double r, g, b;   // 0..255
};

Color base_color(const TextureSettings& s, double dist)
{
    const double t = std::min(1.0, std::max(0.0, dist));
    const Rgb& c1 = s.core_color;
    const Rgb& c2 = s.edge_color;
    if (s.color_mode == ColorMode::Gradient)
        return {c1.r * (1.0 - t) + c2.r * t, c1.g * (1.0 - t) + c2.g * t, c1.b * (1.0 - t) + c2.b * t};
    return {static_cast<double>(c1.r), static_cast<double>(c1.g), static_cast<double>(c1.b)};
}

// Calls fn(x, y, color, intensity) for every pixel of a size x size sprite.
template <typename Fn>
void for_each_texel(const TextureSettings& s, int size, Fn&& fn)
{
    const double center = size / 2.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx = (x - center) / center;
            const double dy = (y - center) / center;
            const double dist = std::hypot(dx, dy);
            const double intensity = std::pow(pattern_intensity(s.pattern, dx, dy, dist), s.hardness);
            fn(x, y, base_color(s, dist), intensity);
        }
    }
}

// ---------------------------------------------------------------------------
// Palette quantization
// ---------------------------------------------------------------------------

struct HistEntry {
    std::array<std::uint8_t, 3> rgb;
    std::uint32_t count;
};

std::vector<std::array<std::uint8_t, 3>> median_cut(std::vector<HistEntry> entries, std::size_t max_colors)
{
    struct Box {
        std::size_t begin, end;
    };
    auto channel_range = [&](const Box& b, int ch) {
        auto [lo, hi] = std::minmax_element(entries.begin() + b.begin, entries.begin() + b.end,
                                            [ch](const HistEntry& a, const HistEntry& c) { return a.rgb[ch] < c.rgb[ch]; });
        return static_cast<int>(hi->rgb[ch]) - static_cast<int>(lo->rgb[ch]);
    };

    std::vector<Box> boxes{{0, entries.size()}};
    while (boxes.size() < max_colors) {
        // Split the box with the widest single-channel extent.
        int best_box = -1, best_ch = 0, best_range = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2)
                continue;
            for (int ch = 0; ch < 3; ++ch) {
                const int r = channel_range(boxes[i], ch);
                if (r > best_range) {
                    best_range = r;
                    best_box = static_cast<int>(i);
                    best_ch = ch;
                }
            }
        }
        if (best_box < 0)
            break;

        Box box = boxes[best_box];
        auto first = entries.begin() + box.begin, last = entries.begin() + box.end;
        std::sort(first, last, [ch = best_ch](const HistEntry& a, const HistEntry& b) { return a.rgb[ch] < b.rgb[ch]; });

        std::uint64_t total = 0;
        for (auto it = first; it != last; ++it)
            total += it->count;
        std::uint64_t acc = 0;
        std::size_t split = box.begin + 1;
        for (std::size_t i = box.begin; i < box.end - 1; ++i) {
            acc += entries[i].count;
            split = i + 1;
            if (acc * 2 >= total)
                break;
        }
        boxes[best_box] = {box.begin, split};
        boxes.push_back({split, box.end});
    }

    std::vector<std::array<std::uint8_t, 3>> palette;
    for (const Box& b : boxes) {
        double sum[3] = {0, 0, 0}, n = 0;
        for (std::size_t i = b.begin; i < b.end; ++i) {
            for (int ch = 0; ch < 3; ++ch)
                sum[ch] += static_cast<double>(entries[i].rgb[ch]) * entries[i].count;
            n += entries[i].count;
        }
        palette.push_back({static_cast<std::uint8_t>(std::lround(sum[0] / n)), static_cast<std::uint8_t>(std::lround(sum[1] / n)),
                           static_cast<std::uint8_t>(std::lround(sum[2] / n))});
    }
    return palette;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back((v >> (8 * i)) & 0xFF);
}

} // namespace

double pattern_intensity(TexPattern pattern, double dx, double dy, double dist) noexcept
{
    switch (pattern) {
    case TexPattern::HollowRing:
        return std::max(0.0, 1.0 - std::abs(dist - 0.7) * 4.0);
    case TexPattern::SharpStar:
        return std::max(0.0, 1.0 - (std::abs(dx) + std::abs(dy)));
    case TexPattern::SoftCloud: {
        const double n = (std::sin(dx * 10.0) + std::cos(dy * 10.0)) * 0.5;
        return std::max(0.0, 1.0 - dist) * (0.5 + 0.5 * n);
    }
    case TexPattern::FlowerPetals: {
        const double angle = std::atan2(dy, dx);
        const double spokes = std::pow(std::cos(angle * 3.0), 6.0);
        const double radius = 0.15 + 0.85 * spokes;
        return std::max(0.0, 1.0 - (dist / radius));
    }
    case TexPattern::Snowflake: {
        const double angle = std::atan2(dy, dx);
        const double sector = std::abs(floor_mod(angle, kPi / 3.0) - (kPi / 6.0));
        const double x_sec = dist * std::cos(sector);
        const double y_sec = dist * std::sin(sector);
        const double stem = std::max(0.0, 1.0 - y_sec * 25.0);
        const double stripes = std::cos(x_sec * 50.0 - y_sec * 80.0);
        const double branch = std::max(0.0, stripes) * std::max(0.0, 1.0 - y_sec * 8.0);
        return std::max(stem, branch) * std::max(0.0, 1.0 - dist * 1.1);
    }
    case TexPattern::RainDrop:
        return std::max(0.0, 1.0 - std::abs(dx) * 3.0) * std::max(0.0, 1.0 - std::abs(dy));
    case TexPattern::RadialOrb:
        break;
    }
    return std::max(0.0, 1.0 - dist);
}

RgbImage make_export_texture(const TextureSettings& s)
{
    const int size = s.size;
    RgbImage img(size, size);
    for_each_texel(s, size, [&](int x, int y, Color c, double intensity) {
        std::uint8_t* px = img.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.r * intensity), 0, 255));
        px[1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.g * intensity), 0, 255));
        px[2] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.b * intensity), 0, 255));
    });
    return img;
}

MaskedTexture make_masked_texture(const TextureSettings& s)
{
    const int size = s.size;
    MaskedTexture out{RgbImage(size, size), std::vector<bool>(static_cast<std::size_t>(size) * static_cast<std::size_t>(size))};
    for_each_texel(s, size, [&](int x, int y, Color c, double intensity) {
        if (intensity < kMaskThreshold)
            return;   // transparent: the colour does not matter
        std::uint8_t* px = out.colors.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.r), 0, 255));
        px[1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.g), 0, 255));
        px[2] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.b), 0, 255));
        out.opaque[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = true;
    });
    return out;
}

namespace {

// The colours a quantizer works from: the opaque pixels, or all of them.
bool is_opaque(const std::vector<bool>* opaque, std::size_t i)
{
    return !opaque || (*opaque)[i];
}

// Gives a masked image its transparent colour at index 255.
void finish_masked(IndexedImage& out)
{
    out.masked = true;
    if (out.palette.empty())
        out.palette.push_back({0, 0, 0});
    out.palette.resize(256, {0, 0, 0});
    out.palette[kTransparentIndex] = kTransparentColor;
}

} // namespace

IndexedImage quantize(const RgbImage& img, const std::vector<bool>* opaque)
{
    const std::size_t n = img.pixel_count();
    auto key_of = [&](std::size_t i) {
        const std::uint8_t* p = img.pixel(i);
        return (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
    };

    std::unordered_map<std::uint32_t, std::uint32_t> hist;
    for (std::size_t i = 0; i < n; ++i)
        if (is_opaque(opaque, i))
            ++hist[key_of(i)];

    std::vector<HistEntry> entries;
    entries.reserve(hist.size());
    for (auto [key, count] : hist)
        entries.push_back({{static_cast<std::uint8_t>(key >> 16), static_cast<std::uint8_t>(key >> 8), static_cast<std::uint8_t>(key)}, count});
    // Deterministic order (unordered_map iteration order is not).
    std::sort(entries.begin(), entries.end(), [](const HistEntry& a, const HistEntry& b) { return a.rgb < b.rgb; });

    // A masked image keeps index 255 for the transparent pixels.
    const std::size_t max_colors = opaque ? 255 : 256;
    std::vector<std::array<std::uint8_t, 3>> colors;
    if (!entries.empty())
        colors = median_cut(std::move(entries), max_colors);
    IndexedImage out{img.width, img.height, std::vector<std::uint8_t>(n), std::move(colors)};
    const auto& palette = out.palette;

    std::unordered_map<std::uint32_t, std::uint8_t> index_of;
    for (const auto& [key, count] : hist) {
        const int r = key >> 16, g = (key >> 8) & 0xFF, b = key & 0xFF;
        int best = 0, best_d = 1 << 30;
        for (std::size_t i = 0; i < palette.size(); ++i) {
            const int dr = r - palette[i][0], dg = g - palette[i][1], db = b - palette[i][2];
            const int d = dr * dr + dg * dg + db * db;
            if (d < best_d) {
                best_d = d;
                best = static_cast<int>(i);
            }
        }
        index_of[key] = static_cast<std::uint8_t>(best);
    }
    for (std::size_t i = 0; i < n; ++i)
        out.indices[i] = is_opaque(opaque, i) ? index_of[key_of(i)] : static_cast<std::uint8_t>(kTransparentIndex);
    if (opaque)
        finish_masked(out);
    return out;
}

namespace {

// Palette entries as doubles, for the refinement and the dithering.
using Palette = std::vector<std::array<double, 3>>;

std::size_t nearest_entry(const Palette& pal, const std::array<double, 3>& c) noexcept
{
    std::size_t best = 0;
    double best_d = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < pal.size(); ++i) {
        const double dr = c[0] - pal[i][0], dg = c[1] - pal[i][1], db = c[2] - pal[i][2];
        const double d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

} // namespace

IndexedImage quantize_refined(const RgbImage& img, const std::vector<bool>* opaque)
{
    const std::size_t n = img.pixel_count();
    auto key_of = [&](std::size_t i) {
        const std::uint8_t* p = img.pixel(i);
        return (std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2];
    };

    // Distinct colours with their pixel counts, in a fixed order.
    std::unordered_map<std::uint32_t, std::uint32_t> hist;
    for (std::size_t i = 0; i < n; ++i)
        if (is_opaque(opaque, i))
            ++hist[key_of(i)];
    std::vector<HistEntry> entries;
    entries.reserve(hist.size());
    for (const auto& [key, count] : hist)
        entries.push_back({{std::uint8_t(key >> 16), std::uint8_t(key >> 8), std::uint8_t(key)}, count});
    std::sort(entries.begin(), entries.end(), [](const HistEntry& a, const HistEntry& b) { return a.rgb < b.rgb; });

    // Start from median cut, then let each entry move to the weighted mean of the
    // colours it represents (k-means on the histogram). Usually converges in a few passes.
    Palette pal;
    if (!entries.empty())   // a masked image keeps index 255 for the transparent pixels
        for (const auto& c : median_cut(entries, opaque ? 255 : 256))
            pal.push_back({double(c[0]), double(c[1]), double(c[2])});
    else
        pal.push_back({0.0, 0.0, 0.0});
    for (int pass = 0; pass < 8; ++pass) {
        Palette sum(pal.size(), {0.0, 0.0, 0.0});
        std::vector<double> weight(pal.size(), 0.0);
        for (const HistEntry& e : entries) {
            const std::array<double, 3> c{double(e.rgb[0]), double(e.rgb[1]), double(e.rgb[2])};
            const std::size_t k = nearest_entry(pal, c);
            for (int ch = 0; ch < 3; ++ch)
                sum[k][ch] += c[ch] * e.count;
            weight[k] += e.count;
        }
        for (std::size_t k = 0; k < pal.size(); ++k)
            if (weight[k] > 0)
                for (int ch = 0; ch < 3; ++ch)
                    pal[k][ch] = sum[k][ch] / weight[k];
    }

    // Map every pixel to the palette with Floyd-Steinberg error diffusion, so
    // gradients dither instead of banding.
    IndexedImage out{img.width, img.height, std::vector<std::uint8_t>(n), {}};
    for (const auto& c : pal)
        out.palette.push_back({static_cast<std::uint8_t>(std::lround(std::clamp(c[0], 0.0, 255.0))),
                               static_cast<std::uint8_t>(std::lround(std::clamp(c[1], 0.0, 255.0))),
                               static_cast<std::uint8_t>(std::lround(std::clamp(c[2], 0.0, 255.0)))});

    const std::size_t w = static_cast<std::size_t>(img.width);
    std::vector<double> err_now(3 * (w + 2), 0.0), err_next(3 * (w + 2), 0.0);
    for (int y = 0; y < img.height; ++y) {
        std::fill(err_next.begin(), err_next.end(), 0.0);
        for (std::size_t x = 0; x < w; ++x) {
            if (!is_opaque(opaque, static_cast<std::size_t>(y) * w + x)) {
                out.indices[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint8_t>(kTransparentIndex);
                continue;   // transparent: nothing to match and no error to hand on
            }
            const std::uint8_t* p = img.pixel(static_cast<int>(x), y);
            std::array<double, 3> want{};
            for (int ch = 0; ch < 3; ++ch)
                want[ch] = std::clamp(p[ch] + err_now[3 * (x + 1) + ch], 0.0, 255.0);
            const std::size_t k = nearest_entry(pal, want);
            out.indices[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint8_t>(k);
            for (int ch = 0; ch < 3; ++ch) {
                const double e = want[ch] - pal[k][ch];
                err_now[3 * (x + 2) + ch] += e * 7.0 / 16.0;
                err_next[3 * x + ch] += e * 3.0 / 16.0;
                err_next[3 * (x + 1) + ch] += e * 5.0 / 16.0;
                err_next[3 * (x + 2) + ch] += e * 1.0 / 16.0;
            }
        }
        std::swap(err_now, err_next);
    }
    if (opaque)
        finish_masked(out);
    return out;
}

IndexedImage export_texture(const TextureSettings& s)
{
    const bool masked = s.blend == BlendMode::Masked;
    if (s.file.empty()) {
        if (!masked)
            return quantize(make_export_texture(s));   // procedural: the same as always
        const MaskedTexture texture = make_masked_texture(s);
        return quantize(texture.colors, &texture.opaque);
    }

    const RgbaImage fitted = fit_to_square_rgba(load_image_file(from_utf8(s.file)), s.size);
    RgbImage colors(fitted.width, fitted.height);
    std::vector<bool> opaque(fitted.pixel_count());
    for (std::size_t i = 0; i < fitted.pixel_count(); ++i) {
        const std::uint8_t* p = fitted.pixel(i);   // colour times alpha, then alpha
        if (masked) {
            opaque[i] = p[3] >= 128;
            for (int c = 0; c < 3; ++c)   // back to the colour itself, without the alpha
                colors.pixel(i)[c] = p[3] == 0 ? 0 : static_cast<std::uint8_t>(std::min(255, (p[c] * 255 + p[3] / 2) / p[3]));
        } else {
            std::copy_n(p, 3, colors.pixel(i));
        }
    }
    return masked ? quantize_refined(colors, &opaque) : quantize_refined(colors);
}

RgbImage expand(const IndexedImage& img)
{
    RgbImage out(img.width, img.height);
    for (std::size_t i = 0; i < img.indices.size(); ++i)
        std::ranges::copy(img.palette[img.indices[i]], out.pixel(i));
    return out;
}

RgbaImage expand_rgba(const IndexedImage& img)
{
    RgbaImage out(img.width, img.height);
    for (std::size_t i = 0; i < img.indices.size(); ++i) {
        std::copy_n(img.palette[img.indices[i]].begin(), 3, out.pixel(i));
        out.pixel(i)[3] = img.masked && img.indices[i] == kTransparentIndex ? 0 : 255;
    }
    return out;
}

RgbaImage to_rgba(const RgbImage& img)
{
    RgbaImage out(img.width, img.height);
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        std::copy_n(img.pixel(i), 3, out.pixel(i));
        out.pixel(i)[3] = 255;
    }
    return out;
}

std::vector<std::uint8_t> encode_indexed_bmp(const RgbImage& img)
{
    return encode_indexed_bmp(quantize(img));
}

std::vector<std::uint8_t> encode_indexed_bmp(const IndexedImage& img)
{
    const auto& palette = img.palette;
    const std::uint32_t row_stride = (static_cast<std::uint32_t>(img.width) + 3) & ~3u;
    const std::uint32_t palette_bytes = 256 * 4;
    const std::uint32_t pixel_offset = 14 + 40 + palette_bytes;
    const std::uint32_t image_bytes = row_stride * static_cast<std::uint32_t>(img.height);

    std::vector<std::uint8_t> out;
    out.reserve(pixel_offset + image_bytes);
    // BITMAPFILEHEADER
    out.push_back('B');
    out.push_back('M');
    put_u32(out, pixel_offset + image_bytes);
    put_u32(out, 0);
    put_u32(out, pixel_offset);
    // BITMAPINFOHEADER
    put_u32(out, 40);
    put_u32(out, static_cast<std::uint32_t>(img.width));
    put_u32(out, static_cast<std::uint32_t>(img.height));   // positive: bottom-up rows
    put_u16(out, 1);                           // planes
    put_u16(out, 8);                           // bits per pixel
    put_u32(out, 0);                           // BI_RGB
    put_u32(out, image_bytes);
    put_u32(out, 3780);   // 96 dpi
    put_u32(out, 3780);
    put_u32(out, 256);    // colors used
    put_u32(out, 256);    // colors important
    // Palette (BGRX); unused entries stay black.
    for (std::size_t i = 0; i < 256; ++i) {
        const auto c = i < palette.size() ? palette[i] : std::array<std::uint8_t, 3>{0, 0, 0};
        out.push_back(c[2]);
        out.push_back(c[1]);
        out.push_back(c[0]);
        out.push_back(0);
    }
    for (int y = img.height - 1; y >= 0; --y) {
        for (int x = 0; x < img.width; ++x)
            out.push_back(img.indices[static_cast<std::size_t>(y) * img.width + x]);
        for (std::uint32_t pad = img.width; pad < row_stride; ++pad)
            out.push_back(0);
    }

    return out;
}

} // namespace gs
