#pragma once

#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>

namespace gs {

struct CompileOptions {
    // Folder holding the exported files; studiomdl writes the .mdl there.
    std::filesystem::path work_dir = ".";
    std::chrono::milliseconds timeout = std::chrono::minutes(2);
    // Requesting a stop kills the compiler (checked about every 100 ms).
    std::stop_token stop;
};

struct CompileResult {
    enum class Status { Ok, CompilerNotFound, Failed, TimedOut, Cancelled };
    Status status = Status::Failed;
    std::string output;   // compiler stdout + stderr
};

// Runs `<compiler> <safe_name>.qc` inside options.work_dir. A compiler given as
// a path ("./studiomdl") is resolved against the caller's current directory.
// Success means exit code 0 and <safe_name>.mdl present; the intermediate
// files are then deleted. Throws std::runtime_error when the process cannot be
// set up at all.
CompileResult compile_model(const std::string& compiler, const std::string& safe_name,
                            const CompileOptions& options = {});

} // namespace gs
