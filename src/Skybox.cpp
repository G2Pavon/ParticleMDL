#include "Skybox.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>

#include "ImageImport.hpp"
#include "PathUtil.hpp"

namespace gs {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The entry of `dir` called `name`, whatever its case.
std::optional<fs::path> child_any_case(const fs::path& dir, std::string_view name)
{
    std::error_code ec;
    if (fs::is_directory(dir / from_utf8(name), ec))
        return dir / from_utf8(name);
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (lower(to_utf8(it->path().filename())) == lower(name))
            return it->path();
    return std::nullopt;
}

// liblist.gam's fallback_dir "name", if it has one.
std::string fallback_dir(const fs::path& game)
{
    std::ifstream in(game / "liblist.gam");
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t key = lower(line).find("fallback_dir");
        if (key == std::string::npos)
            continue;
        const std::size_t open = line.find('"', key), close = line.find('"', open + 1);
        if (open != std::string::npos && close != std::string::npos && close > open + 1)
            return line.substr(open + 1, close - open - 1);
    }
    return {};
}

// Every image of every gfx/env, by lower-case file name; the first folder that has it wins.
std::map<std::string, fs::path> env_files(const fs::path& game)
{
    std::map<std::string, fs::path> files;
    for (const fs::path& folder : sky_search_folders(game)) {
        const auto gfx = child_any_case(folder, "gfx");
        const auto env = gfx ? child_any_case(*gfx, "env") : std::nullopt;
        if (!env)
            continue;
        std::error_code ec;
        for (fs::directory_iterator it(*env, ec), end; !ec && it != end; it.increment(ec))
            files.emplace(lower(to_utf8(it->path().filename())), it->path());
    }
    return files;
}

// The six files of sky `name` (lower case), tried as the engine does: .tga before .bmp, Half-Life's
// naming before Quake's.
std::optional<std::array<fs::path, 6>> sky_files(const std::map<std::string, fs::path>& files, const std::string& name)
{
    for (const char* ext : {".tga", ".bmp"})
        for (const char* delim : {"", "_"}) {
            std::array<fs::path, 6> found;
            bool all = true;
            for (std::size_t i = 0; i < kSkySides.size() && all; ++i) {
                const auto it = files.find(name + delim + std::string(kSkySides[i]) + ext);
                all = it != files.end();
                if (all)
                    found[i] = it->second;
            }
            if (all)
                return found;
        }
    return std::nullopt;
}

} // namespace

std::vector<fs::path> sky_search_folders(const fs::path& game)
{
    fs::path mod_dir = game;
    if (!mod_dir.has_filename())
        mod_dir = mod_dir.parent_path();
    const fs::path root = mod_dir.parent_path();
    const std::string mod = to_utf8(mod_dir.filename());
    std::string fallback = fallback_dir(mod_dir);
    if (fallback.empty())
        fallback = "valve";

    std::vector<fs::path> folders;
    auto add_game = [&](const std::string& name) {
        for (const char* suffix : {"_addon", "_hd", "", "_downloads"})
            if (const auto dir = child_any_case(root, name + suffix))
                if (std::ranges::find(folders, *dir) == folders.end())
                    folders.push_back(*dir);
    };
    add_game(mod);
    if (lower(fallback) != lower(mod))
        add_game(fallback);
    return folders;
}

std::vector<std::string> list_skyboxes(const fs::path& game)
{
    const std::map<std::string, fs::path> files = env_files(game);
    std::map<std::string, std::string> names;   // lower case -> as the first file spells it
    for (const auto& [key, path] : files) {
        const std::string spelled = to_utf8(path.filename());
        for (const char* ext : {".tga", ".bmp"}) {
            const std::string e = ext;
            if (key.size() <= e.size() + 2 || key.compare(key.size() - e.size(), e.size(), e) != 0)
                continue;
            const std::string stem = key.substr(0, key.size() - e.size());
            for (const std::string_view side : kSkySides) {
                if (!stem.ends_with(side))
                    continue;
                std::string base = stem.substr(0, stem.size() - side.size());
                // "space_rt": Quake's naming of the sky "space", as the engine reads a name ending in "_".
                if (base.ends_with('_') && sky_files(files, base.substr(0, base.size() - 1)))
                    base.pop_back();
                if (!base.empty() && !names.contains(base) && sky_files(files, base))
                    names.emplace(base, spelled.substr(0, base.size()));
            }
        }
    }
    std::vector<std::string> out;
    for (const auto& [key, spelled] : names)
        out.push_back(spelled);
    return out;   // the map kept them sorted by their lower-case names
}

std::array<RgbaImage, 6> load_skybox(const fs::path& game, std::string_view name)
{
    const auto files = sky_files(env_files(game), lower(name));
    if (!files)
        throw std::runtime_error("the sky " + std::string(name) + " does not have all six sides in gfx/env");
    std::array<RgbaImage, 6> sides;
    for (std::size_t i = 0; i < sides.size(); ++i)
        sides[i] = load_image_file((*files)[i]);
    return sides;
}

int sky_face_side(int face) noexcept
{
    // The engine's r_skyTexOrder: the +Y face shows "bk", the -X face "lf".
    static constexpr int order[6] = {0, 2, 1, 3, 4, 5};
    return order[face];
}

SkyPoint sky_point(int face, double s, double t) noexcept
{
    // The engine's st_to_vec: which axis of (s, t, 1) each world axis takes, negated when negative.
    static constexpr int st_to_vec[6][3] = {{3, -1, 2}, {-3, 1, 2}, {1, 3, 2}, {-1, -3, 2}, {-2, -1, 3}, {2, -1, -3}};
    const double b[3] = {s, t, 1.0};
    double v[3];
    for (int j = 0; j < 3; ++j) {
        const int k = st_to_vec[face][j];
        v[j] = k < 0 ? -b[-k - 1] : b[k - 1];
    }
    // Images are stored top row first; the engine flips t for that.
    return {{v[0], v[1], v[2]}, {(s + 1.0) / 2.0, 1.0 - (t + 1.0) / 2.0}};
}

} // namespace gs
