#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>

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

std::string qc_of(const Settings& s)
{
    return export_files(ParticleSystem(s)).bytes_of(".qc");
}

} // namespace

TEST_CASE("the blend settings of earlier versions are ignored, with a warning unless they asked for additive")
{
    nlohmann::json j = to_json(Settings{});
    CHECK_FALSE(j.contains("blend_mode"));
    CHECK_FALSE(j.contains("fullbright"));

    // What every earlier file holds unless the blend was changed: loads without a word.
    j["blend_mode"] = "additive";
    j["fullbright"] = false;
    const SettingsLoad quiet = settings_from_json(j, Settings{});
    CHECK(quiet.warnings.empty());
    CHECK(quiet.settings == Settings{});

    j["blend_mode"] = "masked";
    j["fullbright"] = true;
    const SettingsLoad loud = settings_from_json(j, Settings{});
    REQUIRE(loud.warnings.size() == 2);
    CHECK(loud.warnings[0] == "blend_mode \"masked\": ParticleMDL draws particles additively only (ignored)");
    CHECK(loud.warnings[1].starts_with("fullbright true: "));
    CHECK(loud.settings == Settings{});
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

TEST_CASE("the transparent part of a picture is black, so it adds nothing")
{
    const TempDir dir("particlemdl_additive_png");
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
    TextureSettings t;
    t.file = to_utf8(write_png(dir, "disc.png", disc));
    t.size = 64;
    const IndexedImage glow = export_texture(t);
    CHECK(glow.palette[glow.indices[0]] == std::array<std::uint8_t, 3>{0, 0, 0});
    const auto& inside = glow.palette[glow.indices[32 * 64 + 32]];
    CHECK(std::abs(inside[0] - 230) <= 6);
    CHECK(std::abs(inside[1] - 90) <= 6);
}

TEST_CASE("the QC asks for an additive texture and nothing else")
{
    const std::string qc = qc_of(Settings{});
    CHECK(qc.find("$texrendermode \"custom_particles.bmp\" additive\n") != std::string::npos);
    std::size_t lines = 0;
    for (std::size_t at = qc.find("$texrendermode"); at != std::string::npos; at = qc.find("$texrendermode", at + 1))
        ++lines;
    CHECK(lines == 1);
}

TEST_CASE("every texture size exports a BMP of the right size")
{
    for (const int size : kTextureSizes) {
        Settings s;
        s.texture.size = size;
        CHECK(export_files(ParticleSystem(s)).bytes_of(".bmp").size() ==
              static_cast<std::size_t>(14 + 40 + 1024 + size * size));
    }
}
