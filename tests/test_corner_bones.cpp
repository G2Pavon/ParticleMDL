#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <sstream>
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
    CHECK(bones_per_particle(Geometry::VolumetricBox, ScaleMode::VertexBone, 10) == 9);
    CHECK(bones_per_particle(Geometry::VolumetricBox, ScaleMode::Static, 10) == 1);
    CHECK(bones_per_particle(Geometry::TriPlanarQuads, ScaleMode::VertexBone, 9) == 13);
}

TEST_CASE("past the count the particle's bone fits, the corner bones carry the particle alone")
{
    // 127 bones: 14 boxes of 1 + 8 bones, or 15 of 8 corner bones alone.
    CHECK_FALSE(corner_bones_only(Geometry::VolumetricBox, ScaleMode::VertexBone, 14));
    CHECK(corner_bones_only(Geometry::VolumetricBox, ScaleMode::VertexBone, 15));
    CHECK(bones_per_particle(Geometry::VolumetricBox, ScaleMode::VertexBone, 15) == 8);
    CHECK_FALSE(corner_bones_only(Geometry::VolumetricBox, ScaleMode::Static, 127));
    CHECK(max_particles(Geometry::VolumetricBox, ScaleMode::VertexBone) == 15);
    CHECK(max_particles(Geometry::SingleQuad, ScaleMode::VertexBone) == 31);
    CHECK(max_particles(Geometry::CrossedQuads, ScaleMode::VertexBone) == 15);
    CHECK(max_particles(Geometry::TriPlanarQuads, ScaleMode::VertexBone) == 10);
    CHECK(max_particles(Geometry::SingleQuad, ScaleMode::Static) == 127);
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
    CHECK(table.bones == 3 * 9);

    for (int frame : {0, 10, 20}) {
        const double scale = baked.at(frame, 0).scale;
        for (int qi = 0; qi < static_cast<int>(sys.quads().size()); ++qi)
            for (int k = 0; k < 4; ++k) {
                const Vec3 v = sys.quads()[static_cast<std::size_t>(qi)][static_cast<std::size_t>(k)].pos;
                const auto& row = table.at(frame, sys.vertex_bone(0, qi, k));
                CHECK(row[0] == doctest::Approx(v.x * (scale - 1.0)));
                CHECK(row[1] == doctest::Approx(v.y * (scale - 1.0)));
                CHECK(row[2] == doctest::Approx(v.z * (scale - 1.0)));
            }
    }
}

TEST_CASE("the reference mesh of a box names the shared corner bones, and no bone is left unused")
{
    const ParticleSystem sys(with(Geometry::VolumetricBox));
    std::istringstream in(export_files(sys).bytes_of("_ref.smd"));
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

    // 3 particles x 8 corners; the particle bones themselves hold no vertex with vertex bones.
    CHECK(bones_in_triangles.size() == 3 * 8);
    CHECK(*bones_in_triangles.begin() == 1);
    CHECK(*bones_in_triangles.rbegin() == 3 * 9 - 1);
}

TEST_CASE("folding quads take two bones each and only single or crossed quads fold")
{
    CHECK(folds(Geometry::SingleQuad));
    CHECK(folds(Geometry::CrossedQuads));
    CHECK_FALSE(folds(Geometry::TriPlanarQuads));
    CHECK_FALSE(folds(Geometry::VolumetricBox));
    CHECK(bones_per_particle(Geometry::SingleQuad, ScaleMode::Folding, 10) == 2);
    CHECK(max_particles(Geometry::CrossedQuads, ScaleMode::Folding) == 63);

    Settings box;
    box.geometry = Geometry::VolumetricBox;
    box.scale_mode = ScaleMode::Folding;
    CHECK(validate(box));
    CHECK(box.scale_mode == ScaleMode::Static);
    Settings cluster;
    cluster.scale_mode = ScaleMode::Folding;
    cluster.cluster_size = 5;
    CHECK(validate(cluster));
    CHECK(cluster.cluster_size == 1);
}

TEST_CASE("a folding quad has no area on both sides of every jump, so none is drawn")
{
    // Every end behavior; the frames around each rebirth (and, hiding, each death) are folded flat.
    for (int e = 0; e < static_cast<int>(enum_names<EndBehavior>().size()); ++e)
        for (Geometry g : {Geometry::SingleQuad, Geometry::CrossedQuads}) {
            CAPTURE(e);
            Settings s;
            s.geometry = g;
            s.scale_mode = ScaleMode::Folding;
            s.end_behavior = static_cast<EndBehavior>(e);
            s.count = 30;
            s.duration = 2.0;
            s.fps = 15;
            const ParticleSystem sys(s);
            const BakedPoses b = bake(sys);
            const int steps = sys.loop_steps();
            int drawn_jumps = 0, folds_seen = 0;
            for (int i = 0; i < sys.num_particles(); ++i) {
                auto drawn = [](const ParticlePose& p) { return p.visible && ParticleSystem::has_area(p.scale, p.fold); };
                auto step = [&](int f) {
                    const Vec3 d = b.at((f + 1) % steps, i).pos - b.at(f, i).pos;
                    return std::hypot(d.x, d.y, d.z);
                };
                // A jump is a step far longer than the particle's own usual one (a fall is fast but usual).
                std::vector<double> usual;
                for (int f = 0; f < steps; ++f)
                    if (drawn(b.at(f, i)) && drawn(b.at((f + 1) % steps, i)))
                        usual.push_back(step(f));
                if (usual.empty())
                    continue;
                std::ranges::nth_element(usual, usual.begin() + static_cast<std::ptrdiff_t>(usual.size() / 2));
                const double limit = 3 * usual[usual.size() / 2] + s.quad_height;
                for (int f = 0; f < steps; ++f) {
                    folds_seen += b.at(f, i).fold == 1.0 ? 1 : 0;
                    // Drawn at both ends of a jump: what the engine would show as a streak.
                    if (step(f) > limit && drawn(b.at(f, i)) && drawn(b.at((f + 1) % steps, i)))
                        ++drawn_jumps;
                }
            }
            CHECK(drawn_jumps == 0);
            CHECK(folds_seen > 0);
        }
}

TEST_CASE("a fold moves the top corners down and the bottom corners up to the quad's middle line")
{
    Settings s;
    s.scale_mode = ScaleMode::Folding;
    s.quad_height = 10;
    const ParticleSystem sys(s);
    const Quad& q = sys.quads().front();
    for (int k = 0; k < 4; ++k)
        CHECK(sys.fold_offset(k, 0.0) == Vec3{});
    // Folded flat, corners 3 and 0 (and 2 and 1) meet half way.
    const Vec3 top = q[3].pos + sys.fold_offset(3, 1.0), bottom = q[0].pos + sys.fold_offset(0, 1.0);
    CHECK(top.z == doctest::Approx(bottom.z));
    CHECK(top.z == doctest::Approx((q[3].pos.z + q[0].pos.z) / 2));
    Settings plain;
    CHECK(ParticleSystem(plain).fold_offset(2, 1.0) == Vec3{});
}
