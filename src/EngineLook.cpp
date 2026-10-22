#include "EngineLook.hpp"

#include <algorithm>
#include <cmath>

namespace gs {

namespace {

// gamma.c, BuildGammaTable, with its float and double steps kept as they are: the tables are
// exact to the engine's only when every rounding happens where it does.
GammaTables build(const float gamma, const float brightness, const float texgamma, const float lightgamma)
{
    GammaTables t{};
    const float g1 = gamma != 0.0f ? static_cast<float>(1.0 / static_cast<double>(gamma)) : 0.4f;   // "1.0 / gamma": in double
    const float g2 = g1 * texgamma;
    float g3;
    if (brightness <= 0.0f)
        g3 = 0.125f;
    else if (brightness <= 1.0f)
        g3 = 0.125f - brightness * brightness * 0.075f;
    else
        g3 = 0.05f;

    for (int i = 0; i < 256; ++i) {
        const float d = static_cast<float>(std::pow(i / 255.0, static_cast<double>(g2)));
        const int inf = static_cast<int>(d * 255.0f);
        t.texture[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(inf, 0, 255));
    }
    for (int i = 0; i < 1024; ++i) {
        float f = static_cast<float>(std::pow(i / 1023.0, static_cast<double>(lightgamma)));
        if (brightness > 1.0f)
            f *= brightness;
        // The constants are doubles in the engine, so these are worked out in double, then stored in a float.
        if (f <= g3)
            f = static_cast<float>(static_cast<double>(f / g3) * 0.125);
        else
            f = static_cast<float>((static_cast<double>(f - g3) / (1.0 - static_cast<double>(g3))) * 0.875 + 0.125);
        const double d = std::pow(static_cast<double>(f), static_cast<double>(g1));
        const int inf = static_cast<int>(d * 1023.0);
        t.light[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(std::clamp(inf, 0, 1023));
    }
    return t;
}

std::uint8_t to_byte(float v)
{
    // A float stored in a byte, as the engine's VectorScale(..., 255, out) does: truncated.
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v), 0, 255));
}

} // namespace

const GammaTables& gamma_tables()
{
    static const GammaTables tables = build(2.5f, 0.0f, 2.0f, 2.5f);
    return tables;
}

IndexedImage with_texture_gamma(IndexedImage image)
{
    const auto& table = gamma_tables().texture;
    for (auto& colour : image.palette)
        for (std::uint8_t& c : colour)
            c = table[c];
    return image;
}

FaceColor face_color(const EntityLook& look, Vec3 normal)
{
    if (look.mode == EntityRenderMode::Additive) {
        // gl_studio.c: the light values are tr.blend (renderamt / 255) and so is the alpha.
        const float blend = static_cast<float>(std::clamp(look.renderamt, 0, 255)) / 255.0f;
        const std::uint8_t c = to_byte(blend * 255.0f);
        return {c, c, c, to_byte(blend * 255.0f)};
    }

    // ref_light.c, R_EntityDynamicLight, for a light from straight above and no dynamic lights:
    // the light's strength is its brightest channel, nine tenths of it shade and the rest ambient.
    const float lr = look.light.r, lg = look.light.g, lb = look.light.b;
    float total = std::max({lr, lg, lb});
    const float colour_total = total;
    if (total == 0.0f)
        total = 1.0f;
    const float shade_full = total * 0.9f;   // v_direct 0.9: the scale of the light vector
    float ambient = total - shade_full;
    float shade = shade_full;
    const float colour[3] = {colour_total > 0.0f ? lr / colour_total : 1.0f, colour_total > 0.0f ? lg / colour_total : 1.0f,
                             colour_total > 0.0f ? lb / colour_total : 1.0f};
    if (ambient > 128.0f)
        ambient = 128.0f;
    if (ambient + shade > 255.0f)
        shade = 255.0f - ambient;

    // gl_studio.c, R_StudioLighting: hemispheric lighting, the light going down (0, 0, -1).
    constexpr float kShadeLambert = 1.4953241f;
    const float len = static_cast<float>(std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z));
    float lightcos = len > 0.0f ? static_cast<float>(-normal.z) / len : 0.0f;
    lightcos = std::min(lightcos, 1.0f);
    float illum = ambient + shade;
    lightcos = (lightcos + (kShadeLambert - 1.0f)) / kShadeLambert;
    if (lightcos > 0.0f)
        illum -= shade * lightcos;
    illum = std::clamp(illum, 0.0f, 255.0f);
    const float lv = static_cast<float>(gamma_tables().light[static_cast<std::size_t>(illum * 4)]) / 1023.0f;

    return {to_byte(colour[0] * lv * 255.0f), to_byte(colour[1] * lv * 255.0f), to_byte(colour[2] * lv * 255.0f), 255};
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
