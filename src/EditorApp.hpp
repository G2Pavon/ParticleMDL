#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "CompileJob.hpp"
#include "Exporter.hpp"
#include "FileDialog.hpp"
#include "NativeDialog.hpp"
#include "OrbitCamera.hpp"
#include "ParticleSystem.hpp"
#include "PreviewRenderer.hpp"
#include "PreviewScene.hpp"
#include "Settings.hpp"

namespace gs {

// The editor window: settings panels on both sides, live 3D preview in the middle.
// How a status message is coloured in the status bar.
enum class StatusTone { Success, Warning, Error };

class EditorApp {
public:
    EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    void draw_frame(double delta);
    void load_system(const std::filesystem::path& path);

private:
    // Actions
    void rebuild();   // new ParticleSystem + preview textures
    void apply_preset(int index);
    void new_system();
    void save_system(const std::filesystem::path& path);
    void export_data();
    // The file dialogs: the operating system's own when available, else the editor's.
    enum class DialogPurpose { Load, Save, ExportFolder, Compiler, Texture };
    static native::Request request_for(DialogPurpose purpose);
    void open_dialog(DialogPurpose purpose);
    void finish_dialog(DialogPurpose purpose, const std::optional<std::filesystem::path>& path);
    bool can_export() const;
    void poll_compile();
    void advance_clock(double delta);
    void refresh_preset_label();
    void set_status(std::string text, StatusTone tone);

    // Menus and dialogs
    void draw_menu_bar();
    static float status_bar_height();
    void draw_status_bar(float height);
    void draw_about_dialog();

    // Panels
    void draw_left_panel();
    void draw_middle_panel();
    void draw_right_panel();
    void draw_canvas(ImVec2 size);

    Settings settings_;
    std::optional<ParticleSystem> system_;
    std::optional<Settings> texture_settings_;   // settings the preview textures were made from
    bool dirty_ = true;
    int preset_index_ = 0;   // -1: no preset selected

    // Derived from the settings when they change, not every frame.
    std::string preset_label_;                    // name, plus " (modified)" after edits
    ExportReport export_report_;                  // limits, sequence size and warnings of the current settings
    std::optional<std::string> name_problem_;

    double preview_time_ = 0.0;
    OrbitCamera camera_;
    SceneOptions scene_options_;
    Scene scene_;
    PreviewRenderer renderer_;
    Rgb canvas_bg_{0, 0, 0};

    bool auto_compile_ = false;
    std::string compiler_path_;
    CompileJob compile_job_;

    std::string status_;
    StatusTone status_tone_ = StatusTone::Success;

    FileDialog save_dialog_;
    FileDialog load_dialog_;
    FileDialog compiler_dialog_;
    FileDialog export_dialog_;
    FileDialog texture_dialog_;
    native::DialogJob native_job_;                 // the operating system's dialog, when there is one
    std::optional<DialogPurpose> pending_dialog_;  // what the open native dialog is for
    std::optional<std::string> texture_problem_;   // why the imported image could not be used
    std::string export_dir_;                        // where Export writes; per session, not saved with the effect
    std::optional<std::string> export_dir_problem_;
    void set_export_dir(std::string dir);
    bool show_about_ = false;

    float bottom_height_ = 170.0f;   // measured each frame, used to size the canvas
};

} // namespace gs
