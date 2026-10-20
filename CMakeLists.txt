cmake_minimum_required(VERSION 3.24)
project(ParticleMDL VERSION 3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Libraries under third_party/ are vendored.
# GLFW is the only system dependency.

# ---------------------------------------------------------------------------
# OpenGL (Linux: mesa-libGL-devel; Windows and macOS ship it)
# ---------------------------------------------------------------------------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# GLFW 3.3 or newer
# ---------------------------------------------------------------------------
find_package(glfw3 3.3 REQUIRED)

# ---------------------------------------------------------------------------
# nlohmann/json 3.12.0 (header-only)
# ---------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json SYSTEM INTERFACE third_party/nlohmann)

# ---------------------------------------------------------------------------
# Dear ImGui 1.91.9b with the GLFW and OpenGL 3 backends
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
# The demo window (imgui_demo.cpp) is not built; stub its entry points.
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)

# ---------------------------------------------------------------------------
# Compiler flags shared by our own targets
# ---------------------------------------------------------------------------
add_library(gs_flags INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -ffp-contract=off: no fused multiply-add, so the simulation produces the
    # same doubles (and byte-identical exports) on every architecture.
    target_compile_options(gs_flags INTERFACE -Wall -Wextra -Wpedantic -ffp-contract=off)
elseif(MSVC)
    target_compile_options(gs_flags INTERFACE /W4 /fp:precise)
endif()

# ---------------------------------------------------------------------------
# stb_image: decode imported textures (PNG, JPG, BMP, TGA). Single-header library.
# ---------------------------------------------------------------------------
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE third_party/stb)

# ---------------------------------------------------------------------------
# tinyfiledialogs 3.21.5 (zlib license): the operating system's own file
# dialogs. Linux uses kdialog or zenity when installed.
# ---------------------------------------------------------------------------
add_library(tinyfiledialogs STATIC third_party/tinyfiledialogs/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs SYSTEM PUBLIC third_party/tinyfiledialogs)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tinyfiledialogs PRIVATE -w)   # third-party code, kept as is
endif()
if(WIN32)
    target_link_libraries(tinyfiledialogs PRIVATE comdlg32 ole32 shell32)
endif()

# ---------------------------------------------------------------------------
# Core: settings, simulation, textures, export and compiler (no GUI dependencies)
# ---------------------------------------------------------------------------
add_library(particles_core STATIC
    src/Settings.cpp
    src/SettingsJson.cpp
    src/Presets.cpp
    src/ParticleSystem.cpp
    src/TextureGenerator.cpp
    src/ImageImport.cpp
    src/Exporter.cpp
    src/Compiler.cpp
    src/CompileJob.cpp)
target_include_directories(particles_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(particles_core
    PUBLIC nlohmann_json::nlohmann_json Threads::Threads
    PRIVATE stb gs_flags)

# posix_spawn_file_actions_addchdir_np (glibc 2.29+, macOS 10.15+) lets the
# compiler run inside the export folder without changing our own directory.
include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(posix_spawn_file_actions_addchdir_np "spawn.h" GS_HAVE_SPAWN_ADDCHDIR)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(GS_HAVE_SPAWN_ADDCHDIR)
    target_compile_definitions(particles_core PRIVATE GS_HAVE_SPAWN_ADDCHDIR)
endif()

# ---------------------------------------------------------------------------
# Editor support: preview camera and scene, path input, native dialogs. No
# ImGui or OpenGL, so it can be built and tested without a window.
# ---------------------------------------------------------------------------
add_library(particles_editor_support STATIC
    src/OrbitCamera.cpp
    src/PreviewScene.cpp
    src/PathInput.cpp
    src/NativeDialog.cpp)
target_link_libraries(particles_editor_support
    PUBLIC particles_core
    PRIVATE tinyfiledialogs gs_flags)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(particlemdl
    src/main.cpp
    src/EditorApp.cpp
    src/Widgets.cpp
    src/Theme.cpp
    src/PreviewRenderer.cpp
    src/GlTexture.cpp
    src/FileDialog.cpp)
target_link_libraries(particlemdl PRIVATE particles_editor_support imgui gs_flags)
target_compile_definitions(particlemdl PRIVATE GS_VERSION="${PROJECT_VERSION}")
if(WIN32)
    # GUI subsystem: double-clicking the exe opens no console window.
    set_target_properties(particlemdl PROPERTIES WIN32_EXECUTABLE TRUE)
    # The GUI subsystem defaults to WinMain; keep the plain main() entry point.
    if(MSVC)
        target_link_options(particlemdl PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

# ---------------------------------------------------------------------------
# Unit tests (ctest; disable with -DBUILD_TESTING=OFF)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    # doctest 2.4.11 (single header, MIT)
    add_library(doctest INTERFACE)
    target_include_directories(doctest SYSTEM INTERFACE third_party/doctest)
    add_subdirectory(tests)
endif()
