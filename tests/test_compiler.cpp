// The built-in compiler (studiomdl++, under third_party/studiomdl) on exports of this program.
#include <doctest/doctest.h>

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
