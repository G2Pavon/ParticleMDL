#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::native {

// The operating system's own file dialogs. On Linux they need kdialog or
// zenity on the PATH; when neither is installed (or GS_DIALOGS=imgui is set)
// the editor uses its own dialogs instead.
bool available();

enum class Kind { OpenFile, SaveFile, SelectFolder };

struct Request {
    Kind kind = Kind::OpenFile;
    std::string title;
    // Only fixed names here: the dialog library builds shell commands on Linux,
    // so no text typed or chosen by the user may reach a request.
    std::string default_name;
    std::vector<std::string> patterns;   // "*.png" etc.; empty accepts any file
    std::string description;             // label of the pattern filter
    // The folder a folder dialog starts in. It does reach a shell command on Linux, inside double
    // quotes, so it is passed only when shell_safe_path() accepts it; otherwise the dialog starts
    // where the system would.
    std::filesystem::path start_folder;
};

// Whether a UTF-8 path can go inside double quotes in a shell command without the shell reading
// anything into it: no $, backquote, quote or control character, and no backslash but on Windows,
// where the dialogs do not go through a shell. The dialog library itself only refuses quotes.
bool shell_safe_path(std::string_view path);

// Shows the dialog and waits for it. Empty when cancelled. A save over an
// existing file is confirmed first.
std::optional<std::filesystem::path> run(const Request& request);

// Runs one dialog on a worker thread, so the editor keeps drawing while it is
// open. Poll it each frame.
class DialogJob {
public:
    DialogJob() = default;
    ~DialogJob();   // waits for a dialog still open
    DialogJob(const DialogJob&) = delete;
    DialogJob& operator=(const DialogJob&) = delete;

    void start(Request request);
    bool busy() const { return future_.valid(); }
    // Set once the dialog closed: a path, or nothing if it was cancelled.
    struct Result {
        std::optional<std::filesystem::path> path;
    };
    // The result on the first call after the dialog closed, else nullopt.
    std::optional<Result> poll();

private:
    std::future<std::optional<std::filesystem::path>> future_;
};

// Whether `name` is an executable in one of the directories of `path_env`
// (a PATH-style list). Used to find kdialog and zenity.
bool tool_on_path(const std::string& name, const std::string& path_env);

} // namespace gs::native
