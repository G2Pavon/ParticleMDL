#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "Compiler.hpp"
#include "PathUtil.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace gs;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Status = CompileResult::Status;

namespace {

// The test executable is the fake compiler; see fake_studiomdl.hpp.
std::string self_path;

struct Folder {
    fs::path path;

    explicit Folder(const char* name) : path(fs::temp_directory_path() / from_utf8(name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Folder() { fs::remove_all(path); }

    // Writes "<name>.qc" whose first line is the behavior.
    void qc(const std::string& name, const std::string& mode) const { std::ofstream(path / (name + ".qc")) << mode << '\n'; }
    bool has(const std::string& file) const { return fs::exists(path / file); }
};

std::string trimmed(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

CompileOptions in(const Folder& f)
{
    CompileOptions o;
    o.work_dir = f.path;
    return o;
}

} // namespace

// main.cpp sets this before the tests run.
void set_self_path(const std::string& path)
{
    self_path = fs::absolute(path).string();
}

TEST_CASE("quote_windows_argument leaves plain arguments alone")
{
    CHECK(quote_windows_argument(L"studiomdl.exe") == L"studiomdl.exe");
    CHECK(quote_windows_argument(L"C:\\tools\\studiomdl.exe") == L"C:\\tools\\studiomdl.exe");
    CHECK(quote_windows_argument(L"model.qc") == L"model.qc");
}

TEST_CASE("quote_windows_argument quotes what needs it")
{
    CHECK(quote_windows_argument(L"") == L"\"\"");
    CHECK(quote_windows_argument(L"a b") == L"\"a b\"");
    CHECK(quote_windows_argument(L"a\tb") == L"\"a\tb\"");
    CHECK(quote_windows_argument(L"C:\\Program Files\\x.exe") == L"\"C:\\Program Files\\x.exe\"");
}

TEST_CASE("quote_windows_argument escapes quotes and the backslashes before them")
{
    CHECK(quote_windows_argument(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
    CHECK(quote_windows_argument(L"x\"y") == L"\"x\\\"y\"");
    // A trailing backslash must not escape the closing quote.
    CHECK(quote_windows_argument(L"C:\\my dir\\") == L"\"C:\\my dir\\\\\"");
    // Backslashes before a quote are doubled, then the quote escaped.
    CHECK(quote_windows_argument(L"a\\\"b") == L"\"a\\\\\\\"b\"");
    CHECK(quote_windows_argument(L"a\\\\\"b") == L"\"a\\\\\\\\\\\"b\"");
    // Backslashes elsewhere stay as they are.
    CHECK(quote_windows_argument(L"a\\b c") == L"\"a\\b c\"");
}

TEST_CASE("compile_model reports success and removes the intermediate files")
{
    const Folder dir("particlemdl_compile_ok");
    dir.qc("fx", "ok");
    for (const char* f : {"fx_ref.smd", "fx_anim.smd", "fx.bmp"})
        std::ofstream(dir.path / f) << "x";

    const CompileResult r = compile_model(self_path, "fx", in(dir));
    CHECK(r.status == Status::Ok);
    CHECK(dir.has("fx.mdl"));
    CHECK_FALSE(dir.has("fx.qc"));
    CHECK_FALSE(dir.has("fx_ref.smd"));
    CHECK_FALSE(dir.has("fx_anim.smd"));
    CHECK_FALSE(dir.has("fx.bmp"));
}

TEST_CASE("compile_model collects stdout and stderr together")
{
    const Folder dir("particlemdl_compile_output");
    dir.qc("fx", "ok");
    const CompileResult r = compile_model(self_path, "fx", in(dir));
    CHECK(r.output.find("compiled fx.qc") != std::string::npos);
    CHECK(r.output.find("a note on stderr") != std::string::npos);
}

TEST_CASE("compile_model reports a compiler that fails and keeps the files")
{
    const Folder dir("particlemdl_compile_fail");
    dir.qc("fx", "fail");
    const CompileResult r = compile_model(self_path, "fx", in(dir));
    CHECK(r.status == Status::Failed);
    CHECK(r.output.find("ERROR: bad qc") != std::string::npos);
    CHECK(dir.has("fx.qc"));
    CHECK_FALSE(dir.has("fx.mdl"));
}

TEST_CASE("compile_model needs the .mdl, not just exit status 0")
{
    const Folder dir("particlemdl_compile_nomdl");
    dir.qc("fx", "nomdl");
    CHECK(compile_model(self_path, "fx", in(dir)).status == Status::Failed);
    CHECK(dir.has("fx.qc"));
}

TEST_CASE("compile_model runs the compiler inside the work folder")
{
    const Folder dir("particlemdl_compile_cwd");
    dir.qc("fx", "cwd");
    const CompileResult r = compile_model(self_path, "fx", in(dir));
    REQUIRE(r.status == Status::Failed);   // it writes no .mdl; the output is what matters
    std::error_code ec;
    CHECK(fs::equivalent(fs::path(trimmed(r.output)), dir.path, ec));
}

TEST_CASE("compile_model works in a folder other than the current one, with spaces in its name")
{
    const Folder dir("particlemdl compile spaced dir");
    dir.qc("fx", "ok");
    CHECK(fs::current_path() != dir.path);
    CHECK(compile_model(self_path, "fx", in(dir)).status == Status::Ok);
}

TEST_CASE("compile_model starts a compiler whose path has spaces")
{
    const Folder tools("particlemdl compile my tools");
    const fs::path copy = tools.path / fs::path(self_path).filename();
    fs::copy_file(self_path, copy);

    const Folder dir("particlemdl_compile_spaced_tool");
    dir.qc("fx", "ok");
    CHECK(compile_model(copy.string(), "fx", in(dir)).status == Status::Ok);
}

TEST_CASE("compile_model reports a compiler that does not exist")
{
    const Folder dir("particlemdl_compile_missing");
    dir.qc("fx", "ok");
    CHECK(compile_model("no-such-compiler-particlemdl", "fx", in(dir)).status == Status::CompilerNotFound);
    CHECK(compile_model((dir.path / "missing" / "studiomdl").string(), "fx", in(dir)).status == Status::CompilerNotFound);
    CHECK(dir.has("fx.qc"));
}

TEST_CASE("compile_model gives the compiler an empty stdin")
{
    const Folder dir("particlemdl_compile_stdin");
    dir.qc("fx", "stdin");
    CompileOptions options = in(dir);
    options.timeout = 8s;   // a compiler left waiting on our stdin is reported as timed out

#ifndef _WIN32
    // Make our own stdin a pipe that never ends, which a compiler that inherited it would wait on.
    int pipe_fds[2];
    REQUIRE(pipe(pipe_fds) == 0);
    const int saved_stdin = dup(STDIN_FILENO);
    dup2(pipe_fds[0], STDIN_FILENO);
#endif
    const CompileResult r = compile_model(self_path, "fx", options);
#ifndef _WIN32
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
#endif

    CHECK(r.status != Status::TimedOut);
    CHECK(r.output == "stdin: 0 bytes");
}

TEST_CASE("compile_model reads a lot of output without stalling")
{
    const Folder dir("particlemdl_compile_lots");
    dir.qc("fx", "lots");
    const CompileResult r = compile_model(self_path, "fx", in(dir));
    CHECK(r.output.size() > 1500000);
    CHECK(r.output.find("line 19999:") != std::string::npos);
}

TEST_CASE("compile_model kills a compiler that runs past the time limit")
{
    const Folder dir("particlemdl_compile_timeout");
    dir.qc("fx", "sleep");
    CompileOptions options = in(dir);
    options.timeout = 400ms;

    const auto start = std::chrono::steady_clock::now();
    const CompileResult r = compile_model(self_path, "fx", options);
    const auto took = std::chrono::steady_clock::now() - start;

    CHECK(r.status == Status::TimedOut);
    CHECK(r.output.find("started") != std::string::npos);   // what it had written is kept
    CHECK(took < 10s);
    CHECK_FALSE(dir.has("fx.mdl"));
}

TEST_CASE("compile_model stops a compiler when asked to")
{
    const Folder dir("particlemdl_compile_cancel");
    dir.qc("fx", "sleep");
    std::stop_source source;
    CompileOptions options = in(dir);
    options.stop = source.get_token();

    std::thread canceller([&] {
        std::this_thread::sleep_for(300ms);
        source.request_stop();
    });
    const auto start = std::chrono::steady_clock::now();
    const CompileResult r = compile_model(self_path, "fx", options);
    const auto took = std::chrono::steady_clock::now() - start;
    canceller.join();

    CHECK(r.status == Status::Cancelled);
    CHECK(took < 10s);
}

// "Jos" + e-acute and "tools" with an o-circumflex, as UTF-8 bytes.
TEST_CASE("compile_model works in a folder with non-ASCII characters in its name")
{
    const Folder dir(("particlemdl_compile_Jos\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC"));
    dir.qc("fx", "ok");
    CHECK(compile_model(self_path, "fx", in(dir)).status == Status::Ok);
    CHECK(dir.has("fx.mdl"));
}

TEST_CASE("compile_model starts a compiler whose path has non-ASCII characters")
{
    const Folder tools("particlemdl_t\xC3\xB4ols_\xE6\x97\xA5\xE6\x9C\xAC");
    const fs::path copy = tools.path / fs::path(self_path).filename();
    fs::copy_file(self_path, copy);

    const Folder dir("particlemdl_compile_non_ascii_tool");
    dir.qc("fx", "ok");
    // The compiler is given as UTF-8 text, the way the editor keeps it.
    CHECK(compile_model(to_utf8(copy), "fx", in(dir)).status == Status::Ok);
}

TEST_CASE("compile_model does not take an old .mdl for the result of a compile that wrote none")
{
    const Folder dir("particlemdl_compile_stale");
    dir.qc("fx", "nomdl");
    const fs::path mdl = dir.path / "fx.mdl";
    std::ofstream(mdl) << "old model";
    fs::last_write_time(mdl, fs::file_time_type::clock::now() - 1h);

    const CompileResult r = compile_model(self_path, "fx", in(dir));
    CHECK(r.status == Status::Failed);
    CHECK(dir.has("fx.mdl"));   // the old model is left alone
    CHECK(dir.has("fx.qc"));
}

TEST_CASE("compile_model accepts a .mdl written over an old one")
{
    const Folder dir("particlemdl_compile_replace");
    dir.qc("fx", "ok");
    const fs::path mdl = dir.path / "fx.mdl";
    std::ofstream(mdl) << "old model with more text";
    fs::last_write_time(mdl, fs::file_time_type::clock::now() - 1h);

    CHECK(compile_model(self_path, "fx", in(dir)).status == Status::Ok);
    std::ifstream in_file(mdl);
    std::string text;
    std::getline(in_file, text);
    CHECK(text == "IDST");
}

TEST_CASE("compile_model keeps the source files when asked to")
{
    const Folder dir("particlemdl_compile_keep");
    dir.qc("fx", "ok");
    for (const char* f : {"fx_ref.smd", "fx_anim.smd", "fx.bmp"})
        std::ofstream(dir.path / f) << "x";
    CompileOptions options = in(dir);
    options.keep_sources = true;

    CHECK(compile_model(self_path, "fx", options).status == Status::Ok);
    CHECK(dir.has("fx.mdl"));
    for (const char* f : {"fx.qc", "fx_ref.smd", "fx_anim.smd", "fx.bmp"})
        CHECK(dir.has(f));
}

TEST_CASE("a timeout names the hang of studiomdl++ for looping sequences only")
{
    const std::string looping = timeout_message(std::chrono::minutes(2), true);
    CHECK(looping.find("within 120 s") != std::string::npos);
    CHECK(looping.find("studiomdl++") != std::string::npos);
    CHECK(looping.find("Loop") != std::string::npos);

    const std::string once = timeout_message(std::chrono::seconds(30), false);
    CHECK(once.find("within 30 s") != std::string::npos);
    CHECK(once.find("studiomdl++") == std::string::npos);
}
