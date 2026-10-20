#pragma once

#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>

namespace gs {

struct CompileOptions {
    // Folder holding the exported files; studiomdl writes the .mdl there.
    std::filesystem::path work_dir = ".";
    std::chrono::milliseconds timeout = std::chrono::minutes(2);
    // Keep the .qc, .smd and .bmp after a successful compile, to edit and compile again.
    bool keep_sources = false;
    // Requesting a stop kills the compiler (checked about every 100 ms).
    std::stop_token stop;
};

struct CompileResult {
    enum class Status { Ok, CompilerNotFound, Failed, TimedOut, Cancelled };
    Status status = Status::Failed;
    std::string output;   // compiler stdout + stderr
    bool exit_zero = false;   // the compiler ended with status 0 (compile_model also wants the .mdl)
};

// One command-line argument as Windows programs read it back (CommandLineToArgvW
// and the C runtime): quoted when it holds spaces or quotes, with the
// backslashes before a quote doubled.
std::wstring quote_windows_argument(std::wstring_view arg);

// Runs `<compiler> <safe_name>.qc` inside options.work_dir. A compiler given as
// a path ("./studiomdl") is resolved against the caller's current directory.
// The compiler's output is collected in CompileResult::output on every platform;
// it runs without a console window, with an empty stdin, and is stopped on
// timeout or cancel together with anything it started.
// Success means exit code 0 and a <safe_name>.mdl written by this run (an older
// one does not count); the intermediate files are then deleted unless
// options.keep_sources is set. Throws std::runtime_error when the process cannot be
// set up at all.
CompileResult compile_model(const std::string& compiler, const std::string& safe_name,
                            const CompileOptions& options = {});

} // namespace gs
