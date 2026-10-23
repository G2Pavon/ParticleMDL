#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "studiomdl/filesystem.hpp"

namespace studiomdl
{

enum class LogLevel
{
    Info,   // progress and statistics
    Warning // the input was accepted but something was adjusted or ignored
};

// Receives one message per call, without a trailing newline.
using LogCallback = std::function<void(LogLevel level, std::string_view message)>;

struct CompileOptions
{
    bool invert_normals = false;          // CLI -f
    float normal_blend_angle_deg = 2.0f;  // CLI -a <angle>
    bool keep_all_bones = false;          // CLI -b
    LogCallback log;                      // null: compile silently
};

// Thrown for input that cannot be compiled. what() reads
// "<file>:<line>: ERROR: <message>" when the problem is tied to a place in an
// input file (QC or SMD), and "ERROR: <message>" otherwise.
class CompileError : public std::runtime_error
{
public:
    explicit CompileError(std::string message, std::string file = {}, int line = 0)
        : std::runtime_error(describe(message, file, line)), message_(std::move(message)),
          file_(std::move(file)), line_(line)
    {
    }

    const std::string &message() const noexcept { return message_; }
    const std::string &file() const noexcept { return file_; } // empty if not tied to a file
    int line() const noexcept { return line_; }                // 1-based, 0 if unknown

private:
    static std::string describe(const std::string &message, const std::string &file, int line)
    {
        if (file.empty())
            return "ERROR: " + message;
        return file + ":" + std::to_string(line) + ": ERROR: " + message;
    }

    std::string message_;
    std::string file_;
    int line_;
};

struct CompileResult
{
    std::vector<std::uint8_t> mdl; // the complete .mdl file
    std::string file_name;         // <$modelname stem>.mdl, as stored in the header
};

// Compiles the QC at `qc_path`, reading it and everything it references through
// `fs`, and returns the .mdl in memory. Throws CompileError for bad input.
// Never writes to stdout/stderr or terminates the process; diagnostics go to
// options.log, invoked on the calling thread. Calls are independent and may run
// concurrently.
CompileResult compile(std::string_view qc_path, const FileSystem &fs, const CompileOptions &options = {});

// Like compile(), but with the QC script given as text. `qc_path` names it in
// messages, and its directory is the base for the script's relative paths.
CompileResult compile_source(std::string_view qc_text, const FileSystem &fs, const CompileOptions &options = {},
                             std::string_view qc_path = "model.qc");

// Compiles a QC file from disk and writes <$modelname>.mdl next to it.
// Throws CompileError for bad input. Never writes to stdout/stderr or
// terminates the process; diagnostics go to options.log.
//
// Each call is independent: it may be called repeatedly and from several threads
// at once (as long as they write different output files). options.log is invoked
// on the calling thread.
void compile_file(const std::filesystem::path &qc_path, const CompileOptions &options = {});

} // namespace studiomdl
