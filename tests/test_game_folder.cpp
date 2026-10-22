#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "GameFolder.hpp"
#include "PathUtil.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

// A mod folder with a models/ folder, removed at the end.
struct Game {
    fs::path root = fs::temp_directory_path() / "particlemdl_game_folder";
    fs::path mod = root / "cstrike";

    Game()
    {
        fs::remove_all(root);
        fs::create_directories(mod / "models" / "mymodels");
    }
    ~Game() { fs::remove_all(root); }
};

} // namespace

TEST_CASE("a game folder needs to exist and to have a models folder")
{
    const Game g;
    CHECK_FALSE(game_folder_problem(g.mod));
    CHECK(game_folder_problem({})->find("Settings > Game Folder") != std::string::npos);
    CHECK(game_folder_problem(g.root / "nothing")->find("does not exist") != std::string::npos);
    // Half-Life itself rather than one of its mod folders.
    CHECK(game_folder_problem(g.root)->find("Half-Life/valve") != std::string::npos);
}

TEST_CASE("the export folder is models, or a folder inside it")
{
    const fs::path game = from_utf8("/games/Half-Life/cstrike");
    CHECK(export_folder(game, "") == from_utf8("/games/Half-Life/cstrike/models"));
    CHECK(export_folder(game, "mymodels") == from_utf8("/games/Half-Life/cstrike/models/mymodels"));
    CHECK(export_folder(game, "fx/smoke") == from_utf8("/games/Half-Life/cstrike/models/fx/smoke"));

    CHECK_FALSE(models_subfolder_problem(""));
    CHECK_FALSE(models_subfolder_problem("mymodels"));
    CHECK_FALSE(models_subfolder_problem("fx/smoke"));
    CHECK_FALSE(models_subfolder_problem("efectos ñ"));
    CHECK(models_subfolder_problem("/etc"));
    CHECK(models_subfolder_problem("../sprites"));
    CHECK(models_subfolder_problem("fx/../../maps"));
    CHECK(models_subfolder_problem("a:b"));
#ifdef _WIN32
    CHECK(models_subfolder_problem("C:\\models"));
#endif
}

TEST_CASE("the models label is named after the mod folder")
{
    CHECK(models_label(from_utf8("/games/Half-Life/cstrike")) == "cstrike/models/");
    CHECK(models_label(from_utf8("/games/Half-Life/valve/")) == "valve/models/");
    CHECK(models_label({}) == "<game>/models/");
}

TEST_CASE("a folder chosen inside models becomes the export folder, one outside does not")
{
    const Game g;
    CHECK(subfolder_within_models(g.mod, g.mod / "models") == std::optional<std::string>(""));
    CHECK(subfolder_within_models(g.mod, g.mod / "models" / "mymodels") == std::optional<std::string>("mymodels"));
    CHECK(subfolder_within_models(g.mod, g.mod / "models" / "mymodels" / "new") ==
          std::optional<std::string>("mymodels/new"));
    CHECK_FALSE(subfolder_within_models(g.mod, g.mod));
    CHECK_FALSE(subfolder_within_models(g.mod, g.root));
    CHECK_FALSE(subfolder_within_models(g.mod, g.mod / "maps"));
}

TEST_CASE("the app config keeps the game folder, and a missing or broken file gives the defaults")
{
    const fs::path dir = fs::temp_directory_path() / "particlemdl_app_config";
    fs::remove_all(dir);
    const fs::path file = dir / "sub" / "config.json";

    CHECK(read_app_config(file).game_folder.empty());
    write_app_config(file, AppConfig{"/games/Half-Life/cstrike ñ"});   // makes its folder
    CHECK(read_app_config(file).game_folder == "/games/Half-Life/cstrike ñ");

    std::ofstream(file) << "{ not json";
    CHECK(read_app_config(file).game_folder.empty());
    std::ofstream(file) << R"({"game_folder": 42})";
    CHECK(read_app_config(file).game_folder.empty());
    fs::remove_all(dir);
}
