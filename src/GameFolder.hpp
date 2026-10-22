#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs {

// The game's mod folder (Half-Life/valve, Half-Life/cstrike, ...). The editor exports into its
// models/ folder, or a folder inside it (models/mymodels): the path the game loads models from.

// A folder typed or pasted, cleaned up: no spaces around it, and not in the quotes a Windows "Copy as
// path" puts around it.
std::string clean_folder_text(std::string_view text);

// A folder inside models/ as typed, with "/" between its parts whatever was typed ("\" works on every
// system: no folder name on Windows can have one) and no trailing "/".
std::string normalized_subfolder(std::string_view sub);

// Why `game` cannot be used, or nullopt: not set, missing, or without a models/ folder (as when
// Half-Life itself is chosen instead of one of its mod folders).
std::optional<std::string> game_folder_problem(const std::filesystem::path& game);

// Why `sub` cannot be a folder inside models/, or nullopt. Empty is models/ itself; an absolute
// path, a drive and ".." are refused, so an export never leaves models/. Either separator works.
std::optional<std::string> models_subfolder_problem(std::string_view sub);

// <game>/models/<sub>, `sub` normalized.
std::filesystem::path export_folder(const std::filesystem::path& game, std::string_view sub);

// "cstrike/models/": the models folder named after its mod folder, for labels; "<game>/models/" when
// no game folder is set.
std::string models_label(const std::filesystem::path& game);

// `chosen` as a folder inside <game>/models/ ("" for models/ itself, "mymodels/fx"), or nullopt when it
// is not inside it.
std::optional<std::string> subfolder_within_models(const std::filesystem::path& game, const std::filesystem::path& chosen);

// What the editor keeps between sessions.
struct AppConfig {
    std::string game_folder;   // UTF-8; empty until it is set
};

// Where AppConfig is kept: %APPDATA%\ParticleMDL\config.json on Windows, else
// $XDG_CONFIG_HOME/particlemdl/config.json or ~/.config/particlemdl/config.json. Empty if there is
// no such place (no home folder).
std::filesystem::path app_config_path();

// The config at `path`; the defaults if it is missing or cannot be read.
AppConfig read_app_config(const std::filesystem::path& path);
// Writes it whole or not at all, making its folder if needed. Throws on failure.
void write_app_config(const std::filesystem::path& path, const AppConfig& config);

} // namespace gs
