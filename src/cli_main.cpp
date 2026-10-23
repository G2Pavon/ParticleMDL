// particlemdl_cli - export (and optionally compile) a particle system without
// opening the editor.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <cwchar>
#ifndef NOMINMAX
#define NOMINMAX   // or <windows.h> makes min and max macros, which break std::min (Baking.hpp)
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "Compiler.hpp"
#include "Exporter.hpp"
#include "ParticleSystem.hpp"
#include "PathUtil.hpp"
#include "SettingsJson.hpp"
#include "StagedFiles.hpp"

namespace {

namespace fs = std::filesystem;

enum ExitCode { kOk = 0, kError = 1, kUsage = 2, kLimits = 3, kCompileFailed = 4 };

constexpr const char* kUsageText =
    "Usage: particlemdl_cli --settings <file.json> --out <folder> [--compile]\n"
    "\n"
    "  --settings <file>   settings file saved by the editor\n"
    "  --out <folder>      folder for the .qc, .smd and .bmp files (created if missing)\n"
    "  --compile           compile them with the built-in studiomdl and write only\n"
    "                      the .mdl to --out\n"
    "  --help, --version\n"
    "\n"
    "Exit status: 0 done, 1 error, 2 bad arguments, 3 over the engine limits,\n"
    "4 the compiler refused the export.\n";

struct Args {
    std::string settings, out;
    bool help = false, version = false, compile = false;
};

// The arguments as UTF-8. Windows gives main() the ANSI code page, so there they are
// taken from the wide command line instead.
std::vector<std::string> utf8_arguments(int argc, char** argv)
{
#ifdef _WIN32
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide) {
        std::vector<std::string> out;
        for (int i = 0; i < count; ++i) {
            const int length = static_cast<int>(wcslen(wide[i]));
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], length, nullptr, 0, nullptr, nullptr);
            std::string text(static_cast<std::size_t>(bytes), '\0');
            if (bytes > 0)
                WideCharToMultiByte(CP_UTF8, 0, wide[i], length, text.data(), bytes, nullptr, nullptr);
            out.push_back(std::move(text));
        }
        LocalFree(wide);
        return out;
    }
#endif
    return std::vector<std::string>(argv, argv + argc);
}

// "--name value" or "--name=value". Returns false on a problem, with `error` set.
bool parse_args(const std::vector<std::string>& argv, Args& args, std::string& error)
{
    const int argc = static_cast<int>(argv.size());
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[static_cast<std::size_t>(i)];
        if (arg == "--help" || arg == "-h") {
            args.help = true;
            continue;
        }
        if (arg == "--version") {
            args.version = true;
            continue;
        }
        if (arg == "--compile") {
            args.compile = true;
            continue;
        }

        std::optional<std::string> inline_value;
        if (const auto eq = arg.find('='); arg.starts_with("--") && eq != std::string_view::npos) {
            inline_value = std::string(arg.substr(eq + 1));
            arg = arg.substr(0, eq);
        }
        std::string* target = arg == "--settings" ? &args.settings
                              : arg == "--out"    ? &args.out
                                                  : nullptr;
        if (!target) {
            error = "unknown argument '" + std::string(arg) + "'";
            return false;
        }
        if (inline_value) {
            *target = *inline_value;
        } else if (i + 1 < argc) {
            *target = argv[static_cast<std::size_t>(++i)];
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
    const gs::SettingsLoad loaded = gs::read_settings_file(gs::from_utf8(args.settings), gs::Settings{});
    for (const std::string& w : loaded.warnings)
        std::fprintf(stderr, "warning: %s: %s\n", args.settings.c_str(), w.c_str());

    if (const auto problem = gs::model_name_problem(loaded.settings.model_name)) {
        std::fprintf(stderr, "error: %s\n", problem->c_str());
        return kError;
    }

    const gs::ParticleSystem sys(loaded.settings);
    const gs::ExportReport report = gs::analyze_export(sys);
    for (const std::string& w : report.warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());

    // studiomdl would refuse these, so do not write files that cannot compile.
    for (const gs::ExportBlocker& b : report.blockers)
        std::fprintf(stderr, "error: %s\n", b.message.c_str());
    if (!report.blockers.empty())
        return kLimits;

    const fs::path out = gs::from_utf8(args.out);
    fs::create_directories(out);
    const std::string safe = gs::safe_model_name(loaded.settings.model_name);

    if (!args.compile) {
        gs::export_model(sys, out);
        std::printf("Wrote %s.qc, %s_ref.smd, %s_anim.smd and %s.bmp to %s\n", safe.c_str(), safe.c_str(),
                    safe.c_str(), safe.c_str(), gs::to_utf8(out).c_str());
        return kOk;
    }

    gs::CompiledModel model;
    try {
        model = gs::compile_model(gs::export_files(sys));
    } catch (const gs::CompileFailed& e) {
        std::fprintf(stderr, "error: the compiler refused the export: %s\n", e.what());
        return kCompileFailed;
    }
    for (const std::string& w : model.warnings)
        std::fprintf(stderr, "warning: studiomdl: %s\n", w.c_str());
    const fs::path mdl = out / gs::from_utf8(safe + ".mdl");
    gs::write_file_atomically(mdl, {reinterpret_cast<const char*>(model.mdl.data()), model.mdl.size()});
    std::printf("Wrote %s\n", gs::to_utf8(mdl).c_str());
    // Read the model back: what the compiler made, and whether it is what was exported.
    try {
        const gs::MdlInfo info = gs::read_mdl(model.mdl);
        std::printf("  %s\n", gs::describe_mdl(info).c_str());
        for (const std::string& problem : gs::compare_with_export(info, sys))
            std::fprintf(stderr, "warning: %s\n", problem.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "warning: the compiled model could not be checked: %s\n", e.what());
    }
    return kOk;
}

} // namespace

int main(int argc, char** argv)
{
    Args args;
    std::string error;
    if (!parse_args(utf8_arguments(argc, argv), args, error)) {
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
