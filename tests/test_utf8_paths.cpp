// Paths with characters outside ASCII. The names are written as UTF-8 byte
// escapes so they mean the same under every compiler and source encoding:
//   "Jos\xC3\xA9"                 Jose with an acute e
//   "\xE6\x97\xA5\xE6\x9C\xAC"    two Japanese characters
//   "\xF0\x9F\x98\x80"            a character outside the Basic Multilingual Plane
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "ImageImport.hpp"
#include "PathInput.hpp"
#include "PathUtil.hpp"
#include "SettingsJson.hpp"
#include "TextureGenerator.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

const std::string kJose = "Jos\xC3\xA9";
const std::string kJapanese = "\xE6\x97\xA5\xE6\x9C\xAC";
const std::string kEmoji = "\xF0\x9F\x98\x80";

struct TempDir {
    fs::path path;

    explicit TempDir(const std::string& utf8_name) : path(fs::temp_directory_path() / from_utf8(utf8_name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

void write_bmp(const fs::path& path)
{
    RgbImage img(8, 8);
    for (std::size_t i = 0; i < img.pixels.size(); ++i)
        img.pixels[i] = static_cast<std::uint8_t>(i * 7);
    const std::vector<std::uint8_t> bytes = encode_indexed_bmp(img);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("to_utf8 and from_utf8 are inverse for text outside ASCII")
{
    for (const std::string& text : {std::string("plain"), kJose, kJapanese, kEmoji, kJose + "/" + kJapanese + "/" + kEmoji}) {
        CAPTURE(text);
        CHECK(to_utf8(from_utf8(text)) == text);
    }
    CHECK(from_utf8("").empty());
    CHECK(to_utf8(from_utf8("a") / from_utf8(kJose)) == std::string("a") + static_cast<char>(fs::path::preferred_separator) + kJose);
}

TEST_CASE("an image is read from a folder and a file with non-ASCII names")
{
    const TempDir dir(kJose + "_" + kJapanese + "_img");
    const fs::path file = dir.path / from_utf8(kEmoji + "tex" + kJose + ".bmp");
    write_bmp(file);

    const RgbaImage img = load_image_file(file);
    CHECK(img.width == 8);
    CHECK(img.height == 8);

    // An unreadable one reports its name as UTF-8.
    try {
        load_image_file(dir.path / from_utf8(kJose + "_missing.png"));
        FAIL("loading a missing file should throw");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find(kJose + "_missing.png") != std::string::npos);
    }
}

TEST_CASE("an imported texture is found by its UTF-8 path in the settings")
{
    const TempDir dir(kJapanese + "_tex");
    const fs::path file = dir.path / from_utf8(kJose + ".bmp");
    write_bmp(file);

    Settings s;
    s.texture_file = to_utf8(file);
    const IndexedImage img = export_texture(s);
    CHECK(img.width == s.texture_size);
}

TEST_CASE("settings keep a non-ASCII texture path through a settings file")
{
    const TempDir dir(kJose + "_settings");
    Settings s;
    s.texture_file = to_utf8(dir.path / from_utf8(kJapanese + ".png"));

    const fs::path file = dir.path / from_utf8(kEmoji + ".json");
    std::ofstream(file, std::ios::binary) << settings_file_text(s);

    const SettingsLoad loaded = read_settings_file(file, Settings{});
    CHECK(loaded.settings.texture_file == s.texture_file);
    CHECK(loaded.warnings.empty());

    // The path is stored as the UTF-8 text itself, not escaped or converted.
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find(kJapanese + ".png") != std::string::npos);
}

TEST_CASE("an unreadable settings file reports its path as UTF-8")
{
    const fs::path missing = fs::temp_directory_path() / from_utf8(kJose + "_nothing.json");
    try {
        read_settings_file(missing, Settings{});
        FAIL("reading a missing file should throw");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find(kJose + "_nothing.json") != std::string::npos);
    }
}

TEST_CASE("an export goes to a folder with non-ASCII characters in its name")
{
    const TempDir dir(kJose + "_" + kJapanese + "_export");
    export_model(ParticleSystem{Settings{}}, dir.path);

    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir.path))
        names.push_back(to_utf8(entry.path().filename()));
    std::ranges::sort(names);
    CHECK(names == std::vector<std::string>{"custom_particles.bmp", "custom_particles.qc", "custom_particles_anim.smd",
                                            "custom_particles_ref.smd"});

    // A failure names the folder as UTF-8 text.
    try {
        export_model(ParticleSystem{Settings{}}, dir.path / from_utf8(kEmoji));
        FAIL("exporting into a missing folder should throw");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find(kEmoji) != std::string::npos);
    }
}

TEST_CASE("typed names with non-ASCII characters are resolved in a dialog")
{
    const TempDir dir(kJose + "_dialog");
    fs::create_directories(dir.path / from_utf8(kJapanese));
    std::ofstream(dir.path / from_utf8(kJose + ".json")) << "{}";

    const DialogInput folder = resolve_dialog_input(dir.path, kJapanese, false, ".json");
    CHECK(folder.action == DialogInput::Action::Navigate);
    CHECK(folder.path == dir.path / from_utf8(kJapanese));

    const DialogInput file = resolve_dialog_input(dir.path, kJose + ".json", false, ".json");
    CHECK(file.action == DialogInput::Action::Choose);
    CHECK(file.path == dir.path / from_utf8(kJose + ".json"));

    const DialogInput created = resolve_dialog_input(dir.path, kEmoji + "new", true, ".json");
    CHECK(created.action == DialogInput::Action::Choose);
    CHECK(created.path == dir.path / from_utf8(kEmoji + "new.json"));

    const DialogInput existing = resolve_dialog_input(dir.path, kJose, true, ".json");
    CHECK(existing.action == DialogInput::Action::Confirm);

    const DialogInput missing = resolve_dialog_input(dir.path, kJapanese + "x.json", false, ".json");
    CHECK(missing.action == DialogInput::Action::Error);
    CHECK(missing.message.find(kJapanese + "x.json") != std::string::npos);
}
