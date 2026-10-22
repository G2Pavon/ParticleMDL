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
    std::ifstream in(std::filesystem::path(GS_FIXTURE_DIR) / "engine_gamma_tables.txt");
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

TEST_CASE("the gamma tables are the engine's, entry for entry")
{
    const auto tables = engine_tables();
    REQUIRE(tables.size() == 2);
    REQUIRE(tables[0].size() == 256);
    REQUIRE(tables[1].size() == 1024);
    for (std::size_t i = 0; i < 256; ++i) {
        CAPTURE(i);
        CHECK(gamma_tables().texture[i] == tables[0][i]);
    }
    for (std::size_t i = 0; i < 1024; ++i) {
        CAPTURE(i);
        CHECK(gamma_tables().light[i] == tables[1][i]);
    }
    // The texture gamma brightens: dark and middle tones the most.
    CHECK(gamma_tables().texture[64] == 84);
    CHECK(gamma_tables().texture[128] == 146);
    CHECK(gamma_tables().texture[255] == 255);
}

TEST_CASE("a texture is loaded with its palette through the texture gamma")
{
    IndexedImage image;
    image.width = image.height = 1;
    image.indices = {0};
    image.palette = {{0, 64, 128}, {200, 255, 10}};
    const IndexedImage game = with_texture_gamma(image);
    CHECK(game.indices == image.indices);
    CHECK(game.palette[0] == std::array<std::uint8_t, 3>{0, 84, 146});
    CHECK(game.palette[1] == std::array<std::uint8_t, 3>{gamma_tables().texture[200], 255, gamma_tables().texture[10]});
}

TEST_CASE("an Additive entity is faded by renderamt, twice: in the colour and in the alpha")
{
    const Vec3 any{0, 0, 1};
    CHECK(face_color({EntityRenderMode::Additive, 255}, any) == FaceColor{255, 255, 255, 255});
    CHECK(face_color({EntityRenderMode::Additive, 128}, any) == FaceColor{128, 128, 128, 128});
    CHECK(face_color({EntityRenderMode::Additive, 0}, any) == FaceColor{0, 0, 0, 0});
    // The normal does not matter: the map's light is not used.
    CHECK(face_color({EntityRenderMode::Additive, 200}, {1, 0, 0}) == face_color({EntityRenderMode::Additive, 200}, {0, 0, -1}));
}

TEST_CASE("a Normal entity is lit from above, by the map's light through the light gamma")
{
    EntityLook look;
    look.mode = EntityRenderMode::Normal;
    look.light = {128, 128, 128};
    // Light 128: shade 115.2, ambient 12.8. A face turned up gets both, a face turned down the ambient only.
    const auto lv = [](int index) { return static_cast<std::uint8_t>(gamma_tables().light[static_cast<std::size_t>(index)] / 1023.0f * 255.0f); };
    const FaceColor up = face_color(look, {0, 0, 1});
    CHECK(up == FaceColor{lv(512), lv(512), lv(512), 255});
    const FaceColor down = face_color(look, {0, 0, -1});
    CHECK(down == FaceColor{lv(51), lv(51), lv(51), 255});
    // Sideways is in between, and an unnormalized normal gives the same as a normalized one.
    const FaceColor side = face_color(look, {1, 0, 0});
    CHECK(side.r < up.r);
    CHECK(side.r > down.r);
    CHECK(face_color(look, {5, 0, 0}) == side);

    // A coloured light keeps its strength in its brightest channel and colours the rest.
    look.light = {255, 128, 0};
    const FaceColor warm = face_color(look, {0, 0, 1});
    CHECK(warm.r > warm.g);
    CHECK(warm.b == 0);
    CHECK(warm.g == static_cast<std::uint8_t>(128.0f / 255.0f * (gamma_tables().light[1020] / 1023.0f) * 255.0f));

    // No light at all still leaves a little ambient.
    look.light = {0, 0, 0};
    CHECK(face_color(look, {0, 0, 1}).r < 40);
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

TEST_CASE("the preview's faces carry the colour the engine gives them")
{
    const Settings s = one_still_quad();
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    OrbitCamera camera;
    SceneOptions options;
    options.grid = options.axes = options.bounds = false;
    Scene scene;

    build_scene(sys, baked, camera, 0.0, {400, 300}, options, scene);   // the default: Additive, renderamt 255
    REQUIRE_FALSE(scene.faces.empty());
    for (const SceneFace& f : scene.faces)
        CHECK(f.color == Rgba8{255, 255, 255, 255});

    options.look.renderamt = 100;
    build_scene(sys, baked, camera, 0.0, {400, 300}, options, scene);
    for (const SceneFace& f : scene.faces)
        CHECK(f.color == Rgba8{100, 100, 100, 100});

    options.look = {EntityRenderMode::Normal, 255, {200, 200, 200}};
    build_scene(sys, baked, camera, 0.0, {400, 300}, options, scene);
    const FaceColor lit = face_color(options.look, sys.quads()[0][0].normal);   // the quad stands, not turned
    for (const SceneFace& f : scene.faces)
        CHECK(f.color == Rgba8{lit.r, lit.g, lit.b, lit.a});
}
