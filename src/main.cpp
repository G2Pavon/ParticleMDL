// ParticleMDL - C++20 / Dear ImGui

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <system_error>
#include <memory>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

int run_gui()
{
    const GlfwLibrary glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    const float scale = ui_scale();
    const WindowPtr window(glfwCreateWindow(static_cast<int>(kWindowWidth * scale),
                                            static_cast<int>(kWindowHeight * scale), "ParticleMDL",
                                            nullptr, nullptr));
    if (!window)
        throw std::runtime_error("cannot create the window");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(1);

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
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window.get(), GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        const double now = glfwGetTime();
        const double delta = now - last;
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

        glfwSwapBuffers(window.get());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1) {
        std::fprintf(stderr, "Unknown argument '%s'\n", argv[1]);
        return 2;
    }
    try {
        return run_gui();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
