#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Compiler.hpp"
#include "Exporter.hpp"
#include "Presets.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

Settings straight_quads(int count, double duration, int fps)
{
    Settings s;
    s.count = count;
    s.duration = duration;
    s.fps = fps;
    s.geometry = Geometry::SingleQuad;
    return s;
}

bool has_kind(const ExportReport& r, ExportBlocker::Kind kind)
{
    return std::ranges::any_of(r.blockers, [kind](const ExportBlocker& b) { return b.kind == kind; });
}

bool mentions(const ExportReport& r, std::string_view text)
{
    return std::ranges::any_of(r.blockers, [&](const ExportBlocker& b) { return b.message.find(text) != std::string::npos; });
}

// A folder that is removed again when the test ends.
struct TempDir {
    fs::path path;

    explicit TempDir(const char* name) : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    std::size_t file_count() const { return static_cast<std::size_t>(std::ranges::distance(fs::directory_iterator(path))); }
};

} // namespace

TEST_CASE("default settings and every preset are inside the limits")
{
    const ParticleSystem sys{Settings{}};
    const ExportReport r = analyze_export(sys);
    CHECK(r.blockers.empty());
    CHECK(r.frames == 91);
    CHECK(r.sequence_bytes == sequence_bytes(sys));

    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s;
        apply_preset(s, preset);
        CHECK(analyze_export(ParticleSystem(s)).blockers.empty());
    }
}

TEST_CASE("more than 2048 frames is a blocker")
{
    const ExportReport r = analyze_export(ParticleSystem(straight_quads(1, 60.0, 60)));
    CHECK(r.frames == 3601);
    REQUIRE(has_kind(r, ExportBlocker::Kind::TooManyFrames));
    CHECK(r.blockers.front().kind == ExportBlocker::Kind::TooManyFrames);   // listed first
    CHECK(mentions(r, "3601 frames"));
    CHECK(mentions(r, "2048"));
    CHECK(mentions(r, "At 60 fps the longest Duration is 34.1 s"));
}

TEST_CASE("the frame limit is exact")
{
    // 40 fps: the frames are lround(duration * 40) + 1, the last one closing the loop.
    const ExportReport at_limit = analyze_export(ParticleSystem(straight_quads(1, 51.175, 40)));
    CHECK(at_limit.frames == kMaxStudioFrames);
    CHECK_FALSE(has_kind(at_limit, ExportBlocker::Kind::TooManyFrames));

    const ExportReport over = analyze_export(ParticleSystem(straight_quads(1, 51.2, 40)));
    CHECK(over.frames == kMaxStudioFrames + 1);
    CHECK(has_kind(over, ExportBlocker::Kind::TooManyFrames));
}

TEST_CASE("too much animation data is a blocker even with few frames")
{
    const ExportReport r = analyze_export(ParticleSystem(straight_quads(126, 10.0, 30)));
    CHECK(r.frames == 301);
    CHECK(r.sequence_bytes > kMaxSequenceBytes);
    CHECK_FALSE(has_kind(r, ExportBlocker::Kind::TooManyFrames));
    REQUIRE(has_kind(r, ExportBlocker::Kind::SequenceTooLarge));
    CHECK(mentions(r, "301 frames"));
    CHECK(mentions(r, "126 bones"));
    CHECK(mentions(r, "64.0 KB"));
    CHECK_FALSE(mentions(r, "Vertex-bone"));   // no vertex bones here
}

TEST_CASE("the data blocker follows sequence_bytes exactly")
{
    bool saw_ok = false, saw_blocked = false;
    for (int count = 1; count <= 126; ++count) {
        CAPTURE(count);
        const ParticleSystem sys(straight_quads(count, 10.0, 30));
        const ExportReport r = analyze_export(sys);
        const bool over = sequence_bytes(sys) > kMaxSequenceBytes;
        CHECK(has_kind(r, ExportBlocker::Kind::SequenceTooLarge) == over);
        (over ? saw_blocked : saw_ok) = true;
    }
    CHECK(saw_ok);
    CHECK(saw_blocked);   // the sweep crosses the limit
}

TEST_CASE("the data blocker explains vertex bones when they are on")
{
    Settings s = straight_quads(25, 30.0, 60);   // 25 particles x 5 bones = 126
    s.scale_mode = ScaleMode::VertexBone;
    const ExportReport r = analyze_export(ParticleSystem(s));
    REQUIRE(has_kind(r, ExportBlocker::Kind::SequenceTooLarge));
    CHECK(mentions(r, "Vertex-bone scaling uses 5 bones per particle"));
}

TEST_CASE("both blockers can apply together, frames first")
{
    const ExportReport r = analyze_export(ParticleSystem(straight_quads(126, 60.0, 60)));
    REQUIRE(r.blockers.size() == 2);
    CHECK(r.blockers[0].kind == ExportBlocker::Kind::TooManyFrames);
    CHECK(r.blockers[1].kind == ExportBlocker::Kind::SequenceTooLarge);
}

TEST_CASE("analyzing with a prebuilt table gives the same report")
{
    const ParticleSystem sys(straight_quads(60, 10.0, 30));
    const ExportReport a = analyze_export(sys);
    const BakedPoses baked = bake(sys);
    const ExportReport b = analyze_export(sys, animation_table(sys, baked), baked);
    CHECK(a.frames == b.frames);
    CHECK(a.sequence_bytes == b.sequence_bytes);
    REQUIRE(a.blockers.size() == b.blockers.size());
    for (std::size_t i = 0; i < a.blockers.size(); ++i)
        CHECK(a.blockers[i].message == b.blockers[i].message);
}

TEST_CASE("the Hide in Core warning remains advisory")
{
    Settings s;
    s.end_behavior = EndBehavior::HideInCore;
    s.scale_mode = ScaleMode::Static;
    const ExportReport r = analyze_export(ParticleSystem(s));
    CHECK(r.blockers.empty());
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings.front().find("Hide in Core") != std::string::npos);

    s.scale_mode = ScaleMode::VertexBone;
    CHECK(analyze_export(ParticleSystem(s)).warnings.empty());
}

TEST_CASE("export_mdl refuses a blocked export and writes nothing")
{
    const TempDir dir("particlemdl_blocked_test");

    for (const Settings& s : {straight_quads(1, 60.0, 60), straight_quads(126, 10.0, 30)}) {
        const ParticleSystem sys(s);
        CHECK_THROWS_AS(export_mdl(sys, dir.path), ExportBlocked);
        CHECK_THROWS_AS(export_mdl(sys, dir.path), std::runtime_error);   // callers that catch the base class
        CHECK(dir.file_count() == 0);
    }

    try {
        export_mdl(ParticleSystem(straight_quads(126, 60.0, 60)), dir.path);
        FAIL("export_mdl should have thrown");
    } catch (const ExportBlocked& e) {
        REQUIRE(e.blockers().size() == 2);
        CHECK(e.blockers()[0].kind == ExportBlocker::Kind::TooManyFrames);
        CHECK(std::string(e.what()) == e.blockers()[0].message);
    }
    CHECK(dir.file_count() == 0);
}

TEST_CASE("export_mdl replaces nothing when it refuses")
{
    const TempDir dir("particlemdl_blocked_keep_test");
    const fs::path existing = dir.path / "custom_particles.mdl";
    std::ofstream(existing) << "keep me";

    CHECK_THROWS_AS(export_mdl(ParticleSystem(straight_quads(1, 60.0, 60)), dir.path), ExportBlocked);

    std::ifstream in(existing);
    std::string text;
    std::getline(in, text);
    CHECK(text == "keep me");
    CHECK(dir.file_count() == 1);
}

TEST_CASE("export_mdl writes only the model of an export that is inside the limits")
{
    const TempDir dir("particlemdl_unblocked_test");
    export_mdl(ParticleSystem{Settings{}}, dir.path);
    CHECK(dir.file_count() == 1);
    CHECK(fs::exists(dir.path / "custom_particles.mdl"));
}

TEST_CASE("exporting with the baked frames and animation already at hand gives the same files")
{
    const ParticleSystem sys{Settings{}};
    const ExportFiles plain = export_files(sys);
    const BakedPoses baked = bake(sys);
    const ExportFiles reused = export_files(sys, baked, animation_table(sys, baked));

    REQUIRE(plain.files.size() == reused.files.size());
    for (std::size_t i = 0; i < plain.files.size(); ++i) {
        CHECK(plain.files[i].name == reused.files[i].name);
        CHECK(plain.files[i].bytes == reused.files[i].bytes);
    }

    // The limits are checked on this path too.
    Settings over;
    over.count = 126;
    over.duration = 60;
    over.fps = 60;
    over.geometry = Geometry::SingleQuad;
    const ParticleSystem big(over);
    const BakedPoses big_baked = bake(big);
    CHECK_THROWS_AS(export_files(big, big_baked, animation_table(big, big_baked)), ExportBlocked);
}

TEST_CASE("frames and a table built from another system are refused")
{
    const ParticleSystem sys{Settings{}};
    const BakedPoses baked = bake(sys);
    const AnimationTable table = animation_table(sys, baked);

    Settings more;
    more.count = sys.num_particles() + 1;
    const ParticleSystem other(more);
    Settings longer;
    longer.duration = Settings{}.duration * 2;
    const ParticleSystem slower(longer);

    CHECK_THROWS_AS(analyze_export(other, table, baked), std::invalid_argument);
    CHECK_THROWS_AS(analyze_export(slower, table, baked), std::invalid_argument);
    const BakedPoses other_baked = bake(other);
    CHECK_THROWS_AS(analyze_export(sys, table, other_baked), std::invalid_argument);
    CHECK_THROWS_AS(export_files(other, baked, table), std::invalid_argument);
    CHECK_THROWS_AS(export_files_over_limits(other, baked, table), std::invalid_argument);
    CHECK_NOTHROW(analyze_export(sys, table, baked));
}
