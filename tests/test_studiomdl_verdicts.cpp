// What the real studiomdl said about models this program exported.
//
// The models below were exported and compiled with "StudioMDL SC (Nov 30 2018 16:56:14)"
// (Sven Co-op's studiomdl.exe, run under Wine). It prints "sequences N bytes" for a model it
// accepts and `sequence "idle" is greate than 64K` for one it refuses. These cases keep the
// prediction of sequence_bytes() and the 64 KB blocker honest: they cannot be checked against
// the compiler in CI, so the compiler's answers are recorded here.
#include <doctest/doctest.h>

#include <algorithm>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "SettingsJson.hpp"

using namespace gs;

namespace {

struct Verdict {
    const char* name;
    const char* settings;        // as a settings file
    std::size_t predicted;       // sequence_bytes() of these settings
    int bones;
    bool accepted;               // studiomdl compiled it
    std::size_t compiler_total;  // "sequences N bytes" it printed; 0 if it refused the model
};

constexpr Verdict kVerdicts[] = {
    {"small", R"({"model_name":"a1","count":20,"duration":2.0,"fps":15})", 3654, 21, true, 4176},
    {"spin and turbulence",
     R"({"model_name":"a2","count":64,"duration":3.0,"fps":30,"turbulence":40,"particle_spin":[30,40,90]})", 62310, 65, true,
     63360},
    {"vertex bones",
     R"({"model_name":"a3","count":25,"duration":2.0,"fps":15,"scale_mode":"vertex_bone","scale_start":2,"scale_end":0.2,"geometry":"single_quad"})",
     15456, 126, true, 17236},
    {"most bones",
     R"({"model_name":"a4","count":126,"duration":2.0,"fps":15,"geometry":"single_quad","particle_spin":[60,30,10]})", 45486,
     127, true, 47280},
    {"66 particles", R"({"model_name":"b66","count":66,"duration":3.0,"fps":30,"turbulence":40,"particle_spin":[30,40,90]})",
     64218, 67, true, 65292},
    // The compiler's own total is already above 65535 here: the limit is on the measure sequence_bytes() takes.
    {"67 particles", R"({"model_name":"b67","count":67,"duration":3.0,"fps":30,"turbulence":40,"particle_spin":[30,40,90]})",
     65226, 68, true, 66312},
    {"68 particles", R"({"model_name":"b68","count":68,"duration":3.0,"fps":30,"turbulence":40,"particle_spin":[30,40,90]})",
     66200, 69, false, 0},
    {"70 particles", R"({"model_name":"b70","count":70,"duration":3.0,"fps":30,"turbulence":40,"particle_spin":[30,40,90]})",
     68076, 71, false, 0},
    {"one under the limit",
     R"({"model_name":"t65534","geometry":"single_quad","count":115,"duration":2.17,"fps":36,"turbulence":16,"particle_spin":[0,0,53]})",
     65534, 116, true, 67196},
    {"one over the limit",
     R"({"model_name":"t65536","geometry":"single_quad","count":96,"duration":4.58,"fps":28,"turbulence":1,"particle_spin":[0,0,0]})",
     65536, 97, false, 0},
};

} // namespace

TEST_CASE("sequence_bytes predicts what the real studiomdl measures")
{
    for (const Verdict& v : kVerdicts) {
        CAPTURE(v.name);
        const SettingsLoad loaded = settings_from_json(nlohmann::json::parse(v.settings), Settings{});
        REQUIRE(loaded.warnings.empty());
        const ParticleSystem sys(loaded.settings);
        const ExportReport report = analyze_export(sys);

        CHECK(report.sequence_bytes == v.predicted);
        CHECK(animation_table(sys).bones == v.bones);
    }
}

TEST_CASE("the 64 KB blocker agrees with the models studiomdl accepted and refused")
{
    for (const Verdict& v : kVerdicts) {
        CAPTURE(v.name);
        const SettingsLoad loaded = settings_from_json(nlohmann::json::parse(v.settings), Settings{});
        const ExportReport report = analyze_export(ParticleSystem(loaded.settings));

        const bool blocked = !report.blockers.empty();
        CHECK(blocked == !v.accepted);
        if (blocked)
            CHECK(report.blockers.front().kind == ExportBlocker::Kind::SequenceTooLarge);
    }
}

TEST_CASE("the limit is on the predicted measure, not on the total studiomdl writes")
{
    // 67 particles are accepted although studiomdl writes 66312 bytes of sequence data.
    bool above_limit_but_accepted = false;
    for (const Verdict& v : kVerdicts)
        if (v.accepted && v.compiler_total > kMaxSequenceBytes)
            above_limit_but_accepted = true;
    CHECK(above_limit_but_accepted);
}

TEST_CASE("the compiler's total is the prediction plus the offset tables and the sequence header")
{
    // Each bone but the first adds its 12-byte table of channel offsets; the rest is a constant
    // of about 282 bytes (the sequence description and padding).
    for (const Verdict& v : kVerdicts) {
        if (!v.accepted)
            continue;
        CAPTURE(v.name);
        const long extra = static_cast<long>(v.compiler_total) - static_cast<long>(v.predicted) - 12L * (v.bones - 1);
        CHECK(extra >= 280);
        CHECK(extra <= 284);
    }
}

// ---------------------------------------------------------------------------
// Frames. A sequence of more than 2048 frames is not refused: studiomdl keeps the first 2048
// and says nothing. These are one particle, 40 frames per second, single quads.
// ---------------------------------------------------------------------------

namespace {

struct FrameVerdict {
    const char* settings;
    int frames;            // what this program exports
    int compiler_frames;   // "(N frames)" in studiomdl's report
};

constexpr FrameVerdict kFrameVerdicts[] = {
    {R"({"model_name":"fr2047","count":1,"duration":50.0,"fps":40,"geometry":"single_quad"})", 2001, 2001},
    {R"({"model_name":"fr2048","count":1,"duration":51.175,"fps":40,"geometry":"single_quad"})", 2048, 2048},
    {R"({"model_name":"fr2049","count":1,"duration":51.2,"fps":40,"geometry":"single_quad"})", 2049, 2048},
    {R"({"model_name":"fr2100","count":1,"duration":52.5,"fps":40,"geometry":"single_quad"})", 2101, 2048},
};

} // namespace

TEST_CASE("the frame blocker is exactly where studiomdl starts to cut the sequence")
{
    for (const FrameVerdict& v : kFrameVerdicts) {
        CAPTURE(v.frames);
        const SettingsLoad loaded = settings_from_json(nlohmann::json::parse(v.settings), Settings{});
        const ExportReport report = analyze_export(ParticleSystem(loaded.settings));
        CHECK(report.frames == v.frames);
        const bool cut = v.compiler_frames != v.frames;
        const bool blocked = std::ranges::any_of(
            report.blockers, [](const ExportBlocker& b) { return b.kind == ExportBlocker::Kind::TooManyFrames; });
        CHECK(blocked == cut);   // blocked exactly when the compiler would have cut frames off
        CHECK(v.compiler_frames == std::min(v.frames, kMaxStudioFrames));
    }
}

// ---------------------------------------------------------------------------
// Vertices. studiomdl prints "(N vertices, ...)" for a model it accepts and
// `too many vertices in model` above 2048.
// ---------------------------------------------------------------------------

namespace {

struct VertexVerdict {
    const char* name;
    const char* settings;
    int vertices;      // what studiomdl counted, or what vertex_count() says for a model it refused
    bool accepted;
};

constexpr VertexVerdict kVertexVerdicts[] = {
    {"crossed quads", R"({"model_name":"crossed","count":20,"duration":1.0,"fps":10})", 160, true},
    {"static boxes (eight corners each)",
     R"({"model_name":"boxstatic","count":8,"duration":1.0,"fps":10,"geometry":"volumetric_box"})", 64, true},
    {"tri-planar quads",
     R"({"model_name":"tri","count":10,"duration":1.0,"fps":10,"geometry":"tri_planar_quads"})", 120, true},
    {"boxes with shared corner bones",
     R"({"model_name":"boxvb","count":14,"duration":1.0,"fps":10,"geometry":"volumetric_box","scale_mode":"vertex_bone"})", 112,
     true},
    {"clusters of crossed quads",
     R"({"model_name":"clcross","count":30,"duration":1.0,"fps":10,"geometry":"crossed_quads","cluster_size":4,"cluster_radius":30})",
     960, true},
    {"clusters of boxes",
     R"({"model_name":"clbox","count":20,"duration":1.0,"fps":10,"geometry":"volumetric_box","cluster_size":5,"cluster_radius":30})",
     800, true},
    {"exactly 2048 vertices",
     R"({"model_name":"cl8","count":64,"duration":1.0,"fps":10,"geometry":"single_quad","cluster_size":8,"cluster_radius":30})",
     2048, true},
    {"2080 vertices",
     R"({"model_name":"cl8x","count":65,"duration":1.0,"fps":10,"geometry":"single_quad","cluster_size":8,"cluster_radius":30})",
     2080, false},
    {"2400 vertices",
     R"({"model_name":"clbigbox","count":50,"duration":1.0,"fps":10,"geometry":"volumetric_box","cluster_size":6,"cluster_radius":30})",
     2400, false},
};

} // namespace

TEST_CASE("vertex_count is the number of vertices the real studiomdl counts")
{
    for (const VertexVerdict& v : kVertexVerdicts) {
        CAPTURE(v.name);
        const SettingsLoad loaded = settings_from_json(nlohmann::json::parse(v.settings), Settings{});
        REQUIRE(loaded.warnings.empty());
        CHECK(ParticleSystem(loaded.settings).vertex_count() == v.vertices);
    }
}

TEST_CASE("the vertex blocker agrees with the models studiomdl accepted and refused")
{
    for (const VertexVerdict& v : kVertexVerdicts) {
        CAPTURE(v.name);
        const SettingsLoad loaded = settings_from_json(nlohmann::json::parse(v.settings), Settings{});
        const ExportReport report = analyze_export(ParticleSystem(loaded.settings));
        const bool blocked = std::ranges::any_of(
            report.blockers, [](const ExportBlocker& b) { return b.kind == ExportBlocker::Kind::TooManyVertices; });
        CHECK(blocked == !v.accepted);
        CHECK(report.vertices == v.vertices);
    }
}
