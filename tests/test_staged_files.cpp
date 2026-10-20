#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "StagedFiles.hpp"

using namespace gs;
namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path path;

    explicit Dir(const char* name) : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Dir() { fs::remove_all(path); }

    void write(const std::string& name, const std::string& text) const { std::ofstream(path / name, std::ios::binary) << text; }

    // Every file in the folder with its content.
    std::map<std::string, std::string> contents() const
    {
        std::map<std::string, std::string> all;
        for (const auto& e : fs::directory_iterator(path)) {
            if (e.is_directory()) {
                all[e.path().filename().string()] = "<folder>";
                continue;
            }
            std::ifstream in(e.path(), std::ios::binary);
            all[e.path().filename().string()] = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        return all;
    }
};

using Files = std::map<std::string, std::string>;

void add(StagedFiles& staged, const char* name, const std::string& text)
{
    staged.add(name, {text.data(), text.size()});
}

} // namespace

TEST_CASE("committing puts every new file in place and leaves nothing else")
{
    const Dir dir("particlemdl_staged_new");
    StagedFiles staged(dir.path);
    add(staged, "a.txt", "one");
    add(staged, "b.txt", "two");
    CHECK(dir.contents().size() == 2);   // the two temporary files
    staged.commit();
    CHECK(dir.contents() == Files{{"a.txt", "one"}, {"b.txt", "two"}});
}

TEST_CASE("committing replaces existing files and removes the backups")
{
    const Dir dir("particlemdl_staged_replace");
    dir.write("a.txt", "old a");
    dir.write("c.txt", "untouched");
    StagedFiles staged(dir.path);
    add(staged, "a.txt", "new a");
    add(staged, "b.txt", "new b");
    staged.commit();
    CHECK(dir.contents() == Files{{"a.txt", "new a"}, {"b.txt", "new b"}, {"c.txt", "untouched"}});
}

TEST_CASE("staged files that are never committed disappear")
{
    const Dir dir("particlemdl_staged_dropped");
    dir.write("a.txt", "old");
    {
        StagedFiles staged(dir.path);
        add(staged, "a.txt", "new");
        add(staged, "b.txt", "new");
    }
    CHECK(dir.contents() == Files{{"a.txt", "old"}});
}

TEST_CASE("a write that fails leaves the folder as it was")
{
    const Dir dir("particlemdl_staged_nowrite");
    dir.write("a.txt", "old");
    {
        StagedFiles staged(dir.path);
        add(staged, "a.txt", "new");
        CHECK_THROWS_AS(add(staged, "missing/b.txt", "new"), std::runtime_error);
    }
    CHECK(dir.contents() == Files{{"a.txt", "old"}});
}

TEST_CASE("a target that is not a regular file stops the commit before anything changes")
{
    const Dir dir("particlemdl_staged_notfile");
    dir.write("a.txt", "old");
    fs::create_directory(dir.path / "b.txt");
    {
        StagedFiles staged(dir.path);
        add(staged, "a.txt", "new");
        add(staged, "b.txt", "new");
        CHECK_THROWS_AS(staged.commit(), std::runtime_error);
    }
    CHECK(dir.contents().at("a.txt") == "old");
    CHECK(fs::is_directory(dir.path / "b.txt"));
    CHECK(dir.contents().size() == 2);
}

TEST_CASE("whichever rename fails, the old files are back and nothing else is left")
{
    // Three files: two already there, one new. Fail the Nth rename of the commit, for every N.
    for (int fail_at = 1; fail_at <= 6; ++fail_at) {
        CAPTURE(fail_at);
        const Dir dir("particlemdl_staged_rollback");
        dir.write("a.txt", "old a");
        dir.write("b.txt", "old b");
        dir.write("keep.txt", "keep");
        const Files before = dir.contents();

        int calls = 0;
        bool threw = false;
        {
            StagedFiles staged(dir.path, [&](const fs::path& from, const fs::path& to, std::error_code& ec) {
                if (++calls == fail_at)
                    ec = std::make_error_code(std::errc::permission_denied);
                else
                    fs::rename(from, to, ec);
            });
            add(staged, "a.txt", "new a");
            add(staged, "b.txt", "new b");
            add(staged, "c.txt", "new c");
            try {
                staged.commit();
            } catch (const std::runtime_error& e) {
                threw = true;
                CHECK(std::string(e.what()).find("previous files were kept") != std::string::npos);
            }
        }
        // 2 renames for each of the two existing files and 1 for the new one: 5 in all.
        if (fail_at <= 5) {
            CHECK(threw);
            CHECK(dir.contents() == before);
        } else {
            CHECK_FALSE(threw);
            CHECK(dir.contents() == Files{{"a.txt", "new a"}, {"b.txt", "new b"}, {"c.txt", "new c"}, {"keep.txt", "keep"}});
        }
    }
}

TEST_CASE("a backup is kept if the old file cannot be put back")
{
    const Dir dir("particlemdl_staged_stuck");
    dir.write("a.txt", "old a");
    int calls = 0;
    {
        StagedFiles staged(dir.path, [&](const fs::path& from, const fs::path& to, std::error_code& ec) {
            ++calls;
            if (calls >= 2)   // moving the new file in fails, and so does moving the old one back
                ec = std::make_error_code(std::errc::permission_denied);
            else
                fs::rename(from, to, ec);
        });
        add(staged, "a.txt", "new a");
        CHECK_THROWS_AS(staged.commit(), std::runtime_error);
    }
    const Files left = dir.contents();
    REQUIRE(left.count("a.txt.bak") == 1);
    CHECK(left.at("a.txt.bak") == "old a");   // the only copy of the old file survives
    CHECK(left.count("a.txt.tmp") == 0);
}

TEST_CASE("write_file_atomically creates and replaces a file")
{
    const Dir dir("particlemdl_staged_single");
    write_file_atomically(dir.path / "s.json", "first");
    CHECK(dir.contents() == Files{{"s.json", "first"}});
    write_file_atomically(dir.path / "s.json", "second, longer");
    CHECK(dir.contents() == Files{{"s.json", "second, longer"}});
}

TEST_CASE("write_file_atomically keeps the old file when it cannot write")
{
    const Dir dir("particlemdl_staged_single_fail");
    dir.write("s.json", "precious");
    CHECK_THROWS_AS(write_file_atomically(dir.path / "missing" / "s.json", "x"), std::runtime_error);
    CHECK(dir.contents() == Files{{"s.json", "precious"}});

    // A folder where the file should go.
    fs::create_directory(dir.path / "d.json");
    CHECK_THROWS_AS(write_file_atomically(dir.path / "d.json", "x"), std::runtime_error);
    CHECK(fs::is_directory(dir.path / "d.json"));
    CHECK(dir.contents().count("d.json.tmp") == 0);
}

TEST_CASE("write_file_atomically accepts a bare file name")
{
    const Dir dir("particlemdl_staged_bare");
    const fs::path old = fs::current_path();
    fs::current_path(dir.path);
    write_file_atomically("bare.json", "text");
    fs::current_path(old);
    CHECK(dir.contents() == Files{{"bare.json", "text"}});
}
