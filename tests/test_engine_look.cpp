#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "EngineLook.hpp"

using namespace gs;

namespace {

// The engine's own tables for the default settings, from its tests (see the fixture's header).
std::vector<std::vector<int>> engine_tables()
{
    std::ifstream in(std::filesystem::path(GS_FIXTURE_DIR) / "engine_texgamma_table.txt");
    REQUIRE(in);
    std::vector<std::vector<int>> tables;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<int> values;
        std::stringstream fields(line);
        std::string v;
        while (std::getline(fields, v, ','))
            values.push_back(std::stoi(v));
        tables.push_back(values);
    }
    return tables;
}

} // namespace

TEST_CASE("the texture gamma table is the engine's, entry for entry")
{
    const auto tables = engine_tables();
    REQUIRE(tables.size() == 1);
    REQUIRE(tables[0].size() == 256);
    for (std::size_t i = 0; i < 256; ++i) {
        CAPTURE(i);
        CHECK(texture_gamma_table()[i] == tables[0][i]);
    }
    // It brightens: dark and middle tones the most.
    CHECK(texture_gamma_table()[64] == 84);
    CHECK(texture_gamma_table()[128] == 146);
    CHECK(texture_gamma_table()[255] == 255);
}

TEST_CASE("a texture is loaded with its palette through the texture gamma")
{
    IndexedImage image;
    image.width = image.height = 1;
    image.indices.assign(1, 0);   // not "= {0}": GCC 13 warns of a false out-of-bounds copy there (-Warray-bounds)
    image.palette = {{0, 64, 128}, {200, 255, 10}};
    const IndexedImage game = with_texture_gamma(image);
    CHECK(game.indices == image.indices);
    CHECK(game.palette[0] == std::array<std::uint8_t, 3>{0, 84, 146});
    CHECK(game.palette[1] == std::array<std::uint8_t, 3>{texture_gamma_table()[200], 255, texture_gamma_table()[10]});
}

TEST_CASE("mipmaps average four pixels rounded down, as the engine makes them, down to one pixel")
{
    RgbaImage image(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) {
            std::uint8_t* p = image.pixel(x, y);
            p[0] = static_cast<std::uint8_t>(x * 10 + y);
            p[1] = 255;
            p[2] = 0;
            p[3] = 255;
        }
    const auto levels = engine_mipmaps(image);
    REQUIRE(levels.size() == 3);   // 4 x 2, 2 x 1, 1 x 1
    CHECK(levels[1].width == 2);
    CHECK(levels[1].height == 1);
    CHECK(levels[1].pixel(0, 0)[0] == (0 + 10 + 1 + 11) / 4);
    CHECK(levels[1].pixel(1, 0)[0] == (20 + 30 + 21 + 31) / 4);
    CHECK(levels[2].width == 1);
    // One row left: the engine takes the same row twice.
    CHECK(levels[2].pixel(0, 0)[0] == (levels[1].pixel(0, 0)[0] + levels[1].pixel(1, 0)[0] + levels[1].pixel(0, 0)[0] + levels[1].pixel(1, 0)[0]) / 4);
}

#include "PreviewScene.hpp"

namespace {

Settings one_still_quad()
{
    Settings s;
    s.count = 1;
    s.geometry = Geometry::SingleQuad;
    s.speed = s.burst = s.spread = s.gravity = 0;
    s.lifespan = 1.0;
    s.end_behavior = EndBehavior::EndlessLoop;
    return s;
}

// The direction the camera looks along.
Vec3 ahead(const OrbitCamera& camera)
{
    Vec3 best_d{};
    double best = -2;
    for (int i = 0; i < 360; i += 2)
        for (int j = -88; j <= 88; j += 2) {
            const double a = i * kDegToRad, e = j * kDegToRad;
            const Vec3 d{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)};
            if (const double depth = camera.direction_to_camera_space(d).depth; depth > best) {
                best = depth;
                best_d = d;
            }
        }
    return best_d;
}

} // namespace

TEST_CASE("a sequence that does not loop holds a thousandth of a frame before its end, as the engine does")
{
    Settings s = one_still_quad();
    s.loop = false;
    s.speed = 100;
    s.spread = 0;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    const int last = baked.steps();
    const ParticlePose& a = baked.at(last - 1, 0);
    const ParticlePose& b = baked.at(last, 0);
    REQUIRE(a.pos != b.pos);
    for (const double t : {sys.loop_duration(), sys.loop_duration() * 2}) {
        const SampledPose end = baked.sample(0, t);
        CHECK(end.pos.x == doctest::Approx(a.pos.x + (b.pos.x - a.pos.x) * 0.999));
        CHECK(end.pos.y == doctest::Approx(a.pos.y + (b.pos.y - a.pos.y) * 0.999));
    }
}

TEST_CASE("a one-sided quad shows the side the engine shows: the one its normal points away from")
{
    // studiomdl turns the SMD's triangles round: compiled, they wind counter-clockwise about the
    // normal (read from a model Sven Co-op's studiomdl made of this quad). The engine culls
    // GL_FRONT, the counter-clockwise faces, so the quad is seen from behind its normal.
    Settings s = one_still_quad();
    s.double_sided = false;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    const Vec3 n = sys.quads()[0][0].normal;
    SceneOptions options;
    options.grid = options.axes = options.bounds = false;
    int seen_from_behind = 0, hidden_in_front = 0;
    for (int step = 0; step < 8; ++step) {
        OrbitCamera camera;
        camera.orbit(step * 78.5 + 10, 0);   // around the quad, never edge on
        const Vec3 d = ahead(camera);
        const double facing = d.x * n.x + d.y * n.y + d.z * n.z;   // > 0: the camera is behind the normal
        Scene scene;
        build_scene(sys, baked, camera, 0.0, {400, 300}, options, scene);
        if (facing > 0.1) {
            CHECK(scene.faces.size() == 1);
            ++seen_from_behind;
        } else if (facing < -0.1) {
            CHECK(scene.faces.empty());
            ++hidden_in_front;
        }
    }
    CHECK(seen_from_behind > 0);
    CHECK(hidden_in_front > 0);
}
