cmake_minimum_required(VERSION 3.24)
project(ParticleMDL VERSION 3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# vcpkg's static triplets link the C runtime statically; our own targets must match them.
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(GS_WERROR "Treat warnings in our own code as errors (CI turns this on)" OFF)
option(GS_SANITIZE "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(GS_SANITIZE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "GS_SANITIZE needs GCC or Clang")
    endif()
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
target_include_directories(imgui SYSTEM PUBLIC
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
    target_compile_options(gs_flags INTERFACE -Wall -Wextra -Wpedantic -ffp-contract=off
        -Wshadow -Wconversion -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wdouble-promotion -Wformat=2)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang's -Wconversion includes sign conversions, which GCC's does not; keep both the same.
        # Its libstdc++ notice about <ciso646> (included by doctest) is not ours to fix.
        target_compile_options(gs_flags INTERFACE -Wno-sign-conversion "-Wno-error=#warnings"
            -Wno-unknown-warning-option -Wno-c2y-extensions)   # the latter: __COUNTER__ in the test macros
    endif()
    if(GS_WERROR)
        target_compile_options(gs_flags INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(gs_flags INTERFACE /W4 /fp:precise /utf-8 /permissive- /Zc:__cplusplus)
    if(GS_WERROR)
        target_compile_options(gs_flags INTERFACE /WX)
    endif()
endif()
if(WIN32)
    # <windows.h> must not define min and max as macros: they break std::min and std::max.
    # The C runtime's "may be unsafe" notices (getenv, sscanf) are about functions used safely here.
    target_compile_definitions(gs_flags INTERFACE NOMINMAX _CRT_SECURE_NO_WARNINGS)
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
# studiomdl++ (G2Pavon/studiomdlPlusPlus, lib branch, commit 73bee88; Half-Life 1
# SDK license): compiles the exported QC, SMDs and texture into the .mdl in memory.
# ---------------------------------------------------------------------------
set(STUDIOMDL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/studiomdl)
add_library(studiomdl STATIC
    ${STUDIOMDL_DIR}/src/utils/cmdlib.cpp
    ${STUDIOMDL_DIR}/src/utils/mathlib.cpp
    ${STUDIOMDL_DIR}/src/utils/stripification.cpp
    ${STUDIOMDL_DIR}/src/format/image/bmpread.cpp
    ${STUDIOMDL_DIR}/src/filesystem.cpp
    ${STUDIOMDL_DIR}/src/format/qc.cpp
    ${STUDIOMDL_DIR}/src/studiomdl.cpp
    ${STUDIOMDL_DIR}/src/writemdl.cpp)
target_include_directories(studiomdl SYSTEM PUBLIC ${STUDIOMDL_DIR}/include)
target_include_directories(studiomdl PRIVATE ${STUDIOMDL_DIR}/src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Third-party code, kept as is; no fused multiply-add, so the .mdl is the same on every architecture.
    target_compile_options(studiomdl PRIVATE -w -ffp-contract=off)
elseif(MSVC)
    target_compile_options(studiomdl PRIVATE /W0 /fp:precise /utf-8)
endif()

# ---------------------------------------------------------------------------
# Core: settings, simulation, textures, export and compiler (no GUI dependencies)
# ---------------------------------------------------------------------------
add_library(particles_core STATIC
    src/Settings.cpp
    src/SettingsJson.cpp
    src/Presets.cpp
    src/BoneLayout.cpp
    src/EngineLook.cpp
    src/ParticleSystem.cpp
    src/SeedRoll.cpp
    src/Baking.cpp
    src/TextureGenerator.cpp
    src/TextureJob.cpp
    src/ImageImport.cpp
    src/StagedFiles.cpp
    src/MdlReader.cpp
    src/Exporter.cpp
    src/Compiler.cpp
    src/CompileJob.cpp
    src/WinProcess.cpp)
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
    src/GameFolder.cpp
    src/Skybox.cpp
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
# Command-line exporter: particles_core only, no GUI or dialog libraries
# ---------------------------------------------------------------------------
add_executable(particlemdl_cli src/cli_main.cpp)
target_link_libraries(particlemdl_cli PRIVATE particles_core gs_flags)
target_compile_definitions(particlemdl_cli PRIVATE GS_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(particlemdl_cli PRIVATE shell32)   # CommandLineToArgvW: the arguments as UTF-8
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

# ---------------------------------------------------------------------------
# Installing and packaging (cmake --install, cpack): the editor, the command-line
# exporter, the licence and the readme.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS particlemdl particlemdl_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
# The built-in compiler is Half-Life 1 SDK code, whose license must go with it.
install(FILES third_party/studiomdl/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE.studiomdl)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CPACK_PACKAGE_NAME "particlemdl")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Simulate particles using animated GoldSrc models")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" gs_package_system)
    set(CPACK_PACKAGE_FILE_NAME "particlemdl-${PROJECT_VERSION}-${gs_package_system}")
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
    else()
        set(CPACK_GENERATOR TGZ)
    endif()
    include(CPack)
endif()
