#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "Exporter.hpp"

using namespace gs;

namespace {

Settings with(Geometry g, ScaleMode scale = ScaleMode::VertexBone)
{
    Settings s;
    s.geometry = g;
    s.scale_mode = scale;
    s.count = 3;
    s.quad_width = 10;
    s.quad_height = 14;
    return s;
}

} // namespace

TEST_CASE("the quads of a box share eight corners, the other geometries share none")
{
    CHECK(corners_for(Geometry::SingleQuad) == 4);
    CHECK(corners_for(Geometry::CrossedQuads) == 8);
    CHECK(corners_for(Geometry::TriPlanarQuads) == 12);
    CHECK(corners_for(Geometry::VolumetricBox) == 8);

    for (const Geometry g : {Geometry::SingleQuad, Geometry::CrossedQuads, Geometry::TriPlanarQuads, Geometry::VolumetricBox}) {
        CAPTURE(enum_id(g));
        const ParticleSystem sys(with(g));
        CHECK(static_cast<int>(sys.corners().size()) == corners_for(g));
        CHECK(sys.bones().corners_per_particle() == corners_for(g));
    }
}

TEST_CASE("bones per particle count the shared corners once")
{
    CHECK(bones_per_particle(Geometry::VolumetricBox, ScaleMode::VertexBone) == 9);
    CHECK(bones_per_particle(Geometry::VolumetricBox, ScaleMode::Static) == 1);
    CHECK(bones_per_particle(Geometry::TriPlanarQuads, ScaleMode::VertexBone) == 13);
    CHECK(max_particles(Geometry::VolumetricBox, ScaleMode::VertexBone) == 14);
}

TEST_CASE("every vertex of every quad maps to the corner at its position")
{
    for (const double rotation : {0.0, 33.0}) {
        CAPTURE(rotation);
        Settings s = with(Geometry::VolumetricBox);
        s.mesh_rotation = {rotation, rotation * 0.5, rotation * 2};
        const ParticleSystem sys(s);

        std::set<int> used;
        for (int qi = 0; qi < static_cast<int>(sys.quads().size()); ++qi)
            for (int k = 0; k < 4; ++k) {
                const int corner = sys.corner_of(qi, k);
                REQUIRE(corner >= 0);
                REQUIRE(corner < static_cast<int>(sys.corners().size()));
                const Vec3 expected = sys.quads()[static_cast<std::size_t>(qi)][static_cast<std::size_t>(k)].pos;
                const Vec3 got = sys.corners()[static_cast<std::size_t>(corner)];
                CHECK(got.x == doctest::Approx(expected.x));
                CHECK(got.y == doctest::Approx(expected.y));
                CHECK(got.z == doctest::Approx(expected.z));
                used.insert(corner);
            }
        CHECK(used.size() == 8);   // every corner is used
    }
}

TEST_CASE("in a box each corner is where three quads meet")
{
    const ParticleSystem sys(with(Geometry::VolumetricBox));
    std::vector<int> uses(sys.corners().size(), 0);
    for (int qi = 0; qi < static_cast<int>(sys.quads().size()); ++qi)
        for (int k = 0; k < 4; ++k)
            ++uses[static_cast<std::size_t>(sys.corner_of(qi, k))];
    CHECK(std::ranges::all_of(uses, [](int n) { return n == 3; }));
}

TEST_CASE("the exported animation moves a shared corner by the same offset for every quad that uses it")
{
    Settings s = with(Geometry::VolumetricBox);
    s.scale_start = 2.0;
    s.scale_end = 0.5;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    const AnimationTable table = animation_table(sys, baked);
    CHECK(table.bones == 1 + 3 * 9);

    for (int frame : {0, 10, 20}) {
        const double scale = baked.at(frame, 0).scale;
        for (int qi = 0; qi < static_cast<int>(sys.quads().size()); ++qi)
            for (int k = 0; k < 4; ++k) {
                const Vec3 v = sys.quads()[static_cast<std::size_t>(qi)][static_cast<std::size_t>(k)].pos;
                const auto& row = table.at(frame, sys.vertex_bone(0, qi, k));
                // the SMD axes of the exporter: (y, -x, z)
                CHECK(row[0] == doctest::Approx(v.y * (scale - 1.0)));
                CHECK(row[1] == doctest::Approx(-v.x * (scale - 1.0)));
                CHECK(row[2] == doctest::Approx(v.z * (scale - 1.0)));
            }
    }
}

TEST_CASE("the reference mesh of a box names the shared corner bones, and no bone is left unused")
{
    const ParticleSystem sys(with(Geometry::VolumetricBox));
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "particlemdl_corner_bones";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    export_model(sys, dir);

    std::ifstream in(dir / "custom_particles_ref.smd");
    std::string line;
    std::set<int> bones_in_triangles;
    bool in_triangles = false;
    while (std::getline(in, line)) {
        if (line == "triangles") {
            in_triangles = true;
            continue;
        }
        if (in_triangles && line != "end" && !line.empty() && std::isdigit(static_cast<unsigned char>(line[0]) )) {
            bones_in_triangles.insert(std::stoi(line));   // "  <bone> x y z nx ny nz u v"
        } else if (in_triangles && line.size() > 2 && line[0] == ' ') {
            bones_in_triangles.insert(std::stoi(line.substr(2)));
        }
    }
    in.close();   // Windows does not delete a file that is open
    std::filesystem::remove_all(dir);

    // 3 particles x 8 corners; the particle bones themselves hold no vertex with vertex bones.
    CHECK(bones_in_triangles.size() == 3 * 8);
    CHECK(*bones_in_triangles.begin() == 2);
    CHECK(*bones_in_triangles.rbegin() == 1 + 3 * 9 - 1);
}
