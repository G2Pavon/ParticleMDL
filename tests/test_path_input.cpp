#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "NativeDialog.hpp"
#include "PathInput.hpp"

using namespace gs;
namespace fs = std::filesystem;
using Action = DialogInput::Action;

namespace {

// A folder with "sub/", "sub/inner.json" and "a.json".
struct Tree {
    fs::path root = fs::temp_directory_path() / "particlemdl_path_test";

    Tree()
    {
        fs::remove_all(root);
        fs::create_directories(root / "sub");
        std::ofstream(root / "a.json") << "{}";
        std::ofstream(root / "sub" / "inner.json") << "{}";
    }
    ~Tree() { fs::remove_all(root); }
};

} // namespace

TEST_CASE("empty input does nothing")
{
    const Tree t;
    CHECK(resolve_dialog_input(t.root, "", false, ".json").action == Action::None);
    CHECK(resolve_dialog_input(t.root, "   ", true, ".json").action == Action::None);
}

TEST_CASE("a folder name navigates into the folder")
{
    const Tree t;
    for (const char* typed : {"sub", "sub/", "./sub", " sub "}) {
        CAPTURE(typed);
        const DialogInput in = resolve_dialog_input(t.root, typed, false, ".json");
        CHECK(in.action == Action::Navigate);
        CHECK(in.path == t.root / "sub");
    }
}

TEST_CASE("'..' navigates to the parent")
{
    const Tree t;
    const DialogInput in = resolve_dialog_input(t.root / "sub", "..", false, ".json");
    CHECK(in.action == Action::Navigate);
    CHECK(in.path == t.root);
}

TEST_CASE("an absolute path ignores the current folder")
{
    const Tree t;
    const DialogInput in = resolve_dialog_input(t.root / "sub", (t.root / "a.json").string(), false, ".json");
    CHECK(in.action == Action::Choose);
    CHECK(in.path == t.root / "a.json");
}

TEST_CASE("opening needs an existing file")
{
    const Tree t;
    const DialogInput found = resolve_dialog_input(t.root, "a.json", false, ".json");
    CHECK(found.action == Action::Choose);
    CHECK(found.path == t.root / "a.json");

    const DialogInput inner = resolve_dialog_input(t.root, "sub/inner.json", false, ".json");
    CHECK(inner.action == Action::Choose);

    const DialogInput missing = resolve_dialog_input(t.root, "nope.json", false, ".json");
    CHECK(missing.action == Action::Error);
    CHECK(missing.message.find("File not found") != std::string::npos);

    const DialogInput no_folder = resolve_dialog_input(t.root, "ghost/x.json", false, ".json");
    CHECK(no_folder.action == Action::Error);
    CHECK(no_folder.message.find("Folder does not exist") != std::string::npos);
}

TEST_CASE("opening does not add the extension")
{
    const Tree t;
    CHECK(resolve_dialog_input(t.root, "a", false, ".json").action == Action::Error);
}

TEST_CASE("saving a new name adds the extension when it is missing")
{
    const Tree t;
    const DialogInput plain = resolve_dialog_input(t.root, "fresh", true, ".json");
    CHECK(plain.action == Action::Choose);
    CHECK(plain.path == t.root / "fresh.json");

    const DialogInput with_ext = resolve_dialog_input(t.root, "fresh.json", true, ".json");
    CHECK(with_ext.action == Action::Choose);
    CHECK(with_ext.path == t.root / "fresh.json");

    const DialogInput none_wanted = resolve_dialog_input(t.root, "fresh", true, "");
    CHECK(none_wanted.path == t.root / "fresh");
}

TEST_CASE("saving over an existing file asks first")
{
    const Tree t;
    const DialogInput in = resolve_dialog_input(t.root, "a", true, ".json");
    CHECK(in.action == Action::Confirm);
    CHECK(in.path == t.root / "a.json");

    CHECK(resolve_dialog_input(t.root, "a.json", true, ".json").action == Action::Confirm);
}

TEST_CASE("saving into a folder that does not exist is an error")
{
    const Tree t;
    const DialogInput in = resolve_dialog_input(t.root, "ghost/new.json", true, ".json");
    CHECK(in.action == Action::Error);
    CHECK(in.message.find("Folder does not exist") != std::string::npos);
}

#ifndef _WIN32
TEST_CASE("~ stands for the home folder")
{
    const Tree t;
    const char* old = std::getenv("HOME");
    const std::string saved = old ? old : "";
    setenv("HOME", t.root.c_str(), 1);

    const DialogInput home = resolve_dialog_input("/", "~", false, ".json");
    CHECK(home.action == Action::Navigate);
    CHECK(home.path == t.root);

    const DialogInput file = resolve_dialog_input("/", "~/a.json", false, ".json");
    CHECK(file.action == Action::Choose);
    CHECK(file.path == t.root / "a.json");

    if (old)
        setenv("HOME", saved.c_str(), 1);
    else
        unsetenv("HOME");
}
#endif

TEST_CASE("only a path the shell reads nothing into starts a native folder dialog")
{
    CHECK(native::shell_safe_path("/home/me/Half-Life/cstrike/models"));
    CHECK(native::shell_safe_path("/home/me/juegos ñ/Half-Life (Steam)/valve/models"));
    CHECK_FALSE(native::shell_safe_path("/home/me/$(rm -rf ~)/models"));
    CHECK_FALSE(native::shell_safe_path("/home/me/`id`/models"));
    CHECK_FALSE(native::shell_safe_path("/home/me/\"quoted\"/models"));
    CHECK_FALSE(native::shell_safe_path("/home/me/it's/models"));
    CHECK_FALSE(native::shell_safe_path("/home/me/a\nb"));
#ifdef _WIN32
    CHECK(native::shell_safe_path("C:\\Games\\Half-Life\\cstrike\\models"));
#else
    CHECK_FALSE(native::shell_safe_path("/home/me/back\\slash"));
#endif
}
