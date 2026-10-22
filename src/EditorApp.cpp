#include "EditorApp.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include "Exporter.hpp"
#include "PathUtil.hpp"
#include "StagedFiles.hpp"
#include "Presets.hpp"
#include "Theme.hpp"
#include "SettingsJson.hpp"
#include "Widgets.hpp"

namespace gs {

namespace {

namespace fs = std::filesystem;

using theme::kAccent;
using theme::kError;
using theme::kMuted;
using theme::kSuccess;
using theme::kWarning;


// The ranges of the sliders that have hard limits in Settings.hpp. A slider does not clamp:
// a value typed past its end (Ctrl+click) or loaded from a file is kept until it leaves the
// hard limits, which validate() enforces. So the sliders must lie inside them.
struct SliderRange {
    double lo, hi;
};
constexpr SliderRange kSliderDuration{0.5, 10.0};
constexpr SliderRange kSliderLifespan{0.1, 1.0};
constexpr SliderRange kSliderHardness{0.2, 5.0};
static_assert(kSliderDuration.lo >= kMinDuration && kSliderDuration.hi <= kMaxDuration);
static_assert(kSliderLifespan.lo >= kMinLifespan && kSliderLifespan.hi <= 1.0);
static_assert(kSliderHardness.lo >= kMinHardness && kSliderHardness.hi <= kMaxHardness);

constexpr const char* kBudgetHelpTitle = "GoldSrc limits animation data to 64 KB per sequence.\n\nHow to save memory:";
constexpr const char* kBudgetHelpBody =
    "Lower 'Particle Count'\nLower 'Duration'\nLower 'FPS'\nDisable scaling Vertex Bones\nLess geometry quads";

void budget_tooltip()
{
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextColored(kAccent, "%s", kBudgetHelpTitle);
        ImGui::TextUnformatted(kBudgetHelpBody);
        ImGui::EndTooltip();
    }
}

void wrapped_text(ImVec4 color, const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

EditorApp::EditorApp()
    :
#ifdef _WIN32
      compiler_path_("studiomdl.exe"),
#else
      compiler_path_("./studiomdlplusplus"),
#endif
      save_dialog_("Save System", FileDialog::Mode::Save, {".json"}, "particle_system.json"),
      load_dialog_("Load System", FileDialog::Mode::Open, {".json"}),
      compiler_dialog_("Select Compiler", FileDialog::Mode::Open),
      export_dialog_("Export Folder", FileDialog::Mode::Directory),
      texture_dialog_("Choose Image", FileDialog::Mode::Open, {".png", ".jpg", ".jpeg", ".bmp", ".tga"}),
      export_dir_(to_utf8(std::filesystem::current_path()))
{
    set_export_dir(export_dir_);
    apply_preset(0);   // Fountain
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void EditorApp::rebuild()
{
    validate(settings_);   // the panels show the values actually used
    system_.emplace(settings_);
    baked_ = bake(*system_);
    table_ = animation_table(*system_, baked_);
    export_report_ = analyze_export(*system_, table_, baked_);
    name_problem_ = model_name_problem(settings_.model_name);
    refresh_preset_label();
    // Quantizing the texture is the slow part; physics sliders do not need it.
    if (!texture_settings_ || *texture_settings_ != settings_.texture) {
        texture_problem_.reset();
        texture_settings_ = settings_.texture;
        if (settings_.texture.file.empty()) {
            renderer_.update_textures(settings_.texture);   // a pattern: instant
        } else {
            // A picture is decoded and reduced on a worker; the old texture stays until it is ready.
            texture_job_.request(settings_.texture);
        }
    }
    dirty_ = false;
}

void EditorApp::poll_texture()
{
    const std::optional<TextureJob::Result> result = texture_job_.poll();
    if (!result || result->settings != settings_.texture)
        return;   // nothing yet, or made for a picture that was changed since
    if (result->image) {
        renderer_.set_texture(*result->image);
        return;
    }
    // Preview the pattern instead; the export refuses until the image is fixed.
    texture_problem_ = result->error;
    TextureSettings pattern = settings_.texture;
    pattern.file.clear();
    renderer_.update_textures(pattern);
}

void EditorApp::apply_preset(int index)
{
    const auto presets = builtin_presets();
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    preset_index_ = index;
    gs::apply_preset(settings_, presets[index]);
    system_base_.reset();
    particle_base_.reset();
    rebuild();
    preview_time_ = 0.0;
}

void EditorApp::new_system()
{
    const std::string name = settings_.model_name;
    settings_ = blank_settings();
    settings_.model_name = name;
    system_base_.reset();
    particle_base_.reset();
    preset_index_ = -1;
    camera_.reset();
    status_.clear();
    set_log({});
    rebuild();
    preview_time_ = 0.0;
}

void EditorApp::set_seed(SeedSide side, int seed)
{
    std::optional<Settings>& base = side == SeedSide::System ? system_base_ : particle_base_;
    if (!base)
        base = settings_;   // the values before this side's first roll
    roll_settings(settings_, *base, side, seed);
    dirty_ = true;
}

void EditorApp::save_system(const fs::path& path)
{
    try {
        // Written next to the file and moved over it, so a failed save keeps the old one.
        write_file_atomically(path, settings_file_text(settings_));
        set_status("Saved: " + to_utf8(path.filename()), StatusTone::Success);
    } catch (const std::exception& e) {
        set_status(std::string("Error: ") + e.what(), StatusTone::Error);
    }
}

void EditorApp::load_system(const fs::path& path)
{
    try {
        SettingsLoad loaded = read_settings_file(path, settings_);
        settings_ = std::move(loaded.settings);
        system_base_.reset();
        particle_base_.reset();
        preset_index_ = -1;
        rebuild();
        preview_time_ = 0.0;
        std::string log;
        for (const std::string& w : loaded.warnings) {
            std::fprintf(stderr, "%s: %s\n", to_utf8(path.filename()).c_str(), w.c_str());
            log += to_utf8(path.filename()) + ": " + w + "\n";
        }
        set_log(std::move(log));
        if (loaded.warnings.empty())
            set_status("Loaded: " + to_utf8(path.filename()), StatusTone::Success);
        else
            set_status("Loaded: " + to_utf8(path.filename()) + " with " + std::to_string(loaded.warnings.size()) +
                           " warning(s), see the log", StatusTone::Warning);
    } catch (const std::exception& e) {
        set_status(std::string("Error: ") + e.what(), StatusTone::Error);
    }
}

void EditorApp::set_export_dir(std::string dir)
{
    export_dir_ = std::move(dir);
    std::error_code ec;
    export_dir_problem_.reset();
    if (!std::filesystem::is_directory(from_utf8(export_dir_), ec))
        export_dir_problem_ = "Folder does not exist";
}

void EditorApp::export_data()
{
    try {
        // system_, baked_ and table_ are those of the current settings: a change rebuilds them
        // at the start of the next frame, and a change made in this frame is taken in here.
        if (dirty_)
            rebuild();
        const ParticleSystem& sys = *system_;
        const std::filesystem::path folder = from_utf8(export_dir_);
        export_model(sys, baked_, table_, folder);
        const std::string safe = safe_model_name(sys.settings().model_name);

        if (auto_compile_) {
            set_status("Compiling .mdl in " + export_dir_ + "...", StatusTone::Warning);
            compiling_.emplace(sys);
            compiling_mdl_ = folder / from_utf8(safe + ".mdl");
            compile_job_.start(compiler_path_, safe, folder, keep_sources_);
        } else {
            set_status("Success! Textures, SMDs, and " + safe + ".qc written to " + export_dir_ + ".", StatusTone::Success);
        }
    } catch (const std::exception& e) {
        set_status(std::string("Error: ") + e.what(), StatusTone::Error);
    }
}

void EditorApp::poll_compile()
{
    try {
        const auto result = compile_job_.poll();
        if (!result)
            return;
        switch (result->status) {
        case CompileResult::Status::Ok: {
            // Read the model back: what the compiler made, and whether it is what was exported.
            std::string summary, problems;
            try {
                const MdlInfo model = read_mdl_file(compiling_mdl_);
                summary = describe_mdl(model);
                if (compiling_)
                    for (const std::string& problem : compare_with_export(model, *compiling_))
                        problems += "Check: " + problem + "\n";
            } catch (const std::exception& e) {
                problems = std::string("Check: the compiled model could not be read: ") + e.what() + "\n";
            }
            set_log(problems + result->output);
            open_log_ = !problems.empty();   // unfolded only when the check found something
            if (problems.empty())
                set_status("Success! " + compile_job_.model_name() + ".mdl (" + summary + ")", StatusTone::Success);
            else
                set_status("Compiled " + compile_job_.model_name() + ".mdl, but it differs from the export: see the log",
                           StatusTone::Warning);
            break;
        }
        case CompileResult::Status::CompilerNotFound:
            std::fprintf(stderr, "Compiler not found: '%s'\n", compiler_path_.c_str());
            set_log("Compiler not found: '" + compiler_path_ + "'");
            set_status("Compiler not found! Check path.", StatusTone::Error);
            break;
        case CompileResult::Status::Failed:
            std::fprintf(stderr, "--- COMPILER ERROR ---\n%s\n", result->output.c_str());
            set_log(result->output.empty() ? "The compiler failed without any output." : result->output);
            set_status("Compile failed! See the log below.", StatusTone::Error);
            break;
        case CompileResult::Status::TimedOut:
            std::fprintf(stderr, "--- COMPILER TIMED OUT ---\n%s\n", result->output.c_str());
            set_log(result->output + (result->output.empty() ? "" : "\n") +
                    timeout_message(CompileOptions{}.timeout, settings_.loop));
            set_status("Compile timed out! See the log below.", StatusTone::Error);
            break;
        case CompileResult::Status::Cancelled:
            set_status("Compile cancelled.", StatusTone::Warning);
            break;
        }
    } catch (const std::exception& e) {
        set_status(std::string("Error: ") + e.what(), StatusTone::Error);
    }
}

void EditorApp::refresh_preset_label()
{
    preset_label_.clear();
    if (preset_index_ < 0)
        return;
    const Preset& preset = builtin_presets()[preset_index_];
    preset_label_ = preset.name;
    Settings untouched = settings_;
    gs::apply_preset(untouched, preset);
    if (untouched != settings_)
        preset_label_ += " (modified)";
}

void EditorApp::advance_clock(double delta)
{
    const double loop = system_->loop_duration();
    preview_time_ += delta;
    if (preview_time_ > loop)
        preview_time_ = floor_mod(preview_time_, loop);
}

void EditorApp::set_log(std::string text)
{
    log_ = std::move(text);
    open_log_ = !log_.empty();
}

void EditorApp::set_status(std::string text, StatusTone tone)
{
    status_ = std::move(text);
    status_tone_ = tone;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

native::Request EditorApp::request_for(DialogPurpose purpose)
{
    using native::Kind;
    const std::vector<std::string> images{"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga"};
    switch (purpose) {
    case DialogPurpose::Load:
        return {Kind::OpenFile, "Load settings", "", {"*.json"}, "Particle settings (*.json)"};
    case DialogPurpose::Save:
        return {Kind::SaveFile, "Save settings", "particle_system.json", {"*.json"}, "Particle settings (*.json)"};
    case DialogPurpose::ExportFolder:
        return {Kind::SelectFolder, "Choose export folder", "", {}, ""};
    case DialogPurpose::Compiler:
#ifdef _WIN32
        return {Kind::OpenFile, "Choose studiomdl", "", {"*.exe"}, "Programs (*.exe)"};
#else
        return {Kind::OpenFile, "Choose studiomdl", "", {}, ""};
#endif
    case DialogPurpose::Texture:
        return {Kind::OpenFile, "Choose image", "", images, "Images (*.png, *.jpg, *.jpeg, *.bmp, *.tga)"};
    }
    return {};
}

void EditorApp::open_dialog(DialogPurpose purpose)
{
    if (native::available()) {
        if (native_job_.busy())
            return;   // one dialog at a time
        pending_dialog_ = purpose;
        native_job_.start(request_for(purpose));
        return;
    }
    switch (purpose) {
    case DialogPurpose::Load: load_dialog_.open(); break;
    case DialogPurpose::Save: save_dialog_.open(); break;
    case DialogPurpose::ExportFolder: export_dialog_.open_at(export_dir_); break;
    case DialogPurpose::Compiler: compiler_dialog_.open(); break;
    case DialogPurpose::Texture: texture_dialog_.open(); break;
    }
}

void EditorApp::finish_dialog(DialogPurpose purpose, const std::optional<std::filesystem::path>& path)
{
    if (!path)
        return;   // cancelled
    switch (purpose) {
    case DialogPurpose::Load: load_system(*path); break;
    case DialogPurpose::Save: save_system(*path); break;
    case DialogPurpose::ExportFolder: set_export_dir(to_utf8(*path)); break;
    case DialogPurpose::Compiler: compiler_path_ = to_utf8(*path); break;
    case DialogPurpose::Texture:
        settings_.texture.file = to_utf8(*path);
        dirty_ = true;
        set_status("Texture: " + to_utf8(path->filename()), StatusTone::Success);
        break;
    }
}

void EditorApp::draw_frame(double delta)
{
    poll_compile();
    poll_texture();
    if (dirty_)
        rebuild();
    advance_clock(delta);

    draw_menu_bar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float status_height = status_bar_height();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - status_height));
    constexpr ImGuiWindowFlags kMainFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("main_window", nullptr, kMainFlags);

    // Titles stay put: they sit above each panel's scrolling region.
    ImGui::BeginGroup();
    ImGui::TextColored(kAccent, "System");
    ImGui::Separator();
    if (ImGui::BeginChild("left_panel", ImVec2(ui::px(340), 0), ImGuiChildFlags_Borders))
        draw_left_panel();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextColored(kAccent, "Preview");
    ImGui::Separator();
    if (ImGui::BeginChild("mid_panel", ImVec2(ui::px(-350), 0), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        draw_middle_panel();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextColored(kAccent, "Particle");
    ImGui::Separator();
    if (ImGui::BeginChild("right_panel", ImVec2(ui::px(340), 0), ImGuiChildFlags_Borders))
        draw_right_panel();
    ImGui::EndChild();
    ImGui::EndGroup();

    draw_status_bar(status_height);
    // Results of the operating system's dialog, and of the editor's own dialogs.
    if (auto result = native_job_.poll()) {
        if (pending_dialog_)
            finish_dialog(*pending_dialog_, result->path);
        pending_dialog_.reset();
    }
    if (auto p = save_dialog_.draw())
        finish_dialog(DialogPurpose::Save, p);
    if (auto p = load_dialog_.draw())
        finish_dialog(DialogPurpose::Load, p);
    if (auto p = compiler_dialog_.draw())
        finish_dialog(DialogPurpose::Compiler, p);
    if (auto p = export_dialog_.draw())
        finish_dialog(DialogPurpose::ExportFolder, p);
    if (auto p = texture_dialog_.draw())
        finish_dialog(DialogPurpose::Texture, p);
    draw_about_dialog();

    ImGui::End();
}

bool EditorApp::can_export() const
{
    return !compile_job_.busy() && !name_problem_ && !export_dir_problem_ && export_report_.blockers.empty();
}

void EditorApp::draw_menu_bar()
{
    // Shortcuts are ignored while a dialog is open, so typing in its name field
    // cannot start another one.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            new_system();
        if (ImGui::IsKeyPressed(ImGuiKey_O, false))
            open_dialog(DialogPurpose::Load);
        if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            open_dialog(DialogPurpose::Save);
        if (ImGui::IsKeyPressed(ImGuiKey_E, false) && can_export())
            export_data();
    }

    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N"))
            new_system();
        if (ImGui::MenuItem("Load...", "Ctrl+O"))
            open_dialog(DialogPurpose::Load);
        if (ImGui::MenuItem("Save...", "Ctrl+S"))
            open_dialog(DialogPurpose::Save);
        ImGui::Separator();
        // Same action and conditions as the Export button in the middle panel.
        if (ImGui::MenuItem(auto_compile_ ? "Export .mdl" : "Export SMD/QC", "Ctrl+E", false, can_export()))
            export_data();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About and controls"))
            show_about_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

float EditorApp::status_bar_height()
{
    return ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
}

namespace {

// Status messages use pure colours, unlike the theme's softer tones.
ImVec4 status_tone_color(StatusTone tone)
{
    switch (tone) {
    case StatusTone::Success: return {0.0f, 1.0f, 0.0f, 1.0f};
    case StatusTone::Warning: return {1.0f, 1.0f, 0.0f, 1.0f};
    case StatusTone::Error: return {1.0f, 0.0f, 0.0f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

} // namespace

void EditorApp::draw_status_bar(float height)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
    constexpr ImGuiWindowFlags kBarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##statusbar", nullptr, kBarFlags);

    // Right: sequence memory against the limit. The bar is fixed, the text is not.
    const double used_kb = static_cast<double>(export_report_.sequence_bytes) / 1024.0;
    const double limit_kb = static_cast<double>(kMaxSequenceBytes) / 1024.0;
    char memory[64];
    std::snprintf(memory, sizeof memory, "Sequence memory: %.1f / %.1f KB", used_kb, limit_kb);
    const float bar_width = ui::px(160);
    const float memory_width = ImGui::CalcTextSize(memory).x;
    // Before it, how far the preview is zoomed, against the default view.
    char zoom[32];
    std::snprintf(zoom, sizeof zoom, "Zoom: %.0f%%", camera_.zoom_factor() * 100.0);
    const float zoom_width = ImGui::CalcTextSize("Zoom: 600%").x;   // the widest, so the text does not shift
    const float right_width = zoom_width + style.ItemSpacing.x * 3 + memory_width + style.ItemSpacing.x + bar_width;

    // Left: the last status message, cut at the memory text.
    const float message_width = std::max(0.0f, ImGui::GetContentRegionAvail().x - right_width - style.ItemSpacing.x);
    ImGui::BeginChild("##status_message", ImVec2(message_width, ImGui::GetFrameHeight()), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::AlignTextToFramePadding();
    if (status_.empty())
        ImGui::TextDisabled("Ready");
    else
        ImGui::TextColored(status_tone_color(status_tone_), "%s", status_.c_str());
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - right_width);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(zoom);
    ImGui::SetItemTooltip("The preview's zoom against the default view (50%% to 600%%).\nThe mouse wheel over the preview zooms.");
    ImGui::SameLine();   // the memory text keeps its place, however wide the zoom text is
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - (memory_width + style.ItemSpacing.x + bar_width));
    ImGui::TextColored(used_kb > limit_kb ? kError : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", memory);
    budget_tooltip();
    ImGui::SameLine();
    const float fraction = static_cast<float>(std::min(1.0, used_kb / limit_kb));
    ImGui::ProgressBar(fraction, ImVec2(bar_width, 0), "");
    budget_tooltip();
    // The percentage, centred on the bar (ImGui's own overlay sits at the right end).
    char percent[16];
    std::snprintf(percent, sizeof percent, "%.0f%%", static_cast<double>(fraction) * 100.0);
    const ImVec2 bar_min = ImGui::GetItemRectMin(), bar_max = ImGui::GetItemRectMax();
    const ImVec2 text = ImGui::CalcTextSize(percent);
    ImGui::GetWindowDrawList()->AddText(ImVec2((bar_min.x + bar_max.x - text.x) * 0.5f,
                                               (bar_min.y + bar_max.y - text.y) * 0.5f),
                                        ImGui::GetColorU32(ImGuiCol_Text), percent);
    ImGui::End();
}

void EditorApp::draw_about_dialog()
{
    if (show_about_) {
        ImGui::OpenPopup("About##dialog");
        show_about_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(ui::px(460), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("About##dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextColored(kAccent, "ParticleMDL");
    ImGui::Text("Version %s", GS_VERSION);
    ImGui::Dummy(ImVec2(0, ui::px(8)));

    ImGui::TextColored(kAccent, "Mouse");
    ImGui::BulletText("Drag in the preview to orbit the camera");
    ImGui::BulletText("Scroll over the preview to zoom");
    ImGui::BulletText("Ctrl+click a slider to type a value");
    ImGui::Dummy(ImVec2(0, ui::px(8)));

    ImGui::TextColored(kAccent, "Keyboard");
    ImGui::BulletText("Ctrl+N  New system");
    ImGui::BulletText("Ctrl+O  Load settings");
    ImGui::BulletText("Ctrl+S  Save settings");
    ImGui::Dummy(ImVec2(0, ui::px(8)));

    ImGui::TextWrapped("Exports write .qc, .smd and .bmp files; Compile .mdl then runs the studiomdl "
                       "binary configured in the panel below the preview.");
    ImGui::Dummy(ImVec2(0, ui::px(12)));
    if (ImGui::Button("Close", ImVec2(ui::px(90), 0)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_left_panel()
{
    using namespace ui;

    const auto presets = builtin_presets();
    ImGui::SetNextItemWidth(ui::px(155));
    if (ImGui::BeginCombo("Preset", preset_label_.c_str())) {
        for (int i = 0; i < static_cast<int>(presets.size()); ++i)
            if (ImGui::Selectable(presets[i].name.data(), i == preset_index_))
                apply_preset(i);
        ImGui::EndCombo();
    }

    Settings& s = settings_;
    bool changed = false;
    if (int seed = s.system_seed;
        seed_input("system_seed", seed,
                   "System seed (Seed picks a new one).\n"
                   "Draws every value of this panel at random anywhere in its slider's range, turning drag,\n"
                   "bounce, turbulence, vortex, velocity and tilt on or off at random (not the rotation, the\n"
                   "FPS, nor the choices). The particle count is drawn among those the export accepts. It also\n"
                   "picks where each particle starts, the direction and speed it leaves with, its turbulence\n"
                   "and its lifespan variance. 0 brings back the values from before any seed."))
        set_seed(SeedSide::System, seed);
    const Settings before = s;
    group_label("Animation");
    changed |= reset_slider("count", "Particle Count", s.count, 1, max_particles(s.geometry, s.scale_mode), 64,
                            ImGuiSliderFlags_AlwaysClamp);
    changed |= reset_slider("duration", "Duration (s)", s.duration, kSliderDuration.lo, kSliderDuration.hi, 3.0, "%.2f");
    changed |= reset_slider("lifespan", "Lifespan (%)", s.lifespan, kSliderLifespan.lo, kSliderLifespan.hi, 0.85, "%.2f");
    changed |= reset_slider("lifespan_variance", "Lifespan Variance", s.lifespan_variance, 0.0, 0.9, 0.0, "%.2f");
    ImGui::SetItemTooltip("Each particle lives up to this fraction shorter than the lifespan, at random");
    changed |= reset_slider("fps", "FPS", s.fps, kMinFps, kMaxFps, 30, ImGuiSliderFlags_AlwaysClamp);
    changed |= enum_combo("End Behavior", s.end_behavior);
    ImGui::SetItemTooltip("What a particle does after its lifespan:\n"
                          "Drop Through Floor: falls, then hides below the effect\n"
                          "Hide Underground: hides below the effect at once\n"
                          "Hide in Core: hides at the center (needs Vertex Bone scaling)\n"
                          "Stop & Stay: freezes where it is\n"
                          "Endless Loop: keeps moving\n"
                          "With Vertex Bone scaling hidden particles also shrink to nothing.");
    changed |= ImGui::Checkbox("Loop", &s.loop);
    ImGui::SetItemTooltip("On: particles are born all through the loop and the animation repeats.\n"
                          "Off: it plays once, like an explosion or an impact, and holds its last frame.");
    if (!s.loop) {
        changed |= reset_slider("burst_window", "Burst Window", s.burst_window, 0.0, 1.0, 0.0, "%.2f");
        ImGui::SetItemTooltip("The part of the duration the particles are born in (0 = all at once)");
    }

    group_label("Volume");
    changed |= reset_slider("volume_width", "Width (X)", s.volume_width, 1, kMaxVolume, 128);
    changed |= reset_slider("volume_depth", "Depth (Y)", s.volume_depth, 1, kMaxVolume, 128);
    changed |= reset_slider("volume_height", "Height (Z)", s.volume_height, 1, kMaxVolume, 256);

    group_label("Velocity");
    changed |= reset_slider("velocity.x", "X", s.velocity.x, -500, 500, 0);
    changed |= reset_slider("velocity.y", "Y", s.velocity.y, -500, 500, 0);
    changed |= reset_slider("velocity.z", "Z", s.velocity.z, -500, 500, 0);

    group_label("Rotation");
    changed |= reset_slider("spin.x", "X (deg/s)", s.spin.x, -360, 360, 0);
    changed |= reset_slider("spin.y", "Y (deg/s)", s.spin.y, -360, 360, 0);
    changed |= reset_slider("spin.z", "Z (deg/s)", s.spin.z, -360, 360, 0);

    group_label("Emitter");
    changed |= reset_slider("emission_angle", "Angle (Fan)", s.emission_angle, 1, 360, 360);
    ImGui::SetItemTooltip("The sector of the disk the particles start in and fly out along (360 = all around)");
    changed |= reset_slider("spawn_radius", "Radius", s.spawn_radius, 0, 500, 0);
    ImGui::SetItemTooltip("Particles start on a disk of this radius (on its edge with Hollow Ring Mode)");
    changed |= reset_slider("spawn_height", "Height", s.spawn_height, 0, 400, 0);
    ImGui::SetItemTooltip("Particles start up to this far above or below the disk");
    changed |= reset_slider("emitter_tilt.x", "Tilt X (Pitch)", s.emitter_tilt.x, -180, 180, 0);
    changed |= reset_slider("emitter_tilt.y", "Tilt Y (Yaw)", s.emitter_tilt.y, -180, 180, 0);
    changed |= reset_slider("emitter_tilt.z", "Tilt Z (Roll)", s.emitter_tilt.z, -180, 180, 0);
    changed |= ImGui::Checkbox("Hollow Ring Mode", &s.hollow_ring);

    group_label("Physics");
    changed |= reset_slider("speed", "Horizontal Speed", s.speed, -200, 400, 60);
    changed |= reset_slider("burst", "Vertical Burst", s.burst, -300, 300, 120);
    changed |= reset_slider("spread", "Spread", s.spread, 0, 150, 30);
    changed |= reset_slider("gravity", "Gravity", s.gravity, -600, 200, -150);
    changed |= reset_slider("drag", "Drag", s.drag, 0.0, 5.0, 0.0, "%.2f");
    changed |= reset_slider("bounce", "Bounce", s.bounce, 0.0, 0.95, 0.0, "%.2f");
    ImGui::SetItemTooltip("0: no floor. Above 0 particles bounce off a floor at the height below and keep this share of\n"
                          "their speed at each hit; a particle that starts under the floor does not bounce.");
    if (s.bounce > 0.0)
        changed |= reset_slider("floor_height", "Floor Height", s.floor_height, -300, 300, 0);
    ImGui::SetItemTooltip("How fast the velocity dies away, per second (0 = none).\nWith gravity it gives a terminal speed.");
    changed |= reset_slider("turbulence", "Turbulence", s.turbulence, 0, 200, 0);
    changed |= reset_slider("vortex_spin", "Vortex Spin", s.vortex_spin, -720, 720, 0);
    changed |= reset_slider("vortex_pull", "Vortex Pull", s.vortex_pull, -300, 300, 0);
    if (!same_rolled_values(before, s, SeedSide::System))
        system_base_.reset();   // set by hand: these are the values the seed varies now
    dirty_ |= changed;
}

void EditorApp::draw_middle_panel()
{
    // Mouse wheel anywhere over the preview panel zooms.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
            camera_.zoom(wheel);
    }

    ui::color_edit("Background Color", canvas_bg_, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::Checkbox("Axes", &scene_options_.axes);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &scene_options_.grid);
    ImGui::SameLine();
    ImGui::Checkbox("Bounds", &scene_options_.bounds);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    draw_canvas(ImVec2(std::max(avail.x, 10.0f), std::max(avail.y - bottom_height_, 10.0f)));

    // --- Bottom controls (height measured for next frame's canvas size) ---
    const float bottom_start = ImGui::GetCursorPosY();
    ImGui::Dummy(ImVec2(0, ui::px(5)));
    ImGui::Separator();

    ImGui::SetNextItemWidth(ui::px(150));
    if (ImGui::InputText("Model Name", &settings_.model_name))
        dirty_ = true;   // the system holds a copy of the name, and the problem is worked out when it is rebuilt
    if (name_problem_)
        wrapped_text(kError, *name_problem_);
    for (const ExportBlocker& b : export_report_.blockers)
        wrapped_text(kError, b.message);
    for (const std::string& w : export_report_.warnings)
        wrapped_text(kWarning, w);

    ImGui::TextUnformatted("Export folder");
    ImGui::SetNextItemWidth(ui::px(-80));
    std::string folder = export_dir_;
    if (ImGui::InputText("##export_dir", &folder))
        set_export_dir(folder);
    ImGui::SameLine();
    if (ImGui::Button("Browse##export", ImVec2(ui::px(70), 0)))
        open_dialog(DialogPurpose::ExportFolder);
    if (export_dir_problem_)
        wrapped_text(kError, *export_dir_problem_);

    ImGui::Checkbox("Compile .mdl", &auto_compile_);
    if (auto_compile_) {
        ImGui::SameLine();
        ImGui::Checkbox("Keep sources", &keep_sources_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::px(-80));
        ImGui::InputText("##compiler_path", &compiler_path_);
        ImGui::SameLine();
        if (ImGui::Button("Browse", ImVec2(ui::px(70), 0)))
            open_dialog(DialogPurpose::Compiler);
    }
    ImGui::Dummy(ImVec2(0, ui::px(5)));

    const char* label = auto_compile_ ? "Export .mdl###export" : "Export SMD/QC###export";
    if (!export_report_.blockers.empty())
        switch (export_report_.blockers.front().kind) {
        case ExportBlocker::Kind::TooManyFrames: label = "Blocked: too many frames###export"; break;
        case ExportBlocker::Kind::SequenceTooLarge: label = "Blocked: animation over 64 KB###export"; break;
        case ExportBlocker::Kind::TooManyVertices: label = "Blocked: too many vertices###export"; break;
        case ExportBlocker::Kind::TooManyNormals: label = "Blocked: too many normals###export"; break;
        }
    ImGui::BeginDisabled(!can_export());
    if (ImGui::Button(label, ImVec2(-1, ui::px(40))))
        export_data();
    ImGui::EndDisabled();

    if (!log_.empty()) {
        if (open_log_) {
            ImGui::SetNextItemOpen(true);
            open_log_ = false;
        }
        if (ImGui::CollapsingHeader("Log"))
            ImGui::InputTextMultiline("##log", &log_, ImVec2(-1, ui::px(110)), ImGuiInputTextFlags_ReadOnly);
    }

    bottom_height_ = ImGui::GetCursorPosY() - bottom_start + ImGui::GetStyle().ItemSpacing.y;
}

void EditorApp::draw_right_panel()
{
    using namespace ui;

    Settings& s = settings_;
    bool changed = false;
    if (int seed = s.particle_seed;
        seed_input("particle_seed", seed,
                   "Particle seed (Seed picks a new one).\n"
                   "Draws the scaling, geometry and spin values of this panel at random anywhere in their\n"
                   "slider's range, turning size variance, cluster, mesh rotation and spin on or off at random\n"
                   "(not the texture, nor the choices). The cluster size is drawn among those the export\n"
                   "accepts, and the particle count is lowered if it must be. It also picks each particle's\n"
                   "spin spread, size variance and cluster copies. 0 brings back the values from before any seed."))
        set_seed(SeedSide::Particle, seed);
    const Settings before = s;
    ImGui::Dummy(ImVec2(0, ui::px(5)));
    ImGui::Image(renderer_.swatch(), ImVec2(ui::px(PreviewRenderer::kSwatchSize), ui::px(PreviewRenderer::kSwatchSize)));
    ImGui::Dummy(ImVec2(0, ui::px(10)));

    group_label("Image");
    ImGui::SetNextItemWidth(ui::px(-150));
    std::string file = s.texture.file;
    ImGui::InputTextWithHint("##texture_file", "custom image", &file, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse##texture", ImVec2(ui::px(70), 0)))
        open_dialog(DialogPurpose::Texture);
    ImGui::SameLine();
    ImGui::BeginDisabled(s.texture.file.empty());
    if (ImGui::Button("Clear##texture", ImVec2(ui::px(70), 0))) {
        s.texture.file.clear();
        changed = true;
    }
    ImGui::EndDisabled();
    if (texture_problem_)
        wrapped_text(kError, *texture_problem_);
    else if (texture_job_.busy() && !settings_.texture.file.empty())
        ImGui::TextDisabled("Loading image...");

    group_label("Texture");
    // An imported image replaces the pattern and colours, so those are greyed out.
    const bool imported = !s.texture.file.empty();
    if (imported)
        ImGui::TextColored(kMuted, "Pattern and colours are replaced by the image.");
    ImGui::BeginDisabled(imported);
    changed |= enum_combo("Pattern", s.texture.pattern);
    changed |= enum_combo("Color Mode", s.texture.color_mode);
    ImGui::SetNextItemWidth(ui::field_width());
    changed |= color_edit("Core Color", s.texture.core_color);
    ImGui::SetNextItemWidth(ui::field_width());
    changed |= color_edit("Edge Color", s.texture.edge_color);
    changed |= reset_slider("hardness", "Hardness", s.texture.hardness, kSliderHardness.lo, kSliderHardness.hi, 0.8, "%.2f");
    ImGui::EndDisabled();
    changed |= int_combo("Resolution", s.texture.size, kTextureSizes);

    group_label("Scaling");
    changed |= enum_combo("Mode", s.scale_mode);
    if (s.scale_mode == ScaleMode::VertexBone) {
        changed |= reset_slider("scale_start", "Start", s.scale_start, 0.1, 5.0, 1.0, "%.2f");
        changed |= reset_slider("scale_end", "End", s.scale_end, 0.0, 5.0, 1.0, "%.2f");
    }

    group_label("Geometry");
    changed |= enum_combo("Type", s.geometry);
    changed |= reset_slider("quad_width", "Width", s.quad_width, 1, 100, 6);
    changed |= reset_slider("quad_height", "Height", s.quad_height, 1, 100, 6);
    changed |= reset_slider("size_variance", "Size Variance", s.size_variance, 0.0, 0.9, 0.0, "%.2f");
    ImGui::SetItemTooltip("Each particle is up to this fraction larger or smaller, at random");
    changed |= ImGui::Checkbox("Double Sided", &s.double_sided);
    ImGui::BeginDisabled(s.scale_mode == ScaleMode::VertexBone);
    changed |= reset_slider("cluster_size", "Cluster Size", s.cluster_size, 1, 16, 1, ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Copies of the geometry on each particle, spread at random inside the radius below and turned\n"
                          "at random: many quads for one bone. studiomdl accepts 2048 vertices in a model.\n"
                          "Needs Static scaling (vertex bones scale each quad on its own).");
    if (s.cluster_size > 1)
        changed |= reset_slider("cluster_radius", "Cluster Radius", s.cluster_radius, 0, 200, 20);
    ImGui::EndDisabled();
    changed |= reset_slider("mesh_rotation.x", "Rot X (Pitch)", s.mesh_rotation.x, -180, 180, 0);
    changed |= reset_slider("mesh_rotation.y", "Rot Y (Yaw)", s.mesh_rotation.y, -180, 180, 0);
    changed |= reset_slider("mesh_rotation.z", "Rot Z (Roll)", s.mesh_rotation.z, -180, 180, 0);

    group_label("Rotation");
    changed |= ImGui::Checkbox("Face Direction of Travel", &s.align_to_velocity);
    ImGui::SetItemTooltip("The quad's Z axis points the way the particle moves (sparks, rain, tracers).\n"
                          "Spin Z then rolls it around that direction; Spin X and Y have no effect.");
    changed |= reset_slider("particle_spin.x", "X (deg/s)", s.particle_spin.x, -360, 360, 0);
    changed |= reset_slider("particle_spin.y", "Y (deg/s)", s.particle_spin.y, -360, 360, 0);
    changed |= reset_slider("particle_spin.z", "Z (deg/s)", s.particle_spin.z, -360, 360, 0);
    changed |= reset_slider("spin_spread", "Random Spread", s.spin_spread, 0, 360, 0);
    if (!same_rolled_values(before, s, SeedSide::Particle))
        particle_base_.reset();   // set by hand: these are the values the seed varies now
    dirty_ |= changed;
}

void EditorApp::draw_canvas(ImVec2 size)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft);

    // Left drag orbits the camera.
    if (ImGui::IsItemActive() && !ImGui::IsItemActivated()) {
        const ImVec2 md = ImGui::GetIO().MouseDelta;
        camera_.orbit(md.x, md.y);
    }

    camera_.set_focal_length(OrbitCamera::kFocalLength * static_cast<double>(ui::scale()));
    const Point2 center{origin.x + size.x / 2, origin.y + size.y / 2};
    build_scene(*system_, baked_, camera_, preview_time_, center, scene_options_, scene_);
    renderer_.draw(ImGui::GetWindowDrawList(), origin, size, canvas_bg_, scene_);
}

} // namespace gs
