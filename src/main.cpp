// ParticleMDL - C++20 / Dear ImGui
//
//   particlemdl                                  interactive editor
//   particlemdl [--preset NAME] [--load FILE.json] [--name MODEL] [--texture IMAGE] --export DIR [--compile STUDIOMDL]
//                                                    headless export (and compile), no window
//   particlemdl --screenshot FILE.ppm [--frames N] [--load FILE.json]
//                                                    render N frames (default 30) at a fixed
//                                                    1/60 s step in a hidden window, save, exit

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <system_error>
#include <memory>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "EditorApp.hpp"
#include "Compiler.hpp"
#include "Exporter.hpp"
#include "Presets.hpp"
#include "Theme.hpp"
#include "Widgets.hpp"
#include "SettingsJson.hpp"

namespace {

int run_headless(int argc, char** argv)
{
    gs::Settings settings;
    gs::apply_preset(settings, gs::builtin_presets()[0]);   // editor starts on "Fountain"
    std::optional<std::filesystem::path> out_dir;
    std::optional<std::string> compiler;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--preset" && has_value) {
            const gs::Preset* preset = gs::find_preset(argv[++i]);
            if (!preset) {
                std::fprintf(stderr, "Unknown preset '%s'\n", argv[i]);
                return 2;
            }
            gs::apply_preset(settings, *preset);
        } else if (arg == "--load" && has_value) {
            gs::SettingsLoad loaded = gs::read_settings_file(argv[++i], settings);
            for (const std::string& w : loaded.warnings)
                std::fprintf(stderr, "%s: %s\n", argv[i], w.c_str());
            settings = std::move(loaded.settings);
        } else if (arg == "--name" && has_value) {
            settings.model_name = argv[++i];
        } else if (arg == "--texture" && has_value) {
            settings.texture_file = argv[++i];   // an image to export instead of the pattern
        } else if (arg == "--export" && has_value) {
            out_dir = argv[++i];
        } else if (arg == "--compile" && has_value) {
            compiler = argv[++i];
        } else {
            std::fprintf(stderr, "Unknown or incomplete argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (!out_dir) {
        std::fprintf(stderr, "Missing --export DIR\n");
        return 2;
    }

    if (const auto problem = gs::model_name_problem(settings.model_name)) {
        std::fprintf(stderr, "%s\n", problem->c_str());
        return 2;
    }
    std::filesystem::create_directories(*out_dir);
    const gs::ParticleSystem sys(settings);
    for (const std::string& w : gs::export_warnings(sys))
        std::fprintf(stderr, "Warning: %s\n", w.c_str());
    gs::export_model(sys, *out_dir);
    const std::string safe = gs::safe_model_name(settings.model_name);
    std::printf("Exported %s (%d particles, %d frames, %.1f KB sequence) to %s\n", safe.c_str(), sys.num_particles(),
                sys.export_frames(), static_cast<double>(gs::sequence_bytes(sys)) / 1024.0, out_dir->string().c_str());
    if (!compiler)
        return 0;

    gs::CompileOptions options;
    options.work_dir = *out_dir;
    const gs::CompileResult r = gs::compile_model(*compiler, safe, options);
    switch (r.status) {
    case gs::CompileResult::Status::Ok:
        std::printf("Compiled %s\n", (*out_dir / (safe + ".mdl")).string().c_str());
        return 0;
    case gs::CompileResult::Status::CompilerNotFound:
        std::fprintf(stderr, "Compiler not found: %s\n", compiler->c_str());
        return 1;
    default:
        std::fprintf(stderr, "Compile failed:\n%s\n", r.output.c_str());
        return 1;
    }
}

void glfw_error(int code, const char* desc)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

constexpr int kWindowWidth = 1200, kWindowHeight = 900;

template <typename T>
std::optional<T> parse_number(std::string_view text)
{
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

// How much larger to draw the UI. Wayland and macOS already scale the
// framebuffer for HiDPI screens (GLFW_SCALE_FRAMEBUFFER); on X11 and Windows
// the UI itself has to grow with the monitor's content scale. GS_UI_SCALE
// overrides it.
float ui_scale()
{
    float s = 1.0f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        float xs = 1.0f, ys = 1.0f;
        glfwGetMonitorContentScale(monitor, &xs, &ys);
        s = std::max(xs, ys);
    }
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    const int platform = glfwGetPlatform();
    if (platform == GLFW_PLATFORM_WAYLAND || platform == GLFW_PLATFORM_COCOA)
        s = 1.0f;
#elif defined(__APPLE__)
    s = 1.0f;
#endif
    if (const char* env = std::getenv("GS_UI_SCALE"))
        if (const auto v = parse_number<float>(env); v && *v >= 0.5f && *v <= 4.0f)
            return *v;
    return std::clamp(s, 1.0f, 4.0f);
}

// Development aid: render a hidden window deterministically and save it.
struct ScreenshotRequest {
    std::filesystem::path path;
    int frames = 30;
    std::optional<std::filesystem::path> load;
};

void save_framebuffer_ppm(const std::filesystem::path& path, int w, int h)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write " + path.string());
    f << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)   // GL rows are bottom-up
        f.write(reinterpret_cast<const char*>(&pixels[static_cast<std::size_t>(y) * w * 3]), std::streamsize{w} * 3);
}

// Owns the GLFW library for its lifetime.
class GlfwLibrary {
public:
    GlfwLibrary()
    {
        glfwSetErrorCallback(glfw_error);
        if (!glfwInit())
            throw std::runtime_error("cannot initialize GLFW");
    }
    ~GlfwLibrary() { glfwTerminate(); }
    GlfwLibrary(const GlfwLibrary&) = delete;
    GlfwLibrary& operator=(const GlfwLibrary&) = delete;
};

struct WindowDeleter {
    void operator()(GLFWwindow* window) const noexcept { glfwDestroyWindow(window); }
};
using WindowPtr = std::unique_ptr<GLFWwindow, WindowDeleter>;

// The Dear ImGui context with its GLFW and OpenGL backends.
class ImGuiSession {
public:
    ImGuiSession(GLFWwindow* window, float scale, float framebuffer_scale)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        gs::theme::apply(ImGui::GetStyle());
        ImGui::GetStyle().ScaleAllSizes(scale);
        // Rasterize the font at its real pixel size so it stays crisp.
        ImFontConfig font;
        font.SizePixels = std::round(13.0f * scale * framebuffer_scale);
        font.OversampleH = font.OversampleV = 1;
        font.PixelSnapH = true;
        io.Fonts->AddFontDefault(&font);
        io.FontGlobalScale = 1.0f / framebuffer_scale;
        if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
            ImGui::DestroyContext();
            throw std::runtime_error("cannot initialize the ImGui GLFW backend");
        }
        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            throw std::runtime_error("cannot initialize the ImGui OpenGL backend");
        }
    }
    ~ImGuiSession()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    ImGuiSession(const ImGuiSession&) = delete;
    ImGuiSession& operator=(const ImGuiSession&) = delete;
};

int run_gui(const std::optional<ScreenshotRequest>& shot)
{
    const GlfwLibrary glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (shot)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const float scale = ui_scale();
    const WindowPtr window(glfwCreateWindow(static_cast<int>(kWindowWidth * scale),
                                            static_cast<int>(kWindowHeight * scale), "ParticleMDL",
                                            nullptr, nullptr));
    if (!window)
        throw std::runtime_error("cannot create the window");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(shot ? 0 : 1);

    // Pixels per window unit (2 on a HiDPI Wayland/macOS screen).
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(window.get(), &ww, &wh);
    glfwGetFramebufferSize(window.get(), &fw, &fh);
    const float framebuffer_scale =
        (ww > 0 && fw > 0) ? std::max(1.0f, static_cast<float>(fw) / static_cast<float>(ww)) : 1.0f;

    const ImGuiSession imgui(window.get(), scale, framebuffer_scale);
    gs::ui::set_scale(scale);

    // Declared last, destroyed first: its textures need the GL context.
    gs::EditorApp app;
    if (shot && shot->load)
        app.load_system(*shot->load);
    double last = glfwGetTime();
    for (int frame = 1; !glfwWindowShouldClose(window.get()); ++frame) {
        glfwPollEvents();
        if (!shot && glfwGetWindowAttrib(window.get(), GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        const double now = glfwGetTime();
        const double delta = shot ? 1.0 / 60.0 : now - last;
        last = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.draw_frame(delta);
        ImGui::Render();

        int w = 0, h = 0;
        glfwGetFramebufferSize(window.get(), &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (shot && frame >= shot->frames) {
            save_framebuffer_ppm(shot->path, w, h);
            break;
        }
        glfwSwapBuffers(window.get());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const std::span<char*> args(argv + 1, argv + argc);
    try {
        if (args.size() >= 2 && std::string_view(args[0]) == "--screenshot") {
            ScreenshotRequest shot;
            shot.path = args[1];
            for (std::size_t i = 2; i + 1 < args.size(); i += 2) {
                const std::string_view option = args[i];
                if (option == "--frames")
                    shot.frames = std::max(1, parse_number<int>(args[i + 1]).value_or(shot.frames));
                else if (option == "--load")
                    shot.load = args[i + 1];
            }
            return run_gui(shot);
        }
        if (!args.empty())
            return run_headless(argc, argv);
        return run_gui(std::nullopt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
