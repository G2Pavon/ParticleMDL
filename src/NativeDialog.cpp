#include "NativeDialog.hpp"

#include <chrono>
#include <cstdlib>
#include <string_view>

#include "tinyfiledialogs.h"

namespace gs::native {

namespace fs = std::filesystem;

bool tool_on_path(const std::string& name, const std::string& path_env)
{
    std::string_view env = path_env;
    while (!env.empty()) {
        const std::size_t colon = env.find(':');
        const std::string_view dir = env.substr(0, colon);
        if (!dir.empty()) {
            const fs::path candidate = fs::path(std::string(dir)) / name;
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec) && (fs::status(candidate, ec).permissions() & fs::perms::owner_exec) != fs::perms::none)
                return true;
        }
        if (colon == std::string_view::npos)
            break;
        env.remove_prefix(colon + 1);
    }
    return false;
}

bool available()
{
    if (const char* env = std::getenv("GS_DIALOGS"); env && std::string_view(env) == "imgui")
        return false;
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    const char* path = std::getenv("PATH");
    const std::string path_env = path ? path : "";
    return tool_on_path("kdialog", path_env) || tool_on_path("zenity", path_env);
#endif
}

std::optional<fs::path> run(const Request& request)
{
    std::vector<const char*> patterns;
    for (const std::string& p : request.patterns)
        patterns.push_back(p.c_str());
    const char* const* patterns_arg = patterns.empty() ? nullptr : patterns.data();
    const int pattern_count = static_cast<int>(patterns.size());
    const char* description = request.description.empty() ? nullptr : request.description.c_str();
    const char* default_name = request.default_name.empty() ? nullptr : request.default_name.c_str();

    const char* result = nullptr;
    switch (request.kind) {
    case Kind::OpenFile:
        result = tinyfd_openFileDialog(request.title.c_str(), default_name, pattern_count, patterns_arg, description, 0);
        break;
    case Kind::SaveFile:
        result = tinyfd_saveFileDialog(request.title.c_str(), default_name, pattern_count, patterns_arg, description);
        break;
    case Kind::SelectFolder:
        result = tinyfd_selectFolderDialog(request.title.c_str(), default_name);
        break;
    }
    if (!result)
        return std::nullopt;   // cancelled

    // The library returns UTF-8 on every platform.
    fs::path chosen{std::u8string(reinterpret_cast<const char8_t*>(result))};
    std::error_code ec;
    if (request.kind == Kind::SaveFile && fs::exists(chosen, ec)) {
        // Fixed text only: the file name is user input and must not reach the dialog.
        if (!tinyfd_messageBox("Replace file?", "The file already exists.\nDo you want to replace it?", "yesno",
                               "question", 0))
            return std::nullopt;
    }
    return chosen;
}

DialogJob::~DialogJob()
{
    if (future_.valid())
        future_.wait();
}

void DialogJob::start(Request request)
{
    future_ = std::async(std::launch::async, [request = std::move(request)] { return run(request); });
}

std::optional<DialogJob::Result> DialogJob::poll()
{
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Result{future_.get()};
}

} // namespace gs::native
