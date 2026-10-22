#pragma once

#include <chrono>
#include <cstddef>
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
    // Requesting a stop kills the compiler (checked every 20 to 100 ms).
    std::stop_token stop;
    // Past this much output only its start and its end are kept, with a note of what was left out.
    std::size_t max_output_bytes = 2u << 20;
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

// What to tell the user when the compiler was stopped for running past `timeout`. A hung
// compiler usually prints nothing (its output is buffered until it exits), so the message
// names the one known cause: studiomdl++ hangs on some looping sequences.
std::string timeout_message(std::chrono::milliseconds timeout, bool looping);

// The reason a compile failed, from the compiler's output: the line after studiomdl's
// "************ ERROR ************", else the last line with text; empty for no output.
std::string compiler_error(std::string_view output);

// The last `lines` lines of `text`, for showing the end of a long compiler output.
std::string last_lines(std::string_view text, std::size_t lines);

} // namespace gs
