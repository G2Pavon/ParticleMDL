// Golden-file tests: every built-in preset is exported and compared with a
// snapshot, so any change to the simulation, the SMD/QC writer or the texture
// pipeline shows up as a diff.
//
// tests/golden/<preset>.qc holds the QC in full (small and readable). The SMDs
// and the BMP are large, so tests/golden/manifest.txt keeps a size and an
// FNV-1a 64-bit hash of every exported file instead.
//
// After an intended change, regenerate the snapshots with
//     GS_UPDATE_GOLDEN=1 ./particlemdl_tests -tc="golden*"
// and review the diff. A failing run leaves the files it produced in
// <temp dir>/particlemdl_golden_actual/<preset>/ for comparison.
//
// The snapshots were made on Linux/glibc. libm (sin, cos, atan2, ...) is not
// bit-exact between platforms, so another platform may need its own run.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "Exporter.hpp"
#include "Presets.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

const fs::path kGoldenDir = GS_GOLDEN_DIR;

struct FileInfo {
    std::uintmax_t size = 0;
    std::uint64_t hash = 0;

    bool operator==(const FileInfo&) const = default;
};

std::string read_file(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

FileInfo describe(const std::string& bytes)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return {bytes.size(), h};
}

// "Campfire Sparks" -> "campfire_sparks"
std::string slug(std::string_view name)
{
    std::string out;
    for (const char c : name)
        out += c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// What one preset exports, by file suffix.
using Export = std::map<std::string, std::string>;

Export export_preset(const Preset& preset)
{
    Settings s = preset.settings;
    apply_preset(s, preset);
    s.model_name = slug(preset.name);
    const ParticleSystem sys(s);

    const fs::path dir = fs::temp_directory_path() / ("particlemdl_golden_" + s.model_name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    export_model(sys, dir);

    Export out;
    for (const char* suffix : {".qc", "_ref.smd", "_anim.smd", ".bmp"})
        out[suffix] = read_file(dir / (s.model_name + suffix));
    fs::remove_all(dir);
    return out;
}

using Manifest = std::map<std::string, FileInfo>;   // "<preset>/<suffix>" -> info

Manifest read_manifest()
{
    Manifest m;
    std::ifstream f(kGoldenDir / "manifest.txt");
    std::string key;
    FileInfo info;
    while (f >> key >> info.size >> std::hex >> info.hash >> std::dec)
        m[key] = info;
    return m;
}

void write_manifest(const Manifest& m)
{
    std::ofstream f(kGoldenDir / "manifest.txt");
    for (const auto& [key, info] : m)
        f << key << ' ' << info.size << ' ' << std::hex << info.hash << std::dec << '\n';
}

void save_actual(std::string_view preset, const Export& files)
{
    const fs::path dir = fs::temp_directory_path() / "particlemdl_golden_actual" / preset;
    fs::create_directories(dir);
    for (const auto& [suffix, bytes] : files) {
        std::ofstream f(dir / (std::string(preset) + suffix), std::ios::binary);
        f << bytes;
    }
}

bool updating()
{
    const char* env = std::getenv("GS_UPDATE_GOLDEN");
    return env && *env && std::string_view(env) != "0";
}

} // namespace

TEST_CASE("golden: built-in presets export byte for byte as recorded")
{
    const bool update = updating();
    const Manifest recorded = update ? Manifest{} : read_manifest();
    Manifest current;

    REQUIRE(!builtin_presets().empty());
    for (const Preset& preset : builtin_presets()) {
        const std::string name = slug(preset.name);
        CAPTURE(name);
        const Export files = export_preset(preset);

        for (const auto& [suffix, bytes] : files)
            current[name + "/" + suffix] = describe(bytes);

        const fs::path qc = kGoldenDir / (name + ".qc");
        if (update) {
            fs::create_directories(kGoldenDir);
            std::ofstream(qc, std::ios::binary) << files.at(".qc");
            continue;
        }

        bool ok = read_file(qc) == files.at(".qc");
        CHECK_MESSAGE(ok, "QC differs from ", qc.string());
        for (const auto& [suffix, bytes] : files) {
            const std::string key = name + "/" + suffix;
            const auto it = recorded.find(key);
            const bool known = it != recorded.end();
            CHECK_MESSAGE(known, "no snapshot recorded for ", key);
            const bool same = known && it->second == current[key];
            CHECK_MESSAGE(same, key, " differs from its snapshot (size ", bytes.size(), ")");
            ok = ok && same;
        }
        if (!ok)
            save_actual(name, files);
    }

    if (update) {
        write_manifest(current);
        MESSAGE("golden files rewritten in ", kGoldenDir.string());
    } else {
        // A snapshot for a preset that no longer exists is stale.
        CHECK_MESSAGE(recorded.size() == current.size(), "manifest and presets disagree on the file count");
    }
}

TEST_CASE("golden: exporting twice gives identical bytes")
{
    const Preset& preset = builtin_presets().front();
    CHECK(export_preset(preset) == export_preset(preset));
}
