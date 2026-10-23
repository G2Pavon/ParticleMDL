#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "Exporter.hpp"
#include "FileDialog.hpp"
#include "GameFolder.hpp"
#include "Skybox.hpp"
#include "NativeDialog.hpp"
#include "OrbitCamera.hpp"
#include "ParticleSystem.hpp"
#include "PreviewRenderer.hpp"
#include "PreviewScene.hpp"
#include "SeedRoll.hpp"
#include "TextureJob.hpp"
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
    enum class DialogPurpose { Load, Save, ExportFolder, Texture, GameFolder };
    static native::Request request_for(DialogPurpose purpose);
    void open_dialog(DialogPurpose purpose);
    void finish_dialog(DialogPurpose purpose, const std::optional<std::filesystem::path>& path);
    bool can_export() const;
    void poll_texture();
    void advance_clock(double delta);
    void refresh_preset_label();
    // `detail`, when given, is shown in the status bar's tooltip: the whole of what one line cannot hold.
    void set_status(std::string text, StatusTone tone, std::string detail = {});

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
    // What each panel's values were before its seed drew them, for seed 0 to bring back. Forgotten
    // when a value of the panel is set by hand, or the whole effect is replaced (preset, new,
    // load): those values are then the ones seed 0 brings back.
    std::optional<Settings> system_base_, particle_base_;
    void set_seed(SeedSide side, int seed);
    std::optional<ParticleSystem> system_;
    std::optional<TextureSettings> texture_settings_;   // what the preview textures were made from
    bool dirty_ = true;
    int preset_index_ = 0;   // -1: no preset selected

    // Derived from the settings when they change, not every frame.
    std::string preset_label_;                    // name, plus " (modified)" after edits
    AnimationTable table_;                        // the animation of system_, as exported
    BakedPoses baked_;                            // the frames as exported, for the game-accurate preview
    ExportReport export_report_;                  // limits, sequence size and warnings of the current settings
    std::optional<std::string> name_problem_;

    double preview_time_ = 0.0;
    OrbitCamera camera_;
    SceneOptions scene_options_;
    Scene scene_;
    PreviewRenderer renderer_;
    Rgb canvas_bg_{0, 0, 0};

    std::string status_;
    std::string status_detail_;   // the warnings, differences or compiler notes behind the status
    StatusTone status_tone_ = StatusTone::Success;

    FileDialog save_dialog_;
    FileDialog load_dialog_;
    FileDialog export_dialog_;
    FileDialog texture_dialog_;
    native::DialogJob native_job_;                 // the operating system's dialog, when there is one
    std::optional<DialogPurpose> pending_dialog_;  // what the open native dialog is for
    TextureJob texture_job_;                       // makes an imported picture's texture off the UI thread
    std::optional<std::string> texture_problem_;   // why the imported image could not be used
    std::filesystem::path config_path_;   // where config_ is kept between sessions
    AppConfig config_;                    // the game folder
    FileDialog game_dialog_;
    // Where Export writes: a folder inside <game>/models/ ("" is models/ itself). Per session.
    std::string models_subfolder_;
    std::optional<std::string> export_dir_problem_;   // why the game folder or that folder cannot be used
    void refresh_export_dir_problem();
    // Settings > Game Folder: a window of its own (not a popup, so a folder dialog can open over it).
    bool show_game_folder_ = false;
    std::string game_folder_edit_;
    void draw_game_folder_window();
    void set_game_folder(std::string folder);
    void save_config();
    // The preview's sky: config_.sky and config_.sky_name say which, skies_ what the game folder has.
    std::vector<std::string> skies_;
    void refresh_skies();
    void apply_sky();   // loads the chosen sky into the renderer, or takes it away
    bool show_about_ = false;

    float bottom_height_ = 170.0f;   // measured each frame, used to size the canvas
};

} // namespace gs
