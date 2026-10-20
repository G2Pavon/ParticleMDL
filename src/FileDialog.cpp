#include "FileDialog.hpp"

#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "PathInput.hpp"
#include "PathUtil.hpp"
#include "Theme.hpp"
#include "Widgets.hpp"

namespace gs {

namespace fs = std::filesystem;

namespace {

bool has_extension(const fs::path& path, const std::vector<std::string>& extensions)
{
    std::string ext = to_utf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

} // namespace

FileDialog::FileDialog(std::string title, Mode mode, std::vector<std::string> extensions, std::string default_name)
    : title_(std::move(title)), mode_(mode), extensions_(std::move(extensions)), default_name_(std::move(default_name))
{
    std::error_code ec;
    dir_ = fs::current_path(ec);
}

void FileDialog::open()
{
    filename_ = default_name_;
    open_requested_ = true;
    pending_overwrite_.reset();
    refresh();
}

void FileDialog::open_at(const fs::path& dir)
{
    dir_ = dir;
    open();
}

void FileDialog::refresh()
{
    entries_.clear();
    error_.clear();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, fs::directory_options::skip_permission_denied, ec)) {
        if (to_utf8(e.path().filename()).starts_with('.'))
            continue;
        std::error_code type_ec;
        const bool is_dir = e.is_directory(type_ec);
        if (is_dir || (mode_ != Mode::Directory && (extensions_.empty() || has_extension(e.path(), extensions_))))
            entries_.push_back(e);
    }
    if (ec)
        error_ = ec.message();
    std::sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        std::error_code e1, e2;
        const bool da = a.is_directory(e1), db = b.is_directory(e2);
        if (da != db)
            return da;
        return a.path().filename() < b.path().filename();
    });
}

std::optional<fs::path> FileDialog::draw()
{
    if (open_requested_) {
        ImGui::OpenPopup(title_.c_str());
        open_requested_ = false;
    }

    std::optional<fs::path> chosen;
    ImGui::SetNextWindowSize(ImVec2(ui::px(600), ui::px(400)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title_.c_str(), nullptr))
        return chosen;

    ImGui::TextUnformatted(to_utf8(dir_).c_str());
    if (ImGui::Button("Up") && dir_.has_parent_path()) {
        dir_ = dir_.parent_path();
        refresh();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        refresh();
    if (!error_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(theme::kError, "%s", error_.c_str());
    }

    auto confirm = [&] {
        const std::string save_extension = extensions_.empty() ? std::string{} : extensions_.front();
        const DialogInput in = resolve_dialog_input(dir_, filename_, mode_ == Mode::Save, save_extension);
        switch (in.action) {
        case DialogInput::Action::None:
            break;
        case DialogInput::Action::Navigate:
            dir_ = in.path;
            filename_.clear();
            refresh();
            break;
        case DialogInput::Action::Choose:
            chosen = in.path;
            ImGui::CloseCurrentPopup();
            break;
        case DialogInput::Action::Confirm:
            pending_overwrite_ = in.path;
            overwrite_requested_ = true;
            break;
        case DialogInput::Action::Error:
            error_ = in.message;
            break;
        }
    };

    const float footer = ImGui::GetFrameHeightWithSpacing() * 2;
    if (ImGui::BeginChild("##entries", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
        std::optional<fs::path> enter_dir;
        for (const auto& e : entries_) {
            std::error_code ec;
            const bool is_dir = e.is_directory(ec);
            const std::string name = to_utf8(e.path().filename());
            const std::string label = is_dir ? "[" + name + "]" : name;
            if (!is_dir && !extensions_.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kLink);
            if (ImGui::Selectable(label.c_str(), !is_dir && name == filename_, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (is_dir) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        enter_dir = e.path();
                } else {
                    filename_ = name;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        confirm();
                }
            }
            if (!is_dir && !extensions_.empty())
                ImGui::PopStyleColor();
        }
        if (enter_dir) {
            dir_ = *enter_dir;
            refresh();
        }
    }
    ImGui::EndChild();

    if (mode_ == Mode::Directory) {
        if (ImGui::Button("Select this folder", ImVec2(ui::px(150), 0))) {
            chosen = dir_;
            ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::SetNextItemWidth(ui::px(-160));
        if (ImGui::InputText("##filename", &filename_, ImGuiInputTextFlags_EnterReturnsTrue))
            confirm();
        else if (ImGui::IsItemEdited())
            error_.clear();
        ImGui::SameLine();
        if (ImGui::Button(mode_ == Mode::Save ? "Save" : "Open", ImVec2(ui::px(70), 0)))
            confirm();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ui::px(70), 0)))
        ImGui::CloseCurrentPopup();

    // Nested confirmation; closing it with "Replace" also closes the dialog.
    if (overwrite_requested_) {
        ImGui::OpenPopup("Replace file?");
        overwrite_requested_ = false;
    }
    bool replace = false;
    if (ImGui::BeginPopupModal("Replace file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s already exists.\nDo you want to replace it?",
                    pending_overwrite_ ? to_utf8(pending_overwrite_->filename()).c_str() : "");
        if (ImGui::Button("Replace", ImVec2(ui::px(90), 0))) {
            replace = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(ui::px(90), 0))) {
            pending_overwrite_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (replace && pending_overwrite_) {
        chosen = *pending_overwrite_;
        pending_overwrite_.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return chosen;
}

} // namespace gs
