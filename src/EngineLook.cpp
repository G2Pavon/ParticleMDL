#include "EngineLook.hpp"

#include <algorithm>
#include <cmath>

namespace gs {

const std::array<std::uint8_t, 256>& texture_gamma_table()
{
    // gamma.c, BuildGammaTable for gamma 2.5 and texgamma 2.0, its float and double steps kept as they
    // are: the table is exact to the engine's only when every rounding happens where it does.
    static const std::array<std::uint8_t, 256> table = [] {
        const float gamma = 2.5f, texgamma = 2.0f;
        const float g1 = static_cast<float>(1.0 / static_cast<double>(gamma));   // "1.0 / gamma": in double
        const float g2 = g1 * texgamma;
        std::array<std::uint8_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float d = static_cast<float>(std::pow(i / 255.0, static_cast<double>(g2)));
            const int inf = static_cast<int>(d * 255.0f);
            t[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(inf, 0, 255));
        }
        return t;
    }();
    return table;
}

IndexedImage with_texture_gamma(IndexedImage image)
{
    const auto& table = texture_gamma_table();
    for (auto& colour : image.palette)
        for (std::uint8_t& c : colour)
            c = table[c];
    return image;
}

std::vector<RgbaImage> engine_mipmaps(const RgbaImage& image)
{
    std::vector<RgbaImage> levels{image};
    while (levels.back().width > 1 || levels.back().height > 1) {
        const RgbaImage& in = levels.back();
        RgbaImage out(std::max(1, in.width >> 1), std::max(1, in.height >> 1));
        for (int y = 0; y < out.height; ++y) {
            const int y0 = y * 2, y1 = y0 + 1 < in.height ? y0 + 1 : y0;
            for (int x = 0; x < out.width; ++x) {
                const int x0 = x * 2;
                std::uint8_t* o = out.pixel(x, y);
                for (int c = 0; c < 4; ++c) {
                    if (x0 + 1 < in.width)
                        o[c] = static_cast<std::uint8_t>(
                            (in.pixel(x0, y0)[c] + in.pixel(x0 + 1, y0)[c] + in.pixel(x0, y1)[c] + in.pixel(x0 + 1, y1)[c]) >> 2);
                    else
                        o[c] = static_cast<std::uint8_t>((in.pixel(x0, y0)[c] + in.pixel(x0, y1)[c]) >> 1);
                }
            }
        }
        levels.push_back(std::move(out));
    }
    return levels;
}

} // namespace gs
