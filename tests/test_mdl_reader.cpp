// The reader is tried on two models compiled by the real studiomdl (Sven Co-op's, Nov 30 2018
// build, run under Wine) from settings files kept next to them:
//   crossed_static.mdl  20 particles, crossed quads, static: 21 bones
//   vertex_bones.mdl    25 particles with vertex-bone scaling: 126 bones
#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "MdlReader.hpp"
#include "SettingsJson.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = fs::path(GS_FIXTURE_DIR) / "real_mdl";

std::vector<std::uint8_t> read_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Settings settings_of(const char* name)
{
    return read_settings_file(kFixtures / (std::string(name) + ".json"), Settings{}).settings;
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value)
{
    REQUIRE(at + 4 <= bytes.size());
    for (int i = 0; i < 4; ++i)
        bytes[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
}

// Offsets of the header fields the tests patch.
constexpr std::size_t kLength = 72, kNumBones = 140, kBoneIndex = 144, kSeqIndex = 168;

} // namespace

TEST_CASE("a model compiled by studiomdl is read back")
{
    const MdlInfo m = read_mdl(read_bytes(kFixtures / "crossed_static.mdl"));
    CHECK(m.version == 10);
    CHECK(m.name == "a1.mdl");
    CHECK(m.length == 17172);

    REQUIRE(m.bones.size() == 21);
    CHECK(m.bones[0].name == "root");
    CHECK(m.bones[0].parent == -1);
    CHECK(m.bones[1].name == "p_1");
    CHECK(m.bones[1].parent == 0);
    CHECK(m.bones[20].name == "p_20");

    REQUIRE(m.sequences.size() == 1);
    const MdlSequence& seq = m.sequences[0];
    CHECK(seq.label == "idle");
    CHECK(seq.frames == 31);
    CHECK(seq.fps == 15.0f);
    CHECK((seq.flags & kMdlSequenceLooping) != 0);
    CHECK(seq.blends == 1);
    CHECK_FALSE(seq.external);
    CHECK(seq.offset_bytes == 21 * 12);
    CHECK(seq.value_bytes == 3642);

    REQUIRE(m.textures.size() == 1);
    CHECK(m.textures[0].name == "a1.bmp");
    CHECK((m.textures[0].flags & kMdlTextureAdditive) != 0);
    CHECK(m.textures[0].width == 64);
    CHECK(m.textures[0].height == 64);
    CHECK(m.body_parts == 1);
}

TEST_CASE("corner bones of a vertex-bone model hang on their particle's bone")
{
    const MdlInfo m = read_mdl(read_bytes(kFixtures / "vertex_bones.mdl"));
    REQUIRE(m.bones.size() == 126);
    CHECK(m.bones[1].name == "p_1");
    CHECK(m.bones[2].name == "c_2");
    CHECK(m.bones[2].parent == 1);
    CHECK(m.bones[5].parent == 1);
    CHECK(m.bones[6].name == "p_6");
    CHECK(m.bones[6].parent == 0);
    CHECK(m.sequences[0].value_bytes == 15444);
}

TEST_CASE("the animation size in a real model is exactly what sequence_bytes predicted")
{
    for (const char* name : {"crossed_static", "vertex_bones"}) {
        CAPTURE(name);
        const MdlInfo m = read_mdl(read_bytes(kFixtures / (std::string(name) + ".mdl")));
        const ParticleSystem sys(settings_of(name));
        // studiomdl measures from the last bone's table of offsets: its 12 bytes and all the values.
        CHECK(m.sequences[0].value_bytes + 12 == sequence_bytes(sys));
    }
}

TEST_CASE("a compiled model agrees with the export it was made from")
{
    for (const char* name : {"crossed_static", "vertex_bones"}) {
        CAPTURE(name);
        const MdlInfo m = read_mdl(read_bytes(kFixtures / (std::string(name) + ".mdl")));
        const std::vector<std::string> problems = compare_with_export(m, ParticleSystem(settings_of(name)));
        CHECK(problems.empty());
        for (const std::string& p : problems)
            MESSAGE(p);
    }
}

TEST_CASE("compare_with_export names what differs")
{
    const MdlInfo m = read_mdl(read_bytes(kFixtures / "crossed_static.mdl"));
    auto problems_with = [&](auto change) {
        Settings s = settings_of("crossed_static");
        change(s);
        return compare_with_export(m, ParticleSystem(s));
    };
    auto mentions = [](const std::vector<std::string>& problems, std::string_view text) {
        for (const std::string& p : problems)
            if (p.find(text) != std::string::npos)
                return true;
        return false;
    };

    CHECK(mentions(problems_with([](Settings& s) { s.count = 21; }), "21 bones, the export has 22"));
    CHECK(mentions(problems_with([](Settings& s) { s.fps = 30; }), "frames"));
    CHECK(mentions(problems_with([](Settings& s) { s.duration = 3.0; }), "31 frames, the export has 46"));
    CHECK(mentions(problems_with([](Settings& s) { s.texture.size = 128; }), "64 x 64, the export has 128 x 128"));
    // Spinning adds three rotation channels to every bone, so the animation is bigger than the model's.
    CHECK(mentions(problems_with([](Settings& s) { s.particle_spin = {10, 20, 30}; }), "bytes of animation values"));
    CHECK(problems_with([](Settings&) {}).empty());
}

TEST_CASE("describe_mdl gives a short summary")
{
    const std::string text = describe_mdl(read_mdl(read_bytes(kFixtures / "crossed_static.mdl")));
    CHECK(text.find("idle: 31 frames at 15 fps") != std::string::npos);
    CHECK(text.find("21 bones") != std::string::npos);
    CHECK(text.find("1 texture (additive)") != std::string::npos);
}

TEST_CASE("read_mdl_file reads a file and reports one that is missing")
{
    CHECK(read_mdl_file(kFixtures / "crossed_static.mdl").bones.size() == 21);
    CHECK_THROWS_AS(read_mdl_file(kFixtures / "no_such.mdl"), std::runtime_error);
}

TEST_CASE("data that is not a version 10 model is refused")
{
    CHECK_THROWS_AS(read_mdl({}), MdlError);
    const std::vector<std::uint8_t> text{'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!'};
    CHECK_THROWS_AS(read_mdl(text), MdlError);

    std::vector<std::uint8_t> bytes = read_bytes(kFixtures / "crossed_static.mdl");
    bytes[4] = 9;   // version
    try {
        read_mdl(bytes);
        FAIL("version 9 should be refused");
    } catch (const MdlError& e) {
        CHECK(std::string(e.what()).find("version 9") != std::string::npos);
    }

    bytes = read_bytes(kFixtures / "crossed_static.mdl");
    bytes[0] = 'X';
    CHECK_THROWS_AS(read_mdl(bytes), MdlError);
}

TEST_CASE("a header that disagrees with the file size is refused")
{
    std::vector<std::uint8_t> bytes = read_bytes(kFixtures / "crossed_static.mdl");
    bytes.pop_back();
    try {
        read_mdl(bytes);
        FAIL("a cut file should be refused");
    } catch (const MdlError& e) {
        CHECK(std::string(e.what()).find("17172") != std::string::npos);
    }
}

TEST_CASE("inconsistent counts and offsets are refused")
{
    const std::vector<std::uint8_t> good = read_bytes(kFixtures / "crossed_static.mdl");

    auto refused_with = [&](auto patch) {
        std::vector<std::uint8_t> bytes = good;
        patch(bytes);
        try {
            read_mdl(bytes);
        } catch (const MdlError& e) {
            return std::string(e.what());
        }
        return std::string();
    };

    CHECK(refused_with([](auto& b) { put32(b, kNumBones, 0xFFFFFFFFu); }).find("bones") != std::string::npos);
    CHECK(refused_with([](auto& b) { put32(b, kNumBones, 5000); }).find("bones") != std::string::npos);
    CHECK(refused_with([](auto& b) { put32(b, kBoneIndex, 0x7FFFFFFF); }).find("ends inside") != std::string::npos);
    CHECK(refused_with([](auto& b) { put32(b, kSeqIndex, 0x80000000u); }).find("negative") != std::string::npos);
    // The parent of bone 1 is 77 and there are 21 bones.
    CHECK(refused_with([](auto& b) { put32(b, 244 + 112 + 32, 77); }).find("parent 77") != std::string::npos);
    // A run of no frames would make the reader loop for ever.
    CHECK(refused_with([](auto& b) {
              const std::size_t anim = 3236;   // where the animation of this model starts
              const std::uint16_t first = static_cast<std::uint16_t>(b[anim + 12] | b[anim + 13] << 8);
              // bone 1's first animated channel: find one with an offset and zero its frame count
              for (std::size_t c = 0; c < 6; ++c) {
                  const std::size_t at = anim + 12 + c * 2;
                  const std::uint16_t off = static_cast<std::uint16_t>(b[at] | b[at + 1] << 8);
                  if (off != 0) {
                      b[anim + 12 + off + 1] = 0;
                      break;
                  }
              }
              (void)first;
          }).find("run of no frames") != std::string::npos);
}

TEST_CASE("a model cut anywhere inside what the reader reads is refused without reading outside the data")
{
    const std::vector<std::uint8_t> good = read_bytes(kFixtures / "crossed_static.mdl");
    std::size_t first_accepted = good.size();
    for (std::size_t length = 0; length < good.size(); ++length) {
        std::vector<std::uint8_t> bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        if (length >= kLength + 4)
            put32(bytes, kLength, static_cast<std::uint32_t>(length));   // let the header agree, so the offsets are what fail
        bool accepted = true;
        try {
            read_mdl(bytes);
        } catch (const MdlError&) {
            accepted = false;
        }
        if (accepted && first_accepted == good.size())
            first_accepted = length;
        // Once the whole of what is read is there, a longer file (the rest is mesh data) is fine too.
        if (first_accepted < good.size())
            CHECK(accepted);
        else
            CHECK_FALSE(accepted);
    }
    // The reader needs the header, bones, animation, sequence and texture descriptions, which end well
    // before the mesh data that makes up the rest of the file.
    CHECK(first_accepted > 7132 + 176);
    CHECK(first_accepted < good.size());
}

TEST_CASE("a damaged model ends in an error or a result, never a crash or a hang")
{
    const std::vector<std::uint8_t> good = read_bytes(kFixtures / "vertex_bones.mdl");
    std::uint32_t state = 2024;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    int refused = 0, accepted = 0;
    for (int i = 0; i < 6000; ++i) {
        std::vector<std::uint8_t> bytes = good;
        // The header, the bones, the sequence description and the start of the animation hold the offsets.
        const std::size_t region = 244 + 126 * 112 + 176 + 4000;
        for (int k = 0; k < 1 + static_cast<int>(next() % 4); ++k) {
            const std::size_t at = next() % region;
            bytes[at] = static_cast<std::uint8_t>(next());
        }
        try {
            read_mdl(bytes);
            ++accepted;
        } catch (const MdlError&) {
            ++refused;
        }
    }
    CHECK(refused + accepted == 6000);
    CHECK(refused > 0);
}

TEST_CASE("a box with vertex bones compiles with shared corner bones")
{
    // 14 particles of nine bones each: the particle and the eight corners its six quads share.
    const MdlInfo m = read_mdl(read_bytes(kFixtures / "box_vertex_bones.mdl"));
    REQUIRE(m.bones.size() == 1 + 14 * 9);
    CHECK(m.bones[1].name == "p_1");
    CHECK(m.bones[9].name == "c_9");
    CHECK(m.bones[9].parent == 1);
    CHECK(m.bones[10].name == "p_10");
    CHECK(m.bones[10].parent == 0);

    const ParticleSystem sys(settings_of("box_vertex_bones"));
    CHECK(compare_with_export(m, sys).empty());
    CHECK(m.sequences[0].value_bytes + 12 == sequence_bytes(sys));
}

TEST_CASE("a one-shot effect compiles to a sequence that does not loop")
{
    const MdlInfo m = read_mdl(read_bytes(kFixtures / "one_shot.mdl"));
    REQUIRE(m.sequences.size() == 1);
    CHECK((m.sequences[0].flags & kMdlSequenceLooping) == 0);
    CHECK(m.sequences[0].frames == 46);   // 45 steps and the frame the effect ends on
    CHECK(m.sequences[0].fps == 30.0f);

    const ParticleSystem sys(settings_of("one_shot"));
    CHECK_FALSE(sys.looping());
    CHECK(compare_with_export(m, sys).empty());
    CHECK(m.sequences[0].value_bytes + 12 == sequence_bytes(sys));
}
