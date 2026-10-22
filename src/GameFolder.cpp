#include "GameFolder.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "PathUtil.hpp"
#include "StagedFiles.hpp"

namespace gs {

namespace fs = std::filesystem;

std::string clean_folder_text(std::string_view text)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    return std::string(text);
}

std::string normalized_subfolder(std::string_view sub)
{
    std::string out = clean_folder_text(sub);
    for (char& c : out)
        if (c == '\\')
            c = '/';
    while (out.size() > 1 && out.back() == '/')   // a lone "/" stays, to be refused as a root
        out.pop_back();
    return out;
}

std::optional<std::string> game_folder_problem(const fs::path& game)
{
    if (game.empty())
        return "No game folder: set it in Settings > Game Folder";
    std::error_code ec;
    if (!fs::is_directory(game, ec))
        return "The game folder does not exist: " + to_utf8(game);
    if (!fs::is_directory(game / "models", ec))
        return "The game folder has no models folder: choose the mod folder itself, such as Half-Life/valve or "
               "Half-Life/cstrike";
    return std::nullopt;
}

std::optional<std::string> models_subfolder_problem(std::string_view typed)
{
    const std::string sub = normalized_subfolder(typed);
    const fs::path p = from_utf8(sub);
    if (p.has_root_name() || p.has_root_directory())
        return "The export folder is a folder inside models: write it without a drive or a leading slash";
    for (const fs::path& part : p)
        if (part == "..")
            return "The export folder must stay inside models (no \"..\")";
    for (const char c : sub)
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            static_cast<unsigned char>(c) < 0x20)
            return "The export folder has a character folders cannot have";
    return std::nullopt;
}

fs::path export_folder(const fs::path& game, std::string_view typed)
{
    const std::string sub = normalized_subfolder(typed);
    fs::path folder = game / "models";
    if (!sub.empty())
        folder /= from_utf8(sub);
    return folder.lexically_normal();
}

std::string models_label(const fs::path& game)
{
    fs::path g = game;
    if (!g.has_filename())   // "C:/Half-Life/cstrike/"
        g = g.parent_path();
    const std::string mod = g.filename().empty() ? "<game>" : to_utf8(g.filename());
    return mod + "/models/";
}

std::optional<std::string> subfolder_within_models(const fs::path& game, const fs::path& chosen)
{
    std::error_code ec;
    const fs::path models = fs::weakly_canonical(game / "models", ec);
    if (ec)
        return std::nullopt;
    const fs::path inside = fs::weakly_canonical(chosen, ec);
    if (ec)
        return std::nullopt;
    const fs::path relative = inside.lexically_relative(models);
    if (relative.empty())
        return std::nullopt;   // another drive, or not related
    if (relative == ".")
        return std::string();
    std::string joined;   // with "/" on every system, in UTF-8
    for (const fs::path& part : relative) {
        if (part == "..")
            return std::nullopt;
        joined += (joined.empty() ? "" : "/") + to_utf8(part);
    }
    return joined;
}

fs::path app_config_path()
{
#ifdef _WIN32
    if (const wchar_t* appdata = _wgetenv(L"APPDATA"); appdata && *appdata)
        return fs::path(appdata) / "ParticleMDL" / "config.json";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg == '/')
        return fs::path(xdg) / "particlemdl" / "config.json";
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".config" / "particlemdl" / "config.json";
#endif
    return {};
}

AppConfig read_app_config(const fs::path& path)
{
    AppConfig config;
    std::ifstream in(path);
    if (!in)
        return config;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_object())
        if (const auto it = j.find("game_folder"); it != j.end() && it->is_string())
            config.game_folder = it->get<std::string>();
    return config;
}

void write_app_config(const fs::path& path, const AppConfig& config)
{
    if (path.empty())
        throw std::runtime_error("there is no place to keep the settings (no home folder)");
    fs::create_directories(path.parent_path());
    write_file_atomically(path, nlohmann::json{{"game_folder", config.game_folder}}.dump(2) + "\n");
}

} // namespace gs
