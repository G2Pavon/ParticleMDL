#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "PreviewScene.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

Settings clustered(int count, int size, double radius, Geometry geometry = Geometry::SingleQuad)
{
    Settings s;
    s.count = count;
    s.geometry = geometry;
    s.cluster_size = size;
    s.cluster_radius = radius;
    s.quad_width = 6;
    s.quad_height = 6;
    return s;
}

Vec3 centroid(const Quad& q)
{
    Vec3 c;
    for (const QuadVertex& v : q)
        c = c + v.pos;
    return c * 0.25;
}

double length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

TEST_CASE("without a cluster every particle shares the geometry")
{
    const ParticleSystem sys(clustered(10, 1, 20));
    CHECK(&sys.quads_of(0) == &sys.quads());
    CHECK(&sys.quads_of(7) == &sys.quads());
    CHECK(sys.vertex_count() == 10 * 4);
}

TEST_CASE("a cluster gives each particle that many copies of the geometry")
{
    for (const Geometry g : {Geometry::SingleQuad, Geometry::CrossedQuads, Geometry::VolumetricBox}) {
        CAPTURE(enum_id(g));
        const ParticleSystem sys(clustered(6, 5, 20, g));
        const std::size_t base = sys.quads().size();
        for (int i = 0; i < sys.num_particles(); ++i)
            CHECK(sys.quads_of(i).size() == base * 5);
        CHECK(sys.vertex_count() == 6 * 5 * corners_for(g));
    }
}

TEST_CASE("the copies lie inside the radius, apart from each other, and differ from particle to particle")
{
    const ParticleSystem sys(clustered(8, 6, 40));
    std::set<std::pair<long, long>> centers;
    for (int i = 0; i < sys.num_particles(); ++i)
        for (const Quad& q : sys.quads_of(i)) {
            CHECK(length(centroid(q)) <= 40.0 + 1e-9);   // a single quad is centered on the copy's own center
            centers.insert({std::lround(centroid(q).x * 1000), std::lround(centroid(q).y * 1000)});
        }
    CHECK(centers.size() == 8 * 6);   // no two copies on the same spot

    // The first particles are not clones of each other.
    CHECK(centroid(sys.quads_of(0)[0]).x != centroid(sys.quads_of(1)[0]).x);
}

TEST_CASE("copies fill the ball and are turned in every direction")
{
    const ParticleSystem sys(clustered(20, 12, 50));
    double widest = 0;
    double normal_z_min = 1, normal_z_max = -1;
    for (int i = 0; i < sys.num_particles(); ++i)
        for (const Quad& q : sys.quads_of(i)) {
            widest = std::max(widest, length(centroid(q)));
            normal_z_min = std::min(normal_z_min, q[0].normal.z);
            normal_z_max = std::max(normal_z_max, q[0].normal.z);
        }
    CHECK(widest > 40.0);            // near the edge of the ball
    CHECK(normal_z_min < -0.5);      // some face down
    CHECK(normal_z_max > 0.5);       // some face up
}

TEST_CASE("a cluster of radius 0 puts every copy at the particle, still turned")
{
    const ParticleSystem sys(clustered(3, 4, 0));
    std::set<long> normals;
    for (int i = 0; i < sys.num_particles(); ++i)
        for (const Quad& q : sys.quads_of(i)) {
            CHECK(length(centroid(q)) < 1e-9);
            normals.insert(std::lround(q[0].normal.x * 1000));
        }
    CHECK(normals.size() > 6);
}

TEST_CASE("a cluster is the same every time and another with another particle seed")
{
    Settings s = clustered(5, 4, 30);
    const ParticleSystem a(s), b(s);
    for (int i = 0; i < a.num_particles(); ++i)
        for (std::size_t q = 0; q < a.quads_of(i).size(); ++q)
            CHECK(centroid(a.quads_of(i)[q]) == centroid(b.quads_of(i)[q]));

    s.particle_seed = 5;
    const ParticleSystem other(s);
    CHECK_FALSE(centroid(a.quads_of(0)[0]) == centroid(other.quads_of(0)[0]));

    // The system seed moves the particles, not the copies on them.
    s.particle_seed = 0;
    s.system_seed = 5;
    const ParticleSystem moved(s);
    CHECK(centroid(a.quads_of(0)[0]) == centroid(moved.quads_of(0)[0]));
}

TEST_CASE("a cluster does not change how the particles move")
{
    const ParticleSystem plain(clustered(10, 1, 20)), cluster(clustered(10, 6, 20));
    for (std::size_t i = 0; i < plain.particles().size(); ++i) {
        CHECK(plain.particles()[i].spawn == cluster.particles()[i].spawn);
        CHECK(plain.particles()[i].velocity == cluster.particles()[i].velocity);
    }
}

TEST_CASE("vertex bones allow no cluster")
{
    Settings s = clustered(5, 8, 20);
    s.scale_mode = ScaleMode::VertexBone;
    CHECK(validate(s));
    CHECK(s.cluster_size == 1);
    CHECK(ParticleSystem(clustered(5, 8, 20)).vertex_count() == 5 * 8 * 4);

    Settings wild = clustered(5, 500, -3);
    CHECK(validate(wild));
    CHECK(wild.cluster_size == kMaxClusterSize);
    CHECK(wild.cluster_radius == 0.0);
}

TEST_CASE("the reference mesh holds every copy on its particle's bone")
{
    const Settings s = clustered(4, 3, 25);
    const ParticleSystem sys(s);
    std::istringstream in(export_files(sys).bytes_of("_ref.smd"));
    std::string line;
    bool triangles = false;
    std::vector<int> per_bone(5, 0);
    int vertex_lines = 0;
    while (std::getline(in, line)) {
        if (line == "triangles") {
            triangles = true;
            continue;
        }
        int bone;
        if (triangles && std::sscanf(line.c_str(), " %d %*f %*f %*f %*f %*f %*f %*d %*d", &bone) == 1) {
            ++per_bone[static_cast<std::size_t>(bone)];
            ++vertex_lines;
        }
    }

    // 4 particles x 3 copies x 1 quad x 4 triangles (double sided) x 3 vertex lines
    CHECK(vertex_lines == 4 * 3 * 4 * 3);
    for (int bone = 0; bone < 4; ++bone)
        CHECK(per_bone[static_cast<std::size_t>(bone)] == 3 * 4 * 3);
}

TEST_CASE("the bounding box and the hiding height make room for the cluster")
{
    Settings plain = clustered(6, 1, 0);
    Settings big = clustered(6, 8, 80);
    plain.speed = big.speed = 0;
    plain.burst = big.burst = 0;
    plain.spread = big.spread = 0;
    plain.gravity = big.gravity = 0;
    big.end_behavior = EndBehavior::HideUnderground;
    const ParticleSystem a(plain), b(big);
    const ParticleSystem::Bounds box_a = animation_bounds(a, bake(a)), box_b = animation_bounds(b, bake(b));
    CHECK(box_b.max.x - box_b.min.x > box_a.max.x - box_a.min.x + 40.0);
    CHECK(b.hide_z() <= b.min_z_raw() * b.volume_scale().z - 80.0);
}

TEST_CASE("the preview draws every copy")
{
    Settings s = clustered(6, 4, 30);
    s.end_behavior = EndBehavior::EndlessLoop;
    s.lifespan = 1.0;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    OrbitCamera camera;
    camera.set_focal_length(500);
    Scene scene;
    SceneOptions options;
    options.grid = options.axes = options.bounds = false;
    build_scene(sys, baked, camera, 0.5, {0, 0}, options, scene);
    // double sided: no face is dropped for facing away
    CHECK(scene.faces.size() == 6 * 4);
}

TEST_CASE("the mesh is dealt over body parts, and too many vertices for 32 is a blocker")
{
    // 2048 vertices fit one body part; 2080 take two.
    const ExportReport one = analyze_export(ParticleSystem(clustered(64, 8, 30)));
    CHECK(one.vertices == 2048);
    CHECK(one.body_parts == 1);
    CHECK(one.blockers.empty());
    const ExportReport two = analyze_export(ParticleSystem(clustered(65, 8, 30)));
    CHECK(two.vertices == 2080);
    CHECK(two.body_parts == 2);
    CHECK(two.blockers.empty());

    // Tri-planar clusters of 64 are 768 vertices a particle, two to a body part: 64 particles fill
    // the 32 body parts, and the 65th is over.
    const ExportReport full = analyze_export(ParticleSystem(clustered(64, 64, 60, Geometry::TriPlanarQuads)));
    CHECK(full.vertices == 64 * 768);
    CHECK(full.body_parts == kMaxBodyParts);
    CHECK(full.blockers.empty());
    const ExportReport over = analyze_export(ParticleSystem(clustered(65, 64, 60, Geometry::TriPlanarQuads)));
    CHECK(over.body_parts == kMaxBodyParts + 1);
    REQUIRE(over.blockers.size() == 1);
    CHECK(over.blockers[0].kind == ExportBlocker::Kind::TooManyVertices);
    CHECK(over.blockers[0].message.find(std::to_string(65 * 768) + " vertices") != std::string::npos);
    CHECK(over.blockers[0].message.find("32 body parts") != std::string::npos);
    CHECK(over.blockers[0].message.find("Cluster Size") != std::string::npos);
    CHECK_THROWS_AS(export_files(ParticleSystem(clustered(65, 64, 60, Geometry::TriPlanarQuads))), ExportBlocked);
}

TEST_CASE("each body part has a reference mesh of its own particles, and every bone")
{
    const ParticleSystem sys(clustered(65, 8, 30));
    const ExportFiles files = export_files(sys);
    const std::string& qc = files.bytes_of(".qc");
    CHECK(qc.find("$body \"studio\" \"custom_particles_ref\"") != std::string::npos);
    CHECK(qc.find("$body \"studio2\" \"custom_particles_ref2\"") != std::string::npos);
    CHECK(qc.find("studio3") == std::string::npos);
    // The first part holds the first 64 particles (2048 vertices), the second the last.
    const std::string& second = files.bytes_of("_ref2.smd");
    CHECK(second.find("  64 \"p_64\" -1") != std::string::npos);   // every part names every bone
    CHECK(second.find("\n  0 ") != std::string::npos);            // in the skeleton, not in the triangles
    std::istringstream in(second.substr(second.find("triangles")));
    std::string line;
    std::set<int> bones;
    while (std::getline(in, line)) {
        int bone = 0;
        if (std::sscanf(line.c_str(), " %d %*f %*f %*f %*f %*f %*f %*d %*d", &bone) == 1)
            bones.insert(bone);
    }
    CHECK(bones == std::set<int>{64});
}

TEST_CASE("the cluster settings are saved and old files have none")
{
    Settings s = clustered(5, 7, 33);
    const SettingsLoad back = settings_from_json(nlohmann::json::parse(settings_file_text(s)), Settings{});
    CHECK(back.settings.cluster_size == 7);
    CHECK(back.settings.cluster_radius == 33.0);

    nlohmann::json old = to_json(Settings{});
    old.erase("cluster_size");
    old.erase("cluster_radius");
    CHECK(settings_from_json(old, Settings{}).settings.cluster_size == 1);
}
