#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gs {

// Minimal modal file browser built on Dear ImGui and std::filesystem.
class FileDialog {
public:
    // Directory: lists folders only and chooses the folder being shown.
    enum class Mode { Open, Save, Directory };

    // `extensions` (e.g. {".png", ".jpg"}, case-insensitive) filter the listing;
    // empty shows every file. Saving appends the first one when it is missing.
    // The name field also takes absolute paths, "~/..." and folder names;
    // saving over an existing file asks first.
    FileDialog(std::string title, Mode mode, std::vector<std::string> extensions = {},
               std::string default_name = {});

    void open();
    // Opens the dialog showing `dir`.
    void open_at(const std::filesystem::path& dir);
    // Call every frame; returns the chosen path on the frame it is confirmed.
    std::optional<std::filesystem::path> draw();

private:
    void refresh();

    std::string title_;
    Mode mode_;
    std::vector<std::string> extensions_;
    std::string default_name_;
    std::filesystem::path dir_;
    std::string filename_;
    std::vector<std::filesystem::directory_entry> entries_;
    std::string error_;
    bool open_requested_ = false;
    std::optional<std::filesystem::path> pending_overwrite_;   // waiting for "Replace?"
    bool overwrite_requested_ = false;
};

} // namespace gs
