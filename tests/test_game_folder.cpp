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
    CHECK_FALSE(read_app_config(file).sky);
    write_app_config(file, AppConfig{"/games/Half-Life/cstrike ñ", true, "desert"});   // makes its folder
    const AppConfig back = read_app_config(file);
    CHECK(back.game_folder == "/games/Half-Life/cstrike ñ");
    CHECK(back.sky);
    CHECK(back.sky_name == "desert");


    std::ofstream(file) << "{ not json";
    CHECK(read_app_config(file).game_folder.empty());
    std::ofstream(file) << R"({"game_folder": 42})";
    CHECK(read_app_config(file).game_folder.empty());
    fs::remove_all(dir);
}

TEST_CASE("either separator works in the export folder, on every system")
{
    const fs::path game = from_utf8("/games/Half-Life/cstrike");
    CHECK(normalized_subfolder("fx\\smoke") == "fx/smoke");
    CHECK(normalized_subfolder(" mymodels/ ") == "mymodels");
    CHECK(normalized_subfolder("fx\\smoke\\") == "fx/smoke");
    CHECK(export_folder(game, "fx\\smoke") == export_folder(game, "fx/smoke"));
    CHECK_FALSE(models_subfolder_problem("fx\\smoke"));
    CHECK(models_subfolder_problem("..\\sprites"));
    CHECK(models_subfolder_problem("\\"));   // the root, whichever way it is written
}

TEST_CASE("a pasted folder loses the spaces and quotes around it")
{
    CHECK(clean_folder_text("  \"C:\\Games\\Half-Life\\cstrike\"  ") == "C:\\Games\\Half-Life\\cstrike");
    CHECK(clean_folder_text("/games/hl/valve\n") == "/games/hl/valve");
    CHECK(clean_folder_text("\"") == "\"");   // a lone quote is not a pair
}

#ifdef _WIN32
TEST_CASE("a Windows game folder works with either separator")
{
    CHECK(models_label(from_utf8("C:\\Games\\Half-Life\\cstrike")) == "cstrike/models/");
    CHECK(models_label(from_utf8("C:/Games/Half-Life/cstrike\\")) == "cstrike/models/");
    CHECK(export_folder(from_utf8("C:\\Games\\Half-Life\\cstrike"), "fx") ==
          export_folder(from_utf8("C:/Games/Half-Life/cstrike"), "fx"));
}
#endif

TEST_CASE("the config is kept beside the program, and read from where it was until it is saved there")
{
    // The tests are a program too: their config would be beside them.
    const fs::path folder = executable_folder();
    REQUIRE_FALSE(folder.empty());
    CHECK(fs::is_directory(folder));
    CHECK(app_config_path() == folder / "config.json");
#ifdef __linux__
    CHECK(fs::equivalent(folder, fs::read_symlink("/proc/self/exe").parent_path()));
#endif

    const fs::path dir = fs::temp_directory_path() / "particlemdl_app_config_move";
    fs::remove_all(dir);
    const fs::path beside = dir / "bin" / "config.json", earlier = dir / "home" / "config.json";
    write_app_config(earlier, AppConfig{"/games/Half-Life/valve", true, "city"});
    // Nothing beside the program yet: the earlier file's settings.
    CHECK(read_app_config(beside, earlier).sky_name == "city");
    // Once saved beside the program, that file is the one read.
    write_app_config(beside, AppConfig{"/games/Half-Life/cstrike", false, ""});
    CHECK(read_app_config(beside, earlier).game_folder == "/games/Half-Life/cstrike");
    // No earlier file either: the defaults.
    CHECK(read_app_config(dir / "none" / "config.json", dir / "nothing.json").game_folder.empty());
    fs::remove_all(dir);
}
