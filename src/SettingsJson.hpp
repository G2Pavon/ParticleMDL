#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "Settings.hpp"

namespace gs {

// The settings file: one key per Settings field (see for_each_field), vectors
// as [x, y, z], colors as [r, g, b], enums by id.
nlohmann::json to_json(const Settings& s);
// The text of a settings file: keys in field order, one per line.
std::string settings_file_text(const Settings& s);

// Applies the keys present in `j`; missing keys keep their value. A value of
// the wrong type is skipped and reported, numbers given as strings and 0/1
// booleans are accepted. Throws when `j` is not an object. Does not validate.
std::vector<std::string> apply_json(Settings& s, const nlohmann::json& j);

struct SettingsLoad {
    Settings settings;                   // validated
    std::vector<std::string> warnings;   // skipped keys, adjusted values
};

// `base` with the json applied, then validated. `base` itself is not touched,
// so a failed load leaves the caller's settings as they were.
SettingsLoad settings_from_json(const nlohmann::json& j, Settings base);
// Throws on unreadable files and malformed JSON.
SettingsLoad read_settings_file(const std::filesystem::path& path, Settings base);

} // namespace gs
