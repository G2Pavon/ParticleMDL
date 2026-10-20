#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "ImageImport.hpp"
#include "PathUtil.hpp"
#include "SettingsJson.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

// A PNG of RGBA pixels, written with stored (uncompressed) deflate blocks.
std::vector<std::uint8_t> make_png(const RgbaImage& img)
{
    static const auto crc_table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    auto crc = [&](const std::vector<std::uint8_t>& bytes) {
        std::uint32_t c = 0xFFFFFFFFu;
        for (const std::uint8_t b : bytes)
            c = crc_table[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    auto be32 = [](std::vector<std::uint8_t>& out, std::uint32_t v) {
        for (int i = 3; i >= 0; --i)
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };

    std::vector<std::uint8_t> raw;   // each row: filter byte 0, then the pixels
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < img.width; ++x)
            raw.insert(raw.end(), img.pixel(x, y), img.pixel(x, y) + 4);
    }
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::size_t pos = 0; pos < raw.size();) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        for (std::size_t i = pos; i < pos + n; ++i) {
            a = (a + raw[i]) % 65521;
            b = (b + a) % 65521;
        }
        pos += n;
    }
    be32(z, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto chunk = [&](const char* type, const std::vector<std::uint8_t>& data) {
        be32(png, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        be32(png, crc(body));
    };
    std::vector<std::uint8_t> header;
    be32(header, static_cast<std::uint32_t>(img.width));
    be32(header, static_cast<std::uint32_t>(img.height));
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", header);
    chunk("IDAT", z);
    chunk("IEND", {});
    return png;
}

struct TempDir {
    fs::path path;

    explicit TempDir(const char* name) : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

fs::path write_png(const TempDir& dir, const char* name, const RgbaImage& img)
{
    const fs::path file = dir.path / name;
    const std::vector<std::uint8_t> bytes = make_png(img);
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file;
}

TextureSettings masked_pattern()
{
    TextureSettings t;
    t.blend = BlendMode::Masked;
    t.size = 64;
    return t;
}

std::string qc_of(const Settings& s)
{
    const TempDir dir("particlemdl_blend_qc");
    export_model(ParticleSystem(s), dir.path);
    std::ifstream in(dir.path / "custom_particles.qc");
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("blend defaults to additive, is saved by name and absent from old files")
{
    CHECK(TextureSettings{}.blend == BlendMode::Additive);
    CHECK_FALSE(TextureSettings{}.fullbright);

    Settings s;
    s.texture.blend = BlendMode::Masked;
    s.texture.fullbright = true;
    const nlohmann::json j = to_json(s);
    CHECK(j.at("blend_mode") == "masked");
    CHECK(j.at("fullbright") == true);
    const Settings back = settings_from_json(j, Settings{}).settings;
    CHECK(back.texture.blend == BlendMode::Masked);
    CHECK(back.texture.fullbright);

    nlohmann::json old = to_json(Settings{});
    old.erase("blend_mode");
    old.erase("fullbright");
    CHECK(settings_from_json(old, Settings{}).settings.texture.blend == BlendMode::Additive);

    Settings invalid;
    invalid.texture.blend = static_cast<BlendMode>(9);
    CHECK(validate(invalid));
    CHECK(invalid.texture.blend == BlendMode::Additive);

    TextureSettings a, b;
    b.blend = BlendMode::Solid;
    CHECK(a != b);   // the texture is rebuilt when the blend changes
    b = a;
    b.fullbright = true;
    CHECK(a != b);
}

TEST_CASE("a masked pattern is transparent exactly where the pattern is weak")
{
    const TextureSettings t = masked_pattern();
    const IndexedImage image = export_texture(t);
    REQUIRE(image.masked);
    REQUIRE(image.palette.size() == 256);
    CHECK(image.palette[kTransparentIndex] == kTransparentColor);

    const MaskedTexture reference = make_masked_texture(t);
    int transparent = 0;
    for (std::size_t i = 0; i < image.indices.size(); ++i) {
        const bool is_transparent = image.indices[i] == kTransparentIndex;
        CHECK(is_transparent == !reference.opaque[i]);
        transparent += is_transparent;
    }
    // A radial orb: the middle is drawn, the corners are not.
    CHECK(transparent > 500);
    CHECK(transparent < 3500);
    CHECK(image.indices[32 * 64 + 32] != kTransparentIndex);
    CHECK(image.indices[0] == kTransparentIndex);
    CHECK(image.indices[63] == kTransparentIndex);

    // The strong pixels keep the full colour, not the colour times the intensity.
    const auto& middle = image.palette[image.indices[32 * 64 + 32]];
    CHECK(middle[0] == doctest::Approx(t.core_color.r).epsilon(0.02));
    CHECK(middle[1] == doctest::Approx(t.core_color.g).epsilon(0.02));
    CHECK(middle[2] == doctest::Approx(t.core_color.b).epsilon(0.05));
}

TEST_CASE("a masked texture keeps index 255 for the transparent colour whatever the colours")
{
    TextureSettings t = masked_pattern();
    t.color_mode = ColorMode::Gradient;   // many shades
    t.core_color = {255, 40, 10};
    t.edge_color = {10, 60, 255};
    t.pattern = TexPattern::SoftCloud;
    t.size = 256;
    const IndexedImage image = export_texture(t);
    std::set<int> used;
    for (const std::uint8_t i : image.indices)
        used.insert(i);
    CHECK(used.count(kTransparentIndex) == 1);
    used.erase(kTransparentIndex);
    CHECK(used.size() <= 255);
    for (const int i : used)
        CHECK(i < kTransparentIndex);
}

TEST_CASE("additive and solid textures are not masked")
{
    for (const BlendMode blend : {BlendMode::Additive, BlendMode::Solid}) {
        TextureSettings t;
        t.blend = blend;
        const IndexedImage image = export_texture(t);
        CHECK_FALSE(image.masked);
        CHECK(image.palette.size() <= 256);

        const RgbaImage rgba = expand_rgba(image);
        for (std::size_t i = 0; i < rgba.pixel_count(); ++i)
            CHECK(rgba.pixel(i)[3] == 255);
    }
}

TEST_CASE("expand_rgba makes the transparent index of a masked image see-through")
{
    const IndexedImage image = export_texture(masked_pattern());
    const RgbaImage rgba = expand_rgba(image);
    int clear = 0;
    for (std::size_t i = 0; i < rgba.pixel_count(); ++i) {
        const bool transparent = image.indices[i] == kTransparentIndex;
        CHECK(rgba.pixel(i)[3] == (transparent ? 0 : 255));
        clear += transparent;
    }
    CHECK(clear > 0);
}

TEST_CASE("an all-transparent masked texture is still a valid image")
{
    TextureSettings t = masked_pattern();
    t.hardness = kMaxHardness;      // the pattern never reaches the threshold
    t.core_color = {0, 0, 0};
    const IndexedImage image = export_texture(t);
    CHECK(image.masked);
    CHECK(image.palette.size() == 256);
    CHECK(encode_indexed_bmp(image).size() == 14 + 40 + 1024 + 64 * 64);
}

TEST_CASE("fit_to_square_rgba keeps the alpha, zero outside the picture")
{
    RgbaImage wide(128, 64);
    for (std::size_t i = 0; i < wide.pixel_count(); ++i) {
        std::uint8_t* p = wide.pixel(i);
        p[0] = 200;
        p[1] = 100;
        p[2] = 50;
        p[3] = 255;
    }
    const RgbaImage out = fit_to_square_rgba(wide, 64);   // fits as 64 x 32, rows 16..47
    CHECK(out.pixel(10, 0)[3] == 0);
    CHECK(out.pixel(10, 15)[3] == 0);
    CHECK(out.pixel(10, 16)[3] == 255);
    CHECK(out.pixel(10, 47)[3] == 255);
    CHECK(out.pixel(10, 48)[3] == 0);
    CHECK(out.pixel(10, 30)[0] == 200);

    // Half-transparent pixels: the colour is multiplied by the alpha, the alpha is itself.
    RgbaImage half(64, 64);
    for (std::size_t i = 0; i < half.pixel_count(); ++i) {
        std::uint8_t* p = half.pixel(i);
        p[0] = 200;
        p[3] = 128;
    }
    const RgbaImage out_half = fit_to_square_rgba(half, 64);
    CHECK(out_half.pixel(5, 5)[3] == 128);
    CHECK(std::abs(out_half.pixel(5, 5)[0] - 100) <= 1);
    // The three-channel version is the colour part of it.
    CHECK(fit_to_square(half, 64).pixel(5, 5)[0] == out_half.pixel(5, 5)[0]);
}

TEST_CASE("a picture with an alpha channel is cut out when masked and black behind when additive")
{
    const TempDir dir("particlemdl_blend_png");
    RgbaImage disc(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const bool inside = std::hypot(x - 31.5, y - 31.5) < 20;
            std::uint8_t* p = disc.pixel(x, y);
            p[0] = 230;
            p[1] = 90;
            p[2] = 20;
            p[3] = inside ? 255 : 0;
        }
    const fs::path file = write_png(dir, "disc.png", disc);

    TextureSettings masked;
    masked.file = to_utf8(file);
    masked.blend = BlendMode::Masked;
    masked.size = 64;
    const IndexedImage cut = export_texture(masked);
    REQUIRE(cut.masked);
    CHECK(cut.indices[32 * 64 + 32] != kTransparentIndex);
    CHECK(cut.indices[0] == kTransparentIndex);
    CHECK(cut.indices[5 * 64 + 5] == kTransparentIndex);
    const auto& inside = cut.palette[cut.indices[32 * 64 + 32]];
    CHECK(std::abs(inside[0] - 230) <= 6);   // the picture's colour, not dimmed by anything
    CHECK(std::abs(inside[1] - 90) <= 6);

    TextureSettings additive = masked;
    additive.blend = BlendMode::Additive;
    const IndexedImage glow = export_texture(additive);
    CHECK_FALSE(glow.masked);
    const auto& corner = glow.palette[glow.indices[0]];
    CHECK(corner == std::array<std::uint8_t, 3>{0, 0, 0});   // transparent pixels add nothing
}

TEST_CASE("the bars around a picture that is not square are transparent in a masked texture")
{
    const TempDir dir("particlemdl_blend_bars");
    RgbaImage wide(128, 64);
    for (std::size_t i = 0; i < wide.pixel_count(); ++i) {
        std::uint8_t* p = wide.pixel(i);
        p[0] = 50;
        p[1] = 200;
        p[2] = 50;
        p[3] = 255;
    }
    TextureSettings t;
    t.file = to_utf8(write_png(dir, "wide.png", wide));
    t.blend = BlendMode::Masked;
    t.size = 64;
    const IndexedImage image = export_texture(t);
    CHECK(image.indices[3 * 64 + 20] == kTransparentIndex);    // top bar
    CHECK(image.indices[60 * 64 + 20] == kTransparentIndex);   // bottom bar
    CHECK(image.indices[32 * 64 + 20] != kTransparentIndex);   // the picture
}

TEST_CASE("the QC names the render flags the settings ask for")
{
    auto settings = [](BlendMode blend, bool fullbright) {
        Settings s;
        s.texture.blend = blend;
        s.texture.fullbright = fullbright;
        return s;
    };
    const std::string additive = qc_of(settings(BlendMode::Additive, false));
    CHECK(additive.find("$texrendermode \"custom_particles.bmp\" additive\n") != std::string::npos);
    CHECK(additive.find("masked") == std::string::npos);
    CHECK(additive.find("fullbright") == std::string::npos);

    const std::string masked = qc_of(settings(BlendMode::Masked, false));
    CHECK(masked.find("$texrendermode \"custom_particles.bmp\" masked\n") != std::string::npos);
    CHECK(masked.find("additive") == std::string::npos);

    const std::string both = qc_of(settings(BlendMode::Additive, true));
    CHECK(both.find("\" additive\n") != std::string::npos);
    CHECK(both.find("\" fullbright\n") != std::string::npos);

    const std::string solid = qc_of(settings(BlendMode::Solid, false));
    CHECK(solid.find("$texrendermode") == std::string::npos);

    const std::string bright_solid = qc_of(settings(BlendMode::Solid, true));
    CHECK(bright_solid.find("\" fullbright\n") != std::string::npos);
    CHECK(bright_solid.find("additive") == std::string::npos);
    CHECK(bright_solid.find("masked") == std::string::npos);
}

TEST_CASE("every blend mode exports a BMP the right size and within the limits")
{
    for (const BlendMode blend : {BlendMode::Additive, BlendMode::Masked, BlendMode::Solid}) {
        Settings s;
        s.texture.blend = blend;
        s.texture.size = 128;
        const TempDir dir("particlemdl_blend_export");
        export_model(ParticleSystem(s), dir.path);
        CHECK(fs::file_size(dir.path / "custom_particles.bmp") == 14 + 40 + 1024 + 128 * 128);
        CHECK(analyze_export(ParticleSystem(s)).blockers.empty());
    }
}
