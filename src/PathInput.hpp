#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gs {

// What a file dialog should do with the text typed into its file name field.
struct DialogInput {
    enum class Action {
        None,        // empty input
        Navigate,    // `path` is a folder: show it
        Choose,      // `path` is the file to open or save
        Confirm,     // save over the existing file `path` after asking
        Error,       // `message` says why
    };
    Action action = Action::None;
    std::filesystem::path path;
    std::string message;
};

// Resolves `input` against the dialog's current folder `dir`. Accepts names,
// relative paths, absolute paths and "~/...". When saving, `extension` (e.g.
// ".json") is appended if missing; when opening, the file must exist.
DialogInput resolve_dialog_input(const std::filesystem::path& dir, std::string_view input, bool save,
                                 std::string_view extension);

} // namespace gs
