#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Skybox.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

// The engine's inverse of its sky mapping (ref_skybox.c): the face a direction points at, and where on
// it. Written from that code, apart from sky_point(), so the two check each other.
int axis_from_dir(const double d[3])
{
    const double a0 = std::abs(d[0]), a1 = std::abs(d[1]), a2 = std::abs(d[2]);
    if (a0 > a1 && a0 > a2)
        return d[0] < 0 ? 1 : 0;
    if (a1 > a2 && a1 > a0)
        return d[1] < 0 ? 3 : 2;
    return d[2] < 0 ? 5 : 4;
}

void project(const double d[3], int axis, double& s, double& t)
{
    static constexpr int vec_to_st[6][3] = {{-2, 3, 1}, {2, 3, -1}, {1, 3, 2}, {-1, 3, -2}, {-2, -1, 3}, {-2, 1, -3}};
    auto pick = [&](int j) { return j < 0 ? -d[-j - 1] : d[j - 1]; };
    const double dv = pick(vec_to_st[axis][2]);
    s = pick(vec_to_st[axis][0]) / dv;
    t = pick(vec_to_st[axis][1]) / dv;
}

// An uncompressed 24-bit TGA of one colour, top row first.
void write_tga(const fs::path& path, int size, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    std::vector<std::uint8_t> bytes(18, 0);
    bytes[2] = 2;   // true colour
    bytes[12] = static_cast<std::uint8_t>(size);
    bytes[14] = static_cast<std::uint8_t>(size);
    bytes[16] = 24;
    bytes[17] = 0x20;   // top-left origin
    for (int i = 0; i < size * size; ++i)
        bytes.insert(bytes.end(), {b, g, r});
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_sky(const fs::path& env, const std::string& name, const std::string& delim, const std::string& ext,
               std::uint8_t shade = 10)
{
    for (std::size_t i = 0; i < kSkySides.size(); ++i)
        write_tga(env / (name + delim + std::string(kSkySides[i]) + ext), 4, static_cast<std::uint8_t>(shade + i), 0, 0);
}

struct Games {
    fs::path root = fs::temp_directory_path() / "particlemdl_skies";
    Games() { fs::remove_all(root); }
    ~Games() { fs::remove_all(root); }
};

} // namespace

TEST_CASE("the sky faces are where the engine puts them, every point of every face")
{
    for (int face = 0; face < 6; ++face) {
        CAPTURE(face);
        for (double s = -0.95; s < 1.0; s += 0.1)
            for (double t = -0.95; t < 1.0; t += 0.1) {
                const SkyPoint p = sky_point(face, s, t);
                const double d[3] = {p.dir.x, p.dir.y, p.dir.z};
                CHECK(axis_from_dir(d) == face);
                double s2 = 0, t2 = 0;
                project(d, face, s2, t2);
                CHECK(s2 == doctest::Approx(s));
                CHECK(t2 == doctest::Approx(t));
                CHECK(p.uv.x == doctest::Approx((s + 1) / 2));
                CHECK(p.uv.y == doctest::Approx(1 - (t + 1) / 2));
            }
    }
    // The middle of each face looks along +X, -X, +Y, -Y, +Z, -Z, and shows rt, lf, bk, ft, up, dn.
    const Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const char* shown[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
    for (int face = 0; face < 6; ++face) {
        CHECK(sky_point(face, 0, 0).dir == axes[face]);
        CHECK(kSkySides[static_cast<std::size_t>(sky_face_side(face))] == shown[face]);
    }
    // Up on a side face is up in the world, and the image's top row is there.
    CHECK(sky_point(0, 0, 1).dir.z == doctest::Approx(1));
    CHECK(sky_point(0, 0, 1).uv.y == doctest::Approx(0));
}

TEST_CASE("skies are found in the mod, its _hd, _addon and _downloads, and the game it falls back to")
{
    const Games g;
    const fs::path mod = g.root / "cstrike";
    fs::create_directories(mod / "models");
    write_sky(mod / "gfx" / "env", "Desert", "", ".TGA");             // mixed case, as the game ships them
    write_sky(g.root / "cstrike_hd" / "gfx" / "env", "hdsky", "", ".tga");
    write_sky(g.root / "cstrike_downloads" / "gfx" / "env", "space", "_", ".bmp");   // Quake's naming
    write_sky(g.root / "valve" / "gfx" / "env", "city", "", ".tga");
    // Five sides only: not a sky.
    write_sky(mod / "gfx" / "env", "broken", "", ".tga");
    fs::remove(mod / "gfx" / "env" / "brokendn.tga");
    // Another game nobody falls back to.
    write_sky(g.root / "gearbox" / "gfx" / "env", "other", "", ".tga");

    CHECK(list_skyboxes(mod) == std::vector<std::string>{"city", "Desert", "hdsky", "space"});

    std::ofstream(mod / "liblist.gam") << "game \"Counter-Strike\"\nfallback_dir \"gearbox\"\n";
    CHECK(list_skyboxes(mod) == std::vector<std::string>{"Desert", "hdsky", "other", "space"});

    const std::vector<fs::path> folders = sky_search_folders(mod);
    REQUIRE(folders.size() == 4);
    CHECK(folders[0].filename() == "cstrike_hd");
    CHECK(folders[1].filename() == "cstrike");
    CHECK(folders[2].filename() == "cstrike_downloads");
    CHECK(folders[3].filename() == "gearbox");
}

TEST_CASE("a sky loads its six sides in rt, bk, lf, ft, up, dn order, the mod's before the fallback's")
{
    const Games g;
    const fs::path mod = g.root / "valve";
    write_sky(mod / "gfx" / "env", "desert", "", ".tga", 100);
    write_sky(g.root / "valve_addon" / "gfx" / "env", "desert", "", ".tga", 200);   // the addon wins

    const auto sides = load_skybox(mod, "DESERT");
    for (std::size_t i = 0; i < sides.size(); ++i) {
        CAPTURE(i);
        REQUIRE(sides[i].width == 4);
        CHECK(sides[i].pixel(0)[0] == 200 + i);
    }
    CHECK_THROWS_AS(load_skybox(mod, "nothing"), std::runtime_error);
}

#include "PreviewScene.hpp"

namespace {

// Whether p is inside the convex quad q (either winding).
bool inside(const std::array<Point2, 4>& q, Point2 p)
{
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        const Point2 a = q[static_cast<std::size_t>(i)], b = q[static_cast<std::size_t>((i + 1) % 4)];
        const double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        const int s = cross > 0 ? 1 : cross < 0 ? -1 : 0;
        if (s != 0 && sign != 0 && s != sign)
            return false;
        if (s != 0)
            sign = s;
    }
    return true;
}

} // namespace

TEST_CASE("the sky piece in the middle of the view is from the face the camera looks at")
{
    // Turned away from the default view, which looks along a diagonal, where two faces meet.
    OrbitCamera camera;
    for (const double turn : {20.0, 120.0, 300.0}) {
        CAPTURE(turn);
        camera.orbit(turn, 0);
        // Where the camera looks: the direction straight ahead of it.
        Vec3 ahead{};
        double best = -1;
        for (int i = 0; i < 360; ++i)
            for (int j = -89; j <= 89; ++j) {
                const double a = i * kDegToRad, e = j * kDegToRad;
                const Vec3 d{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)};
                const CamPoint c = camera.direction_to_camera_space(d);
                if (c.depth > best) {
                    best = c.depth;
                    ahead = d;
                }
            }
        const double dir[3] = {ahead.x, ahead.y, ahead.z};
        const int face = axis_from_dir(dir);

        std::vector<SkyQuad> sky;
        add_sky(sky, camera, {0, 0});
        REQUIRE_FALSE(sky.empty());
        int found = -1;
        for (const SkyQuad& q : sky)
            if (inside(q.pts, {0, 0}))
                found = q.side;
        CHECK(found == sky_face_side(face));
    }
}

TEST_CASE("the sky does not move when the camera zooms, and is not drawn when it is off")
{
    OrbitCamera camera;
    std::vector<SkyQuad> near, far;
    add_sky(near, camera, {100, 100});
    camera.zoom(-3);
    add_sky(far, camera, {100, 100});
    REQUIRE(near.size() == far.size());
    for (std::size_t i = 0; i < near.size(); ++i)
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(near[i].pts[k].x == far[i].pts[k].x);
            CHECK(near[i].pts[k].y == far[i].pts[k].y);
        }

    const ParticleSystem sys{Settings{}};
    const BakedPoses baked = bake(sys);
    Scene scene;
    SceneOptions options;
    build_scene(sys, baked, camera, 0.0, {0, 0}, options, scene);
    CHECK(scene.sky.empty());
    options.sky = true;
    build_scene(sys, baked, camera, 0.0, {0, 0}, options, scene);
    CHECK_FALSE(scene.sky.empty());
}
