// particlemdl_cli - export (and optionally compile) a particle system without
// opening the editor.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "Compiler.hpp"
#include "Exporter.hpp"
#include "ParticleSystem.hpp"
#include "SettingsJson.hpp"

namespace {

namespace fs = std::filesystem;

enum ExitCode { kOk = 0, kError = 1, kUsage = 2, kLimits = 3, kCompileFailed = 4 };

constexpr const char* kUsageText =
    "Usage: particlemdl_cli --settings <file.json> --out <folder> [--compile <studiomdl>]\n"
    "\n"
    "  --settings <file>   settings file saved by the editor\n"
    "  --out <folder>      folder for the .qc, .smd and .bmp files (created if missing)\n"
    "  --compile <path>    run this studiomdl on the .qc afterwards; the .mdl is\n"
    "                      written to --out and the intermediate files are removed\n"
    "  --help, --version\n"
    "\n"
    "Exit status: 0 done, 1 error, 2 bad arguments, 3 over the engine limits,\n"
    "4 the compiler failed.\n";

struct Args {
    std::string settings, out, compiler;
    bool help = false, version = false;
};

// "--name value" or "--name=value". Returns false on a problem, with `error` set.
bool parse_args(int argc, char** argv, Args& args, std::string& error)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            args.help = true;
            continue;
        }
        if (arg == "--version") {
            args.version = true;
            continue;
        }

        std::optional<std::string> inline_value;
        if (const auto eq = arg.find('='); arg.starts_with("--") && eq != std::string_view::npos) {
            inline_value = std::string(arg.substr(eq + 1));
            arg = arg.substr(0, eq);
        }
        std::string* target = arg == "--settings" ? &args.settings
                              : arg == "--out"    ? &args.out
                              : arg == "--compile" ? &args.compiler
                                                   : nullptr;
        if (!target) {
            error = "unknown argument '" + std::string(arg) + "'";
            return false;
        }
        if (inline_value) {
            *target = *inline_value;
        } else if (i + 1 < argc) {
            *target = argv[++i];
        } else {
            error = std::string(arg) + " needs a value";
            return false;
        }
        if (target->empty()) {
            error = std::string(arg) + " must not be empty";
            return false;
        }
    }
    return true;
}

int run(const Args& args)
{
    const gs::SettingsLoad loaded = gs::read_settings_file(args.settings, gs::Settings{});
    for (const std::string& w : loaded.warnings)
        std::fprintf(stderr, "warning: %s: %s\n", args.settings.c_str(), w.c_str());

    if (const auto problem = gs::model_name_problem(loaded.settings.model_name)) {
        std::fprintf(stderr, "error: %s\n", problem->c_str());
        return kError;
    }

    const gs::ParticleSystem sys(loaded.settings);
    for (const std::string& w : gs::export_warnings(sys))
        std::fprintf(stderr, "warning: %s\n", w.c_str());

    // studiomdl would refuse these, so do not write files that cannot compile.
    if (sys.export_frames() > gs::kMaxStudioFrames) {
        std::fprintf(stderr, "error: %d frames; studiomdl accepts at most %d per sequence\n", sys.export_frames(),
                     gs::kMaxStudioFrames);
        return kLimits;
    }
    if (const std::size_t bytes = gs::sequence_bytes(sys); bytes > gs::kMaxSequenceBytes) {
        std::fprintf(stderr, "error: the animation needs %zu bytes; the limit is %zu per sequence\n", bytes,
                     gs::kMaxSequenceBytes);
        return kLimits;
    }

    const fs::path out = args.out;
    fs::create_directories(out);
    gs::export_model(sys, out);
    const std::string safe = gs::safe_model_name(loaded.settings.model_name);

    if (args.compiler.empty()) {
        std::printf("Wrote %s.qc, %s_ref.smd, %s_anim.smd and %s.bmp to %s\n", safe.c_str(), safe.c_str(),
                    safe.c_str(), safe.c_str(), out.string().c_str());
        return kOk;
    }

    gs::CompileOptions options;
    options.work_dir = out;
    const gs::CompileResult result = gs::compile_model(args.compiler, safe, options);
    using Status = gs::CompileResult::Status;
    switch (result.status) {
    case Status::Ok:
        std::printf("Wrote %s\n", (out / (safe + ".mdl")).string().c_str());
        return kOk;
    case Status::CompilerNotFound:
        std::fprintf(stderr, "error: compiler not found: '%s'\n", args.compiler.c_str());
        break;
    case Status::TimedOut:
        std::fprintf(stderr, "error: the compiler timed out\n");
        break;
    case Status::Cancelled:
        std::fprintf(stderr, "error: the compile was cancelled\n");
        break;
    case Status::Failed:
        std::fprintf(stderr, "error: the compiler failed\n");
        break;
    }
    if (!result.output.empty())
        std::fprintf(stderr, "%s\n", result.output.c_str());
    return kCompileFailed;
}

} // namespace

int main(int argc, char** argv)
{
    Args args;
    std::string error;
    if (!parse_args(argc, argv, args, error)) {
        std::fprintf(stderr, "error: %s\n\n%s", error.c_str(), kUsageText);
        return kUsage;
    }
    if (args.help) {
        std::fputs(kUsageText, stdout);
        return kOk;
    }
    if (args.version) {
        std::printf("particlemdl_cli %s\n", GS_VERSION);
        return kOk;
    }
    if (args.settings.empty() || args.out.empty()) {
        std::fprintf(stderr, "error: --settings and --out are required\n\n%s", kUsageText);
        return kUsage;
    }

    try {
        return run(args);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return kError;
    }
}
