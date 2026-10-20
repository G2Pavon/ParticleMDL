#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ImageImport.hpp"
#include "PathUtil.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;

    explicit TempDir(const char* name) : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// The header of a 24-bit BMP of the given size with no pixel data after it: all
// a program needs to learn how large the picture claims to be.
std::vector<std::uint8_t> bmp_header_only(std::uint32_t width, std::uint32_t height)
{
    std::vector<std::uint8_t> out{'B', 'M'};
    put32(out, 54);
    put32(out, 0);
    put32(out, 54);
    put32(out, 40);
    put32(out, width);
    put32(out, height);
    out.push_back(1);
    out.push_back(0);
    out.push_back(24);
    out.push_back(0);
    for (int i = 0; i < 6; ++i)
        put32(out, 0);
    return out;
}

void write(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));
}

RgbaImage filled(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
    RgbaImage img(w, h);
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        std::uint8_t* p = img.pixel(i);
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }
    return img;
}

std::string error_of(const fs::path& path)
{
    try {
        load_image_file(path);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

} // namespace

TEST_CASE("a picture that claims to be huge is refused from its header")
{
    const TempDir dir("particlemdl_image_huge");
    const fs::path file = dir.path / "huge.bmp";
    write(file, bmp_header_only(20000, 20000));   // would be 1.6 GB decoded

    const auto start = std::chrono::steady_clock::now();
    const std::string message = error_of(file);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK(message.find("too large") != std::string::npos);
    CHECK(message.find("20000 x 20000") != std::string::npos);
    CHECK(message.find(std::to_string(kMaxImageDimension)) != std::string::npos);

    write(file, bmp_header_only(kMaxImageDimension + 1, 16));
    CHECK(error_of(file).find("too large") != std::string::npos);
}

TEST_CASE("a file that is empty or not an image is reported")
{
    const TempDir dir("particlemdl_image_bad");
    write(dir.path / "empty.png", {});
    CHECK(error_of(dir.path / "empty.png").find("empty") != std::string::npos);

    write(dir.path / "text.png", {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'});
    CHECK(error_of(dir.path / "text.png").find("cannot read image") != std::string::npos);

    CHECK(error_of(dir.path / "missing.png").find("not found") != std::string::npos);
}

TEST_CASE("the size limit is on each side, and a picture exactly at it is not refused for its size")
{
    const TempDir dir("particlemdl_image_limit");
    const fs::path file = dir.path / "edge.bmp";
    // Only the header matters here: 4096 x 1 is within the limit, 4096 x 4097 and 4097 x 1 are not.
    write(file, bmp_header_only(kMaxImageDimension, 1));
    CHECK(error_of(file).find("too large") == std::string::npos);
    write(file, bmp_header_only(1, kMaxImageDimension + 1));
    CHECK(error_of(file).find("too large") != std::string::npos);
    write(file, bmp_header_only(kMaxImageDimension + 1, 1));
    CHECK(error_of(file).find("too large") != std::string::npos);
}

TEST_CASE("fit_to_square keeps a flat colour flat, enlarged or reduced")
{
    for (const int source : {2, 16, 64, 300, 1000}) {
        CAPTURE(source);
        const RgbImage out = fit_to_square(filled(source, source, 200, 100, 50), 64);
        for (std::size_t i = 0; i < out.pixel_count(); ++i) {
            CHECK(std::abs(out.pixel(i)[0] - 200) <= 1);
            CHECK(std::abs(out.pixel(i)[1] - 100) <= 1);
            CHECK(std::abs(out.pixel(i)[2] - 50) <= 1);
        }
    }
}

TEST_CASE("fit_to_square averages a reduced picture instead of picking pixels")
{
    // Pixel noise: single samples would stay noisy, an average of 64 pixels is nearly flat.
    RgbaImage noise(256, 256);
    std::uint32_t state = 99;
    for (std::size_t i = 0; i < noise.pixel_count(); ++i) {
        state = state * 1664525u + 1013904223u;
        const std::uint8_t v = (state >> 24) & 1 ? 255 : 0;
        std::uint8_t* p = noise.pixel(i);
        p[0] = p[1] = p[2] = v;
        p[3] = 255;
    }
    const RgbImage out = fit_to_square(noise, 32);   // each output pixel covers 8 x 8 source pixels

    double sum = 0, sum_sq = 0;
    const std::size_t n = out.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const double v = out.pixel(i)[0];
        sum += v;
        sum_sq += v * v;
    }
    const double count = static_cast<double>(n);
    const double mean = sum / count, deviation = std::sqrt(sum_sq / count - mean * mean);
    CHECK(mean == doctest::Approx(127.5).epsilon(0.05));
    CHECK(deviation < 25.0);   // a single pixel per output would give about 127
}

TEST_CASE("fit_to_square weighs the pixels a reduced picture covers only in part")
{
    // 3 pixels wide into 2: each output pixel covers 1.5 source pixels. Black, white, black.
    RgbaImage img(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            std::uint8_t* p = img.pixel(x, y);
            p[0] = p[1] = p[2] = x == 1 ? 255 : 0;
            p[3] = 255;
        }
    const RgbImage out = fit_to_square(img, 2);
    // Left output: 1 black + half the white pixel = a third of 1.5 is white... 0.5 / 1.5 = 85
    CHECK(out.pixel(0, 0)[0] == 85);
    CHECK(out.pixel(1, 0)[0] == 85);
}

TEST_CASE("fit_to_square centers a wide picture and leaves the rest black")
{
    const RgbImage out = fit_to_square(filled(128, 64, 255, 255, 255), 64);   // fits as 64 x 32
    for (int x = 0; x < 64; ++x) {
        CHECK(out.pixel(x, 0)[0] == 0);       // top bar: rows 0..15
        CHECK(out.pixel(x, 15)[0] == 0);
        CHECK(out.pixel(x, 16)[0] == 255);    // the picture: rows 16..47
        CHECK(out.pixel(x, 47)[0] == 255);
        CHECK(out.pixel(x, 48)[0] == 0);      // bottom bar
        CHECK(out.pixel(x, 63)[0] == 0);
    }
}

TEST_CASE("fit_to_square lets transparent pixels add nothing")
{
    const RgbImage clear = fit_to_square(filled(32, 32, 255, 0, 0, 0), 64);
    for (std::size_t i = 0; i < clear.pixel_count(); ++i)
        CHECK(clear.pixel(i)[0] == 0);

    const RgbImage half = fit_to_square(filled(64, 64, 200, 200, 200, 128), 64);
    CHECK(std::abs(half.pixel(10, 10)[0] - 100) <= 1);   // premultiplied by 128/255
}

TEST_CASE("fit_to_square returns a picture of the same size unchanged")
{
    RgbaImage img(16, 16);
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        std::uint8_t* p = img.pixel(i);
        p[0] = static_cast<std::uint8_t>(i);
        p[1] = static_cast<std::uint8_t>(255 - i);
        p[2] = static_cast<std::uint8_t>(i * 3);
        p[3] = 255;
    }
    const RgbImage out = fit_to_square(img, 16);
    for (std::size_t i = 0; i < img.pixel_count(); ++i)
        for (int c = 0; c < 3; ++c)
            CHECK(out.pixel(i)[c] == img.pixel(i)[c]);
}
