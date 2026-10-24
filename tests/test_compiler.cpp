// The built-in compiler (studiomdl++, under third_party/studiomdl) on exports of this program.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "Exporter.hpp"
#include "MdlReader.hpp"
#include "Presets.hpp"
#include "SeedRoll.hpp"
#include "SettingsJson.hpp"

using namespace gs;
namespace fs = std::filesystem;

#ifndef GS_ROLLED_SEEDS
#define GS_ROLLED_SEEDS 3
#endif

namespace {

const fs::path kFixtures = fs::path(GS_FIXTURE_DIR) / "real_mdl";

std::vector<std::uint8_t> file_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

ParticleSystem fixture_system(const char* name)
{
    return ParticleSystem(read_settings_file(kFixtures / (std::string(name) + ".json"), Settings{}).settings);
}

} // namespace

TEST_CASE("every preset compiles into the model that was exported")
{
    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s;
        apply_preset(s, preset);
        s.model_name = "preset";
        const ParticleSystem sys(s);
        const CompiledModel model = compile_model(export_files(sys));
        CHECK(model.warnings.empty());
        CHECK(compare_with_export(read_mdl(model.mdl), sys).empty());
    }
}

TEST_CASE("rolled settings compile into the model that was exported")
{
    // Seeds turn clusters, mesh rotation, spin and size variance on at random, which is where the
    // vertices and normals the compiler merges differ from one effect to the next.
    constexpr int kSeeds = GS_ROLLED_SEEDS;
    for (Geometry geometry : {Geometry::SingleQuad, Geometry::CrossedQuads, Geometry::TriPlanarQuads, Geometry::VolumetricBox})
        for (int seed = 1; seed <= kSeeds; ++seed) {
            CAPTURE(static_cast<int>(geometry));
            CAPTURE(seed);
            Settings s;
            s.model_name = "rolled";
            s.geometry = geometry;
            roll_settings(s, Settings(s), SeedSide::System, seed);
            roll_settings(s, Settings(s), SeedSide::Particle, seed * 7919);
            const ParticleSystem sys(s);
            CHECK(compare_with_export(read_mdl(compile_model(export_files(sys)).mdl), sys).empty());
        }
}

TEST_CASE("the built-in compiler makes the models Sven Co-op's studiomdl made")
{
    // The fixtures were compiled by "StudioMDL SC (Nov 30 2018)". The built-in compiler's models
    // have the same size and the same contents; a few animation values round one unit apart and
    // some padding is zero where Sven Co-op's leaves what was in memory.
    for (const char* name : {"box_vertex_bones", "crossed_static", "one_shot", "vertex_bones"}) {
        CAPTURE(name);
        const ParticleSystem sys = fixture_system(name);
        const CompiledModel model = compile_model(export_files(sys));
        const std::vector<std::uint8_t> sven = file_bytes(kFixtures / (std::string(name) + ".mdl"));
        CHECK(model.mdl.size() == sven.size());
        CHECK(describe_mdl(read_mdl(model.mdl)) == describe_mdl(read_mdl(sven)));
        CHECK(compare_with_export(read_mdl(model.mdl), sys).empty());
    }
}

TEST_CASE("a compile gives the same bytes every time")
{
    const ExportFiles files = export_files(fixture_system("one_shot"));
    CHECK(compile_model(files).mdl == compile_model(files).mdl);
}

TEST_CASE("looping sequences that hung studiomdl++ under Wine compile")
{
    // With 10 particles at 30 fps, the Windows build of studiomdl++ never finished 55 to 70 steps.
    for (int steps = 50; steps <= 80; steps += 3) {
        CAPTURE(steps);
        Settings s;
        s.model_name = "loop";
        s.count = 10;
        s.fps = 30;
        s.duration = steps / 30.0;
        s.loop = true;
        const ParticleSystem sys(s);
        REQUIRE(sys.looping());
        CHECK(compare_with_export(read_mdl(compile_model(export_files(sys)).mdl), sys).empty());
    }
}

TEST_CASE("a refused export throws CompileFailed with the compiler's message")
{
    ExportFiles files = export_files(fixture_system("crossed_static"));
    std::erase_if(files.files, [](const ExportFile& f) { return f.name.ends_with("_anim.smd"); });
    try {
        compile_model(files);
        FAIL("the compile did not fail");
    } catch (const CompileFailed& e) {
        const std::string message = e.what();
        CHECK(message.find("ERROR") != std::string::npos);
        CHECK(message.find("_anim") != std::string::npos);
        CHECK_FALSE(message.ends_with("\n"));
    }
}

// ---------------------------------------------------------------------------
// A still of one frame
// ---------------------------------------------------------------------------

namespace {

// A wide and shallow effect, so a still turned by 90 degrees would not fit the box of the preview.
Settings wide_effect()
{
    Settings s;
    s.model_name = "wide";
    s.volume_width = 240;
    s.volume_depth = 30;
    s.count = 30;
    s.duration = 2.0;
    s.fps = 10;
    return s;
}

// The box of every particle face shown on `frame`, as the preview places them.
std::array<Vec3, 2> preview_box(const ParticleSystem& sys, const BakedPoses& baked, int frame)
{
    Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
    for (int i = 0; i < sys.num_particles(); ++i) {
        const SampledPose pose = baked.sample(i, sys.frame_time(frame));
        if (!pose.visible || pose.scale <= 0.0)
            continue;
        for (const Quad& q : sys.quads_of(i))
            for (const QuadVertex& v : q) {
                const Vec3 p = pose.pos + rotate(pose.rot, v.pos * pose.scale);
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            }
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("a frame compiles into a still model of one bone and one frame")
{
    const ParticleSystem sys(wide_effect());
    const BakedPoses baked = bake(sys);
    const CompiledModel model = compile_model(export_frame_files(sys, baked, 12));
    CHECK(model.warnings.empty());
    const MdlInfo info = read_mdl(model.mdl);
    CHECK(info.name == "wide_f12.mdl");
    REQUIRE(info.bones.size() == 1);
    REQUIRE(info.sequences.size() == 1);
    CHECK(info.sequences[0].frames == 1);
    CHECK((info.sequences[0].flags & kMdlSequenceLooping) == 0);
    REQUIRE(info.textures.size() == 1);
    CHECK((info.textures[0].flags & kMdlTextureAdditive) != 0);
    // Static crossed quads: each particle shown keeps its own vertices.
    REQUIRE(info.models.size() == 1);
    CHECK(info.models[0].vertices == sys.vertex_count() / sys.num_particles() * particles_on_frame(sys, baked, 12));
}

TEST_CASE("the still stands where the preview shows that frame")
{
    // studiomdl measures the box of the sequence from the compiled vertices: it must be the box
    // of the faces in the preview, not turned and not moved.
    for (double scale : {1.0, 2.0}) {
        CAPTURE(scale);
        Settings s = wide_effect();
        s.model_scale = scale;
        const ParticleSystem sys(s);
        const BakedPoses baked = bake(sys);
        for (int frame : {0, 7, 15}) {
            CAPTURE(frame);
            const MdlInfo info = read_mdl(compile_model(export_frame_files(sys, baked, frame)).mdl);
            const auto [lo, hi] = preview_box(sys, baked, frame);
            const MdlSequence& seq = info.sequences.at(0);
            CHECK(seq.bbmin[0] == doctest::Approx(lo.x * scale).epsilon(0.002));
            CHECK(seq.bbmin[1] == doctest::Approx(lo.y * scale).epsilon(0.002));
            CHECK(seq.bbmin[2] == doctest::Approx(lo.z * scale).epsilon(0.002));
            CHECK(seq.bbmax[0] == doctest::Approx(hi.x * scale).epsilon(0.002));
            CHECK(seq.bbmax[1] == doctest::Approx(hi.y * scale).epsilon(0.002));
            CHECK(seq.bbmax[2] == doctest::Approx(hi.z * scale).epsilon(0.002));
        }
    }
}

TEST_CASE("a frame with no particle shown, or that the animation does not have, is refused")
{
    Settings s = wide_effect();
    s.loop = false;
    s.lifespan = 0.5;   // played once: every particle has died by the last frame
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    REQUIRE(particles_on_frame(sys, baked, 20) == 0);
    CHECK_THROWS_AS(export_frame_files(sys, baked, 20), std::invalid_argument);
    CHECK_THROWS_AS(export_frame_files(sys, baked, 21), std::invalid_argument);
    CHECK_THROWS_AS(export_frame_files(sys, baked, -1), std::invalid_argument);
    CHECK_NOTHROW(export_frame_files(sys, baked, 0));
}

TEST_CASE("the still's name gives up the end of a long model name to its frame number")
{
    CHECK(frame_model_name("Fire Ball", 40) == safe_model_name("Fire Ball") + "_f40");
    const std::string longest(kMaxModelNameLength, 'a');
    const std::string name = frame_model_name(longest, 1234);
    CHECK(name.size() == static_cast<std::size_t>(kMaxModelNameLength));
    CHECK(name.ends_with("_f1234"));
}

TEST_CASE("the animated model moves where the preview shows it")
{
    // studiomdl measures the box of the sequence from the vertices in every frame: it must be the
    // box of the preview's animation, for each geometry and scaling, spinning and not.
    for (Geometry g : {Geometry::SingleQuad, Geometry::CrossedQuads, Geometry::TriPlanarQuads, Geometry::VolumetricBox})
        for (ScaleMode mode : {ScaleMode::Static, ScaleMode::VertexBone, ScaleMode::Folding})
            for (bool spin : {false, true})
                for (bool most : {false, true}) {   // the most: corner bones alone with vertex bones
                CAPTURE(static_cast<int>(g));
                CAPTURE(static_cast<int>(mode));
                CAPTURE(spin);
                CAPTURE(most);
                Settings s = wide_effect();
                s.geometry = g;
                s.scale_mode = mode;
                s.count = most ? max_particles(g, mode) : 8;
                s.scale_start = 2.0;
                s.scale_end = 0.5;
                if (spin)
                    s.particle_spin = {40, 70, 90};
                const ParticleSystem sys(s);
                REQUIRE(sys.bones().particle_bones() == !(most && mode == ScaleMode::VertexBone));
                const BakedPoses baked = bake(sys);
                const MdlInfo info = read_mdl(compile_model(export_files(sys)).mdl);
                CHECK(compare_with_export(info, sys).empty());
                const ParticleSystem::Bounds b = animation_bounds(sys, baked);
                const MdlSequence& seq = info.sequences.at(0);
                CHECK(seq.bbmin[0] == doctest::Approx(b.min.x).epsilon(0.002));
                CHECK(seq.bbmin[1] == doctest::Approx(b.min.y).epsilon(0.002));
                CHECK(seq.bbmin[2] == doctest::Approx(b.min.z).epsilon(0.002));
                CHECK(seq.bbmax[0] == doctest::Approx(b.max.x).epsilon(0.002));
                CHECK(seq.bbmax[1] == doctest::Approx(b.max.y).epsilon(0.002));
                CHECK(seq.bbmax[2] == doctest::Approx(b.max.z).epsilon(0.002));
            }
}

TEST_CASE("a still of more faces than a body part holds is dealt over several")
{
    Settings s = wide_effect();
    s.geometry = Geometry::TriPlanarQuads;
    s.cluster_size = 64;
    s.cluster_radius = 40;
    const ParticleSystem sys(s);
    const BakedPoses baked = bake(sys);
    const int frame = 7;
    const MdlInfo info = read_mdl(compile_model(export_frame_files(sys, baked, frame)).mdl);
    const int faces = particles_on_frame(sys, baked, frame) * 64 * 3;
    CHECK(info.body_parts == (faces + 511) / 512);   // 512 faces of 4 vertices fill one
    REQUIRE(info.body_parts > 1);
    int vertices = 0;
    for (const MdlModel& part : info.models) {
        CHECK(part.vertices <= kMaxStudioVertices);
        vertices += part.vertices;
    }
    CHECK(vertices == faces * 4);
    const auto [lo, hi] = preview_box(sys, baked, frame);
    CHECK(info.sequences.at(0).bbmin[0] == doctest::Approx(lo.x).epsilon(0.002));
    CHECK(info.sequences.at(0).bbmax[2] == doctest::Approx(hi.z).epsilon(0.002));
}
