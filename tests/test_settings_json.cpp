#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Presets.hpp"
#include "SettingsJson.hpp"

using namespace gs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

bool has_warning(const std::vector<std::string>& warnings, std::string_view needle)
{
    return std::ranges::any_of(warnings, [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("every field is written under its key")
{
    const json j = to_json(Settings{});
    CHECK(j.is_object());
    CHECK(j.at("count") == 64);
    CHECK(j.at("end_behavior") == "hide_underground");
    CHECK(j.at("velocity") == json::array({0.0, 0.0, 0.0}));
    CHECK(j.at("core_color") == json::array({255, 170, 85}));
    CHECK(j.at("texture_file") == "");
}

TEST_CASE("the settings file text is JSON in field order")
{
    const Settings s;
    const std::string text = settings_file_text(s);
    CHECK(text.starts_with("{\n  \"model_name\": "));
    CHECK(text.ends_with("\n}\n"));
    CHECK(json::parse(text) == to_json(s));
    CHECK(text.find("\"count\"") < text.find("\"duration\""));
    CHECK(text.find("\"duration\"") < text.find("\"spin_spread\""));
}

TEST_CASE("every preset survives a write and a read")
{
    for (const Preset& preset : builtin_presets()) {
        CAPTURE(preset.name);
        Settings s;
        apply_preset(s, preset);

        const SettingsLoad loaded = settings_from_json(json::parse(settings_file_text(s)), Settings{});
        CHECK(loaded.warnings.empty());
        CHECK(loaded.settings == s);
    }
}

TEST_CASE("awkward values survive a write and a read")
{
    Settings s;
    s.model_name = "with \"quotes\", \\ and ünïcode";
    s.texture.file = "dir/with space/tex.png";
    s.duration = 0.1 + 0.2;   // not exactly representable
    s.gravity = -1e-7;
    s.velocity = {1.0 / 3.0, -2.5, 1e6};
    s.texture.core_color = {0, 1, 255};

    const SettingsLoad loaded = settings_from_json(json::parse(settings_file_text(s)), Settings{});
    CHECK(loaded.settings == s);
}

TEST_CASE("missing keys keep the base values")
{
    Settings base;
    base.count = 7;
    base.gravity = -9;
    const SettingsLoad loaded = settings_from_json(json{{"fps", 15}}, base);
    CHECK(loaded.settings.fps == 15);
    CHECK(loaded.settings.count == 7);
    CHECK(loaded.settings.gravity == -9);
    CHECK(loaded.warnings.empty());
}

TEST_CASE("numbers and booleans are accepted in their relaxed forms")
{
    Settings s;
    const auto warnings = apply_json(s, json{{"duration", "2.5"},
                                             {"gravity", " -40 "},
                                             {"count", 12.9},
                                             {"fps", "20"},
                                             {"hollow_ring", 1},
                                             {"double_sided", "false"}});
    CHECK(warnings.empty());
    CHECK(s.duration == 2.5);
    CHECK(s.gravity == -40);
    CHECK(s.count == 12);   // truncated toward zero
    CHECK(s.fps == 20);
    CHECK(s.hollow_ring);
    CHECK_FALSE(s.double_sided);
}

TEST_CASE("a value of the wrong type is skipped and reported")
{
    Settings s;
    const Settings before = s;
    const auto warnings = apply_json(s, json{{"count", "many"},
                                             {"duration", json::array({1, 2})},
                                             {"hollow_ring", "maybe"},
                                             {"model_name", 42},
                                             {"velocity", json::array({1, 2})},
                                             {"core_color", "red"},
                                             {"geometry", "sphere"},
                                             {"pattern", 3}});
    CHECK(warnings.size() == 8);
    CHECK(s == before);
    CHECK(has_warning(warnings, "count: expected a number"));
    CHECK(has_warning(warnings, "velocity: expected an [x, y, z] array"));
    CHECK(has_warning(warnings, "core_color: expected an [r, g, b] array"));
    CHECK(has_warning(warnings, "geometry: expected one of single_quad"));
    CHECK(has_warning(warnings, "(ignored)"));
}

TEST_CASE("one bad vector component rejects the whole vector")
{
    Settings s;
    const auto warnings = apply_json(s, json{{"velocity", json::array({1, "x", 3})}});
    CHECK(warnings.size() == 1);
    CHECK(s.velocity == Vec3{});
}

TEST_CASE("colors are clamped and extra entries are ignored")
{
    Settings s;
    const auto warnings = apply_json(s, json{{"core_color", json::array({300, -5, 127.6, 99})}});
    CHECK(warnings.empty());
    CHECK(s.texture.core_color == Rgb{255, 0, 128});
}

TEST_CASE("unknown keys are reported and ignored")
{
    Settings s;
    const auto warnings = apply_json(s, json{{"colour", 1}, {"count", 5}});
    CHECK(warnings.size() == 1);
    CHECK(has_warning(warnings, "unknown setting 'colour'"));
    CHECK(s.count == 5);
}

TEST_CASE("a settings file that is not an object is an error")
{
    Settings s;
    CHECK_THROWS_AS(apply_json(s, json::array({1, 2})), std::runtime_error);
    CHECK_THROWS_AS(apply_json(s, json("text")), std::runtime_error);
    CHECK_THROWS_AS(apply_json(s, json(nullptr)), std::runtime_error);
}

TEST_CASE("old files with only a width use it for the depth")
{
    Settings s;
    apply_json(s, json{{"volume_width", 300}});
    CHECK(s.volume_depth == 300);

    Settings both;
    apply_json(both, json{{"volume_width", 300}, {"volume_depth", 50}});
    CHECK(both.volume_depth == 50);
}

TEST_CASE("loading validates, reports what changed and leaves the base alone")
{
    const Settings base;
    const SettingsLoad loaded =
        settings_from_json(json{{"duration", 1000}, {"fps", 0}, {"count", 100000}, {"texture_size", 100}}, base);
    CHECK(loaded.settings.duration == kMaxDuration);
    CHECK(loaded.settings.fps == kMinFps);
    CHECK(loaded.settings.count == 126);
    CHECK(loaded.settings.texture.size == 128);
    CHECK(loaded.warnings.size() == 4);
    CHECK(has_warning(loaded.warnings, "duration"));
    CHECK(has_warning(loaded.warnings, "is out of range, using"));
    CHECK(base == Settings{});
}

TEST_CASE("a file that is already valid loads without warnings")
{
    const SettingsLoad loaded = settings_from_json(to_json(Settings{}), Settings{});
    CHECK(loaded.warnings.empty());
}

TEST_CASE("read_settings_file reads a file and rejects bad ones")
{
    const fs::path dir = fs::temp_directory_path() / "particlemdl_json_test";
    fs::create_directories(dir);

    const fs::path good = dir / "good.json";
    Settings s;
    s.count = 9;
    s.model_name = "from_disk";
    std::ofstream(good) << settings_file_text(s);
    const SettingsLoad loaded = read_settings_file(good, Settings{});
    CHECK(loaded.settings.count == 9);
    CHECK(loaded.settings.model_name == "from_disk");

    const fs::path broken = dir / "broken.json";
    std::ofstream(broken) << "{ \"count\": ";
    CHECK_THROWS_AS(read_settings_file(broken, Settings{}), std::exception);

    const fs::path truncated = dir / "empty.json";
    std::ofstream(truncated).close();
    CHECK_THROWS_AS(read_settings_file(truncated, Settings{}), std::exception);

    CHECK_THROWS_AS(read_settings_file(dir / "missing.json", Settings{}), std::runtime_error);

    fs::remove_all(dir);
}
