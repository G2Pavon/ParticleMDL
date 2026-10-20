#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "PathUtil.hpp"
#include "TextureJob.hpp"

using namespace gs;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// Polls until the job has something to say, or gives up after a few seconds.
std::optional<TextureJob::Result> wait_for_result(TextureJob& job)
{
    const auto give_up = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < give_up) {
        if (auto result = job.poll())
            return result;
        if (!job.busy())
            return std::nullopt;
        std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
}

fs::path write_picture(const char* name, int size)
{
    const fs::path path = fs::temp_directory_path() / name;
    RgbImage img(size, size);
    for (std::size_t i = 0; i < img.pixels.size(); ++i)
        img.pixels[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
    const std::vector<std::uint8_t> bytes = encode_indexed_bmp(img);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("a pattern texture is made on the worker")
{
    TextureJob job;
    CHECK_FALSE(job.busy());
    TextureSettings s;
    s.size = 128;
    job.request(s);
    CHECK(job.busy());

    const auto result = wait_for_result(job);
    REQUIRE(result);
    REQUIRE(result->image);
    CHECK(result->image->width == 128);
    CHECK(result->settings == s);
    CHECK(result->error.empty());
    CHECK_FALSE(job.busy());
    CHECK_FALSE(job.poll());   // reported once
}

TEST_CASE("a picture is imported on the worker")
{
    const fs::path file = write_picture("particlemdl_texture_job.bmp", 300);
    TextureJob job;
    TextureSettings s;
    s.file = to_utf8(file);
    s.size = 64;
    job.request(s);
    const auto result = wait_for_result(job);
    fs::remove(file);

    REQUIRE(result);
    REQUIRE(result->image);
    CHECK(result->image->width == 64);
    CHECK(result->image->height == 64);
}

TEST_CASE("a picture that cannot be read comes back as an error")
{
    TextureJob job;
    TextureSettings s;
    s.file = to_utf8(fs::temp_directory_path() / "particlemdl_no_such_picture.png");
    job.request(s);
    const auto result = wait_for_result(job);
    REQUIRE(result);
    CHECK_FALSE(result->image);
    CHECK(result->error.find("not found") != std::string::npos);
    CHECK(result->settings == s);
}

TEST_CASE("only the newest request is answered")
{
    const fs::path big = write_picture("particlemdl_texture_job_big.bmp", 1500);   // slow enough to still be running
    TextureJob job;
    TextureSettings first, second, third;
    first.file = to_utf8(big);
    second.size = 128;
    third.size = 256;
    third.hardness = 3.0;

    job.request(first);
    job.request(second);   // replaced before it starts
    job.request(third);
    const auto result = wait_for_result(job);
    fs::remove(big);

    REQUIRE(result);
    CHECK(result->settings == third);
    REQUIRE(result->image);
    CHECK(result->image->width == 256);
    CHECK_FALSE(job.busy());
    CHECK_FALSE(job.poll());
}

TEST_CASE("the job can be asked again after it answered")
{
    TextureJob job;
    TextureSettings a, b;
    b.size = 128;
    job.request(a);
    REQUIRE(wait_for_result(job));
    job.request(b);
    const auto result = wait_for_result(job);
    REQUIRE(result);
    CHECK(result->settings == b);
}

TEST_CASE("destroying a job that is running waits for it and does not leak")
{
    const fs::path big = write_picture("particlemdl_texture_job_drop.bmp", 1200);
    {
        TextureJob job;
        TextureSettings s;
        s.file = to_utf8(big);
        job.request(s);
        TextureSettings t;
        t.size = 128;
        job.request(t);   // pending when the job goes away
    }
    fs::remove(big);
    CHECK_FALSE(fs::exists(big));   // reached the end without a hang or a crash
}
