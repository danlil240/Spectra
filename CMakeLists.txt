cmake_minimum_required(VERSION 3.21)

# Read version from single source-of-truth
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/version.txt _ver_raw)
string(STRIP "${_ver_raw}" SPECTRA_VERSION)
set(SPECTRA_VERSION_STRING "${SPECTRA_VERSION}")

project(spectra VERSION ${SPECTRA_VERSION} LANGUAGES CXX)

# PROJECT_IS_TOP_LEVEL is false when Spectra is consumed via add_subdirectory()
# or FetchContent.  Developer-only conveniences (tests, examples, ROS2/PX4
# adapters, compile_commands.json, icon generation) default off in that case.
set(SPECTRA_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

# If a previous configure auto-sourced a ROS2 workspace, restore its
# environment variables so CMake re-runs triggered by the build still work.
include(${CMAKE_CURRENT_BINARY_DIR}/src/adapters/ros2/spectra_ros_env.cmake OPTIONAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(SPECTRA_IS_TOP_LEVEL AND NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Opt-in: symlink compile_commands.json into the source tree for clangd/ccls.
# Off by default so a configure never writes into the source directory.
option(SPECTRA_LINK_COMPILE_COMMANDS
    "Symlink build/compile_commands.json into the source tree (top-level builds only)" OFF)
if(SPECTRA_IS_TOP_LEVEL AND SPECTRA_LINK_COMPILE_COMMANDS
   AND EXISTS "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json")
    file(CREATE_LINK
        "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        SYMBOLIC)
endif()

# Python is only needed for optional code generators (hicolor icons, Emscripten
# Vulkan stubs); it is not a hard build requirement.
find_package(Python3 COMPONENTS Interpreter QUIET)

# Hicolor icon theme PNGs (Linux desktop integration).  Generated from
# icons/spectra_icon.png with Pillow when available; otherwise the 1024px
# source icon is installed as-is (see the install section).
set(SPECTRA_GENERATE_ICONS "AUTO" CACHE STRING
    "Generate resized hicolor icons with Python+Pillow: AUTO | ON | OFF")
set_property(CACHE SPECTRA_GENERATE_ICONS PROPERTY STRINGS AUTO ON OFF)
set(_spectra_have_pillow FALSE)
if(NOT SPECTRA_GENERATE_ICONS STREQUAL "OFF" AND Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import PIL"
        RESULT_VARIABLE _spectra_pil_result
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_spectra_pil_result EQUAL 0)
        set(_spectra_have_pillow TRUE)
    endif()
endif()
if(SPECTRA_GENERATE_ICONS STREQUAL "ON" AND NOT _spectra_have_pillow)
    message(FATAL_ERROR "SPECTRA_GENERATE_ICONS=ON but Python3 with Pillow was not found")
endif()
set(SPECTRA_HICOLOR_ICON_SIZES 16 32 48 64 128 256 512)
set(SPECTRA_HICOLOR_ICON_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/icons/hicolor)
if(_spectra_have_pillow AND SPECTRA_IS_TOP_LEVEL)
    set(SPECTRA_HICOLOR_ICONS_GENERATED TRUE)
    add_custom_command(
        OUTPUT ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}/16x16/apps/spectra.png
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_hicolor_icons.py
            --input ${CMAKE_CURRENT_SOURCE_DIR}/icons/spectra_icon.png
            --output-dir ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}
            --name spectra.png
            --sizes ${SPECTRA_HICOLOR_ICON_SIZES}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/icons/spectra_icon.png
        COMMENT "Generating hicolor icon theme PNGs"
        VERBATIM
    )
    add_custom_target(spectra_hicolor_icons ALL
        DEPENDS ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}/16x16/apps/spectra.png)
else()
    set(SPECTRA_HICOLOR_ICONS_GENERATED FALSE)
    if(SPECTRA_IS_TOP_LEVEL AND NOT SPECTRA_GENERATE_ICONS STREQUAL "OFF")
        message(STATUS "Pillow not found: installing the source icon instead of resized hicolor icons")
    endif()
endif()

include(GNUInstallDirs)

# Generate version header.  generated/spectra/version.hpp is used by internal
# targets; generated/public/ holds only public generated headers and is part of
# spectra-core's build-tree include path (find_package from the build dir).
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra/version.hpp
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/public/spectra/version.hpp
    @ONLY
)

# Embed font data into C arrays (eliminates runtime file dependency)
include(cmake/EmbedAssets.cmake)
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp)
    message("Font file Inter-Regular.ttf already embedded, skipping font embedding")
else()
    embed_binary_file(
        INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Inter-Regular.ttf
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp
        VARIABLE InterFont_ttf
    )
endif()

# Embed Spectra logo icon into C array for the welcome screen
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra_icon_embedded.hpp)
    message("Icon file spectra_icon.png already embedded, skipping")
else()
    embed_binary_file(
        INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/icons/spectra_icon.png
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra_icon_embedded.hpp
        VARIABLE SpectraIcon_png
    )
endif()


# Feature flags
option(SPECTRA_USE_GLFW    "Enable GLFW windowing adapter"       ON)
option(SPECTRA_USE_SDL3    "Enable SDL3 windowing adapter"        OFF)
option(SPECTRA_USE_IMGUI   "Enable Dear ImGui menu bar/settings" ON)
option(SPECTRA_USE_QT      "Enable Qt adapter scaffolding"        OFF)
option(SPECTRA_USE_FFMPEG  "Enable video export via ffmpeg pipe"  ON)
option(SPECTRA_USE_EIGEN   "Enable Eigen vector adapters"         ON)
# Developer/integration features default to ON only for top-level builds so
# that add_subdirectory()/FetchContent consumers get just the library.
option(SPECTRA_USE_ROS2    "Enable ROS2 adapter (requires sourced ROS2 workspace)" ${SPECTRA_IS_TOP_LEVEL})
option(SPECTRA_ROS2_BAG    "Enable rosbag2 support (requires rosbag2 packages)"    ON)
option(SPECTRA_USE_PX4     "Enable PX4 ULog adapter (ULog visualizer + MAVLink)"   ${SPECTRA_IS_TOP_LEVEL})
option(SPECTRA_USE_WEBGPU  "Enable WebGPU/wasm rendering backend (requires Dawn or wgpu-native)" OFF)
option(SPECTRA_BUILD_EXAMPLES "Build example programs"            ${SPECTRA_IS_TOP_LEVEL})
option(SPECTRA_BUILD_QT_EXAMPLE "Build the Qt6 embed example"     OFF)
option(SPECTRA_BUILD_TESTS    "Build unit tests and benchmarks"   ${SPECTRA_IS_TOP_LEVEL})
# The multiproc runtime spawns spectra-backend/spectra-window, so the apps are
# built by default even for subproject consumers.
option(SPECTRA_BUILD_APPS     "Build spectra, spectra-backend and spectra-window executables" ON)
option(SPECTRA_INSTALL        "Generate install rules and the spectra CMake package" ON)
option(SPECTRA_PYTHON_WHEEL  "Install backend into Python package _bin/" OFF)

# SPECTRA_USE_SYSTEM_<DEP> switches (system package vs. pinned FetchContent copy)
include(cmake/SpectraDependencyOptions.cmake)

# Linux wheel builds: keep code generation on portable x86_64 ISA.
if(SPECTRA_PYTHON_WHEEL
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_compile_options(-march=x86-64 -mtune=generic)
    message(STATUS "SPECTRA_PYTHON_WHEEL: forcing portable x86_64 ISA (-march=x86-64 -mtune=generic)")
endif()

# Runtime mode: inproc (single-process, all windows in one process)
#               multiproc (future: each window in its own process via IPC)
set(SPECTRA_RUNTIME_MODE "multiproc" CACHE STRING "Runtime mode: inproc | multiproc")
set_property(CACHE SPECTRA_RUNTIME_MODE PROPERTY STRINGS "inproc" "multiproc")

# Vulkan present mode:
#   FIFO      — VSync, stable frame pacing, guaranteed by spec (default)
#   MAILBOX   — Low-latency + tear-free if available, may over-queue on resize
#   IMMEDIATE — No VSync, tearing possible, shows max FPS
set(SPECTRA_PRESENT_MODE "FIFO" CACHE STRING "Vulkan present mode: FIFO | MAILBOX | IMMEDIATE")
set_property(CACHE SPECTRA_PRESENT_MODE PROPERTY STRINGS "FIFO" "MAILBOX" "IMMEDIATE")

# Skip network update checks when FetchContent source dirs already exist (all
# dependencies are pinned to tags, so there is nothing to update).  Only a
# default for top-level builds, never FORCEd, so users and parent projects
# keep control of FetchContent's behaviour.
if(SPECTRA_IS_TOP_LEVEL AND NOT DEFINED FETCHCONTENT_UPDATES_DISCONNECTED)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
        "Do not re-check pinned FetchContent dependencies on every configure")
endif()

message(STATUS "SPECTRA_RUNTIME_MODE: ${SPECTRA_RUNTIME_MODE}")
message(STATUS "SPECTRA_PRESENT_MODE: ${SPECTRA_PRESENT_MODE}")

# Find Vulkan SDK
if(EMSCRIPTEN)
    # Browser builds render through WebGPU only.  App code still references the
    # Vulkan backend types, so compile against the Vulkan headers; the Vulkan
    # entry points resolve to generated stubs (see vulkan_stubs.cpp below).
    if(NOT SPECTRA_USE_WEBGPU)
        message(FATAL_ERROR "Emscripten builds require -DSPECTRA_USE_WEBGPU=ON")
    endif()
    include(FetchContent)
    FetchContent_Declare(
        vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.275
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(vulkan_headers)
    add_library(Vulkan::Vulkan INTERFACE IMPORTED)
    target_link_libraries(Vulkan::Vulkan INTERFACE Vulkan::Headers)
    set(Vulkan_INCLUDE_DIR "${vulkan_headers_SOURCE_DIR}/include")
else()
    find_package(Vulkan REQUIRED)
endif()

# Shader compilation (GLSL -> SPIR-V)
include(cmake/CompileShaders.cmake)

# WGSL shader embedding — always generated when the wgsl source dir exists.
# This allows the WebGPU shader tests to run even when SPECTRA_USE_WEBGPU=OFF
# (e.g. in CI without Dawn), because the embedded sources are plain strings.
set(WGSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/wgsl)
set(WGSL_HEADER     ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_wgsl.hpp)
if(EXISTS ${WGSL_SOURCE_DIR})
    file(GLOB _wgsl_inputs CONFIGURE_DEPENDS "${WGSL_SOURCE_DIR}/*.wgsl")
    if(_wgsl_inputs)
        add_custom_command(
            OUTPUT  ${WGSL_HEADER}
            COMMAND ${CMAKE_COMMAND}
                -DWGSL_SOURCE_DIR=${WGSL_SOURCE_DIR}
                -DOUTPUT_HEADER=${WGSL_HEADER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedWGSLShaders.cmake
            DEPENDS ${_wgsl_inputs}
            COMMENT "Embedding WGSL shader sources into shader_wgsl.hpp"
            VERBATIM
        )
        add_custom_target(spectra_wgsl_shaders DEPENDS ${WGSL_HEADER})
    endif()
endif()

# FlatBuffers schema compilation (IPC protocol v2)
include(cmake/CompileFlatBuffers.cmake)

# Collect source files
file(GLOB_RECURSE SPECTRA_CORE_SOURCES   src/core/*.cpp)
file(GLOB_RECURSE SPECTRA_RENDER_SOURCES src/render/*.cpp)
file(GLOB_RECURSE SPECTRA_PLATFORM_SOURCES src/platform/*.cpp)
file(GLOB_RECURSE SPECTRA_ANIM_SOURCES   src/anim/*.cpp)
file(GLOB_RECURSE SPECTRA_IO_SOURCES     src/io/*.cpp)
file(GLOB_RECURSE SPECTRA_DATA_SOURCES  src/data/*.cpp)
file(GLOB_RECURSE SPECTRA_MATH_SOURCES  src/math/*.cpp)
file(GLOB_RECURSE SPECTRA_IPC_SOURCES   src/ipc/*.cpp)
file(GLOB SPECTRA_DAEMON_SOURCES
    src/daemon/session_graph.cpp
    src/daemon/process_manager.cpp
    src/daemon/figure_model.cpp
    src/daemon/topic_registry.cpp
)

# UI sources (non-ImGui) — guarded so parallel agents don't break each other's builds.
# app.cpp is always present; others are included only if they exist on disk.
file(GLOB SPECTRA_UI_SOURCES src/ui/app/app.cpp src/ui/app/app_inproc.cpp src/ui/app/app_multiproc.cpp src/ui/app/app_step.cpp)
foreach(_ui_src
    src/app/inproc_topic_server.cpp
    src/ui/app/window_ui_context_builder.cpp
    src/ui/app/window_manager_bootstrap.cpp
    src/ui/app/window_ui_context_runtime.cpp
    src/ui/input/input.cpp
    src/ui/input/input_pan_zoom.cpp
    src/ui/input/input_select.cpp
    src/ui/input/input_measure.cpp
    src/ui/input/input_annotate.cpp
    src/ui/layout/layout_manager.cpp
    src/ui/figures/tab_bar.cpp
    src/ui/animation/animation_controller.cpp
    src/ui/input/gesture_recognizer.cpp
    src/ui/animation/transition_engine.cpp
    src/ui/figures/figure_manager.cpp
    src/ui/viewmodel/figure_view_model.cpp
    src/ui/viewmodel/axes_view_model.cpp
    src/ui/viewmodel/series_view_model.cpp
    src/ui/animation/timeline_editor.cpp
    src/ui/animation/recording_export.cpp
    src/ui/docking/split_view.cpp
    src/ui/docking/dock_system.cpp
    src/ui/panel/panel_detach_controller.cpp
    src/ui/shell/panel.cpp
    src/ui/shell/panel_registry.cpp
    src/ui/shell/menu_bar.cpp
    src/ui/shell/nav_rail.cpp
    src/ui/shell/status_bar.cpp
    src/ui/shell/canvas_host.cpp
    src/ui/shell/app_shell.cpp
    src/ui/shell/spectra_app_shell.cpp
    src/ui/shell/shell_style.cpp
    src/ui/data/axis_link.cpp
    src/ui/animation/keyframe_interpolator.cpp
    src/ui/animation/camera_animator.cpp
    src/ui/animation/animation_curve_editor.cpp
    src/ui/animation/mode_transition.cpp
    src/ui/imgui/axes3d_renderer.cpp
    src/ui/window/window_manager.cpp
    src/ui/window/window_glfw_callbacks.cpp
    src/ui/window/window_lifecycle.cpp
    src/ui/window/window_figure_ops.cpp
    src/ui/app/window_runtime.cpp
    src/ui/app/session_runtime.cpp
    src/ui/figures/figure_registry.cpp
    src/ui/data/csv_loader.cpp
    src/ui/data/clipboard_export.cpp
    src/ui/data/html_table_export.cpp
    src/ui/accessibility/sonification.cpp
    src/ui/accessibility/accessible_summary.cpp
    src/ui/native_dialog_policy.cpp
    src/ui/ui_interaction_log.cpp
    src/ui/workspace/figure_serializer.cpp
    src/ui/commands/series_clipboard.cpp
    src/ui/overlay/data_editor.cpp
    src/ui/overlay/custom_transform_dialog.cpp
    src/ui/overlay/plot_overlay_dialog.cpp
    src/ui/plot/plot_annotations.cpp
    src/ui/overlay/overlay_registry.cpp
    src/adapters/data_source_registry.cpp
    src/embed/embed_surface.cpp
    src/embed/spectra_embed_c.cpp
    src/ui/automation/automation_server.cpp
    src/ui/automation/automation_dispatch.cpp
    src/ui/automation/automation_security.cpp
    src/ui/automation/mcp_server.cpp
    src/ui/automation/handlers/handlers_command.cpp
    src/ui/automation/handlers/handlers_input.cpp
    src/ui/automation/handlers/handlers_figure.cpp
    src/ui/automation/handlers/handlers_capture.cpp
    src/ui/automation/handlers/handlers_window.cpp
    src/ui/automation/handlers/handlers_utility.cpp
    src/ui/automation/handlers/handlers_fuzz.cpp
)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_ui_src})
        list(APPEND SPECTRA_UI_SOURCES ${_ui_src})
    endif()
endforeach()

# ─── spectra-core: headless data model library (no Vulkan, no GLFW, no ImGui) ─
# This is a real static/shared library containing the core data model, math,
# data processing, I/O export, animation, and knob manager.  It has zero GPU
# dependencies, making it suitable for headless Python consumers, CI pipelines,
# and embedding scenarios that don't need rendering.

add_library(spectra-core
    ${SPECTRA_CORE_SOURCES}
    ${SPECTRA_ANIM_SOURCES}
    ${SPECTRA_IO_SOURCES}
    ${SPECTRA_DATA_SOURCES}
    ${SPECTRA_MATH_SOURCES}
    src/ui/overlay/knob_manager.cpp
    src/ui/camera/camera.cpp
)

set_target_properties(spectra-core PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(spectra-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/public>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(spectra-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_include_directories(spectra-core SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_compile_definitions(spectra-core PUBLIC
    $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
    SPECTRA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPECTRA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPECTRA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPECTRA_VERSION_STRING="${SPECTRA_VERSION_STRING}"
)

# Windows toolchains require _USE_MATH_DEFINES to expose M_PI, M_E etc. from <cmath>.
if(WIN32)
    target_compile_definitions(spectra-core PUBLIC _USE_MATH_DEFINES)
endif()

# Suppress warnings in third-party headers (stb_image.h tc16 array — GCC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(spectra-core PRIVATE -Wno-error=maybe-uninitialized)
endif()

# ─── spectra: full GPU-accelerated library (rendering + UI + IPC) ─────────────
# Links spectra-core and adds Vulkan rendering, platform windowing, IPC,
# daemon support, and the full ImGui-based UI stack.

add_library(spectra
    ${SPECTRA_RENDER_SOURCES}
    ${SPECTRA_PLATFORM_SOURCES}
    third_party/tinyfiledialogs.cpp
    ${SPECTRA_IPC_SOURCES}
    ${SPECTRA_DAEMON_SOURCES}
    ${SPECTRA_UI_SOURCES}
)

set_target_properties(spectra PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# Public include dirs come transitively from spectra-core.
target_include_directories(spectra PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers
)
target_include_directories(spectra SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
    ${SPECTRA_FLATBUFFERS_INCLUDE_DIRS}
)

# Suppress warnings in third-party headers (stb_image.h tc16 array — GCC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(spectra PRIVATE -Wno-error=maybe-uninitialized)
endif()

target_link_libraries(spectra PUBLIC spectra-core Vulkan::Vulkan)
# dlopen: fontconfig is loaded at run time for fallback-font lookup (font_fallback.cpp).
target_link_libraries(spectra PRIVATE ${CMAKE_DL_LIBS})

target_compile_definitions(spectra PUBLIC
    SPECTRA_RUNTIME_MODE_${SPECTRA_RUNTIME_MODE}
)
message("SPECTRA_VERSION_STRING: ${SPECTRA_VERSION_STRING}")
target_compile_definitions(spectra PRIVATE
    $<$<BOOL:${SPECTRA_USE_EIGEN}>:SPECTRA_USE_EIGEN>
)
target_compile_definitions(spectra PRIVATE SPECTRA_PRESENT_MODE_${SPECTRA_PRESENT_MODE})

# Emscripten: browsers have no Vulkan, so resolve the Vulkan backend's entry
# points with generated stubs that fail cleanly at init.
if(EMSCRIPTEN)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "Emscripten builds need Python 3 to generate Vulkan stubs")
    endif()
    set(_spectra_vk_stubs${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan_stubs.cpp)
    add_custom_command(
        OUTPUT  ${_spectra_vk_stubs}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_vulkan_stubs.py
                ${Vulkan_INCLUDE_DIR}/vulkan/vulkan_core.h ${_spectra_vk_stubs}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_vulkan_stubs.py
                ${Vulkan_INCLUDE_DIR}/vulkan/vulkan_core.h
        COMMENT "Generating Vulkan entry-point stubs for Emscripten"
    )
    target_sources(spectra PRIVATE ${_spectra_vk_stubs})
endif()

# Ensure shaders are compiled before the library
if(TARGET spectra_shaders)
    add_dependencies(spectra spectra_shaders)
endif()

# Ensure FlatBuffers schemas are generated before the library
add_dependencies(spectra spectra_flatbuffers_gen)

# GLFW adapter (optional)
if(SPECTRA_USE_GLFW AND EMSCRIPTEN)
    # Emscripten ships its own browser GLFW implementation.
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE "SHELL:-s USE_GLFW=3")
    set(_spectra_glfw_link glfw)
    target_sources(spectra PRIVATE src/ui/window/glfw_adapter.cpp)
    target_link_libraries(spectra PRIVATE glfw)
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_GLFW)
elseif(SPECTRA_USE_GLFW)
    # Prefer a system GLFW (>= 3.3; 3.4-only features are compile-time
    # guarded) unless SPECTRA_USE_SYSTEM_GLFW=OFF forces the bundled copy.
    set(SPECTRA_GLFW_FROM_SYSTEM OFF)
    if(NOT SPECTRA_USE_SYSTEM_GLFW STREQUAL "OFF")
        if(SPECTRA_USE_SYSTEM_GLFW STREQUAL "ON")
            find_package(glfw3 3.3 CONFIG REQUIRED)
        else()
            find_package(glfw3 3.3 CONFIG QUIET)
        endif()
        if(glfw3_FOUND AND TARGET glfw)
            set(SPECTRA_GLFW_FROM_SYSTEM ON)
        endif()
    endif()

    if(SPECTRA_GLFW_FROM_SYSTEM)
        message(STATUS "GLFW: using system glfw3 ${glfw3_VERSION} (${glfw3_DIR})")
        set(_spectra_glfw_link glfw)
    else()
        message(STATUS "GLFW: building bundled GLFW 3.4 (FetchContent)")
        include(FetchContent)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG        3.4  # 7b6aead9fb88b3623e3b3725ebb42670cbe4c579
            GIT_SHALLOW    TRUE
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        # Don't install GLFW's own headers/libs into the system prefix — they
        # conflict with libglfw3-dev on the user's machine when the spectra .deb
        # is installed.  The install and export of the glfw target is handled in
        # the Install targets section.
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
        # Bundle the GLFW static library in the spectra install so consumers
        # don't need system GLFW.  $<BUILD_INTERFACE:> scopes the build-tree
        # target; $<INSTALL_INTERFACE:> provides the exported name (spectra::glfw)
        # for the installed package.
        set(_spectra_glfw_link "$<BUILD_INTERFACE:glfw>;$<INSTALL_INTERFACE:spectra::glfw>")
    endif()

    target_sources(spectra PRIVATE src/ui/window/glfw_adapter.cpp)
    # GLFW is linked PRIVATE (no public header includes glfw3.h), but the
    # static library has unresolved GLFW symbols.  $<INSTALL_INTERFACE:spectra::glfw>
    # in the PRIVATE interface ensures the exported target carries the GLFW
    # dependency for consumers.
    target_link_libraries(spectra PRIVATE ${_spectra_glfw_link})
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_GLFW)
    if(WIN32)
        target_sources(spectra PRIVATE src/platform/window_system/win32_glfw_hook.cpp)
        target_link_libraries(spectra PRIVATE comctl32)
    endif()
endif()

# SDL3 adapter (optional, mutually exclusive with GLFW)
if(SPECTRA_USE_GLFW AND SPECTRA_USE_SDL3)
    message(FATAL_ERROR "SPECTRA_USE_GLFW and SPECTRA_USE_SDL3 are mutually exclusive. "
                        "Enable only one windowing backend at a time.")
endif()

if(SPECTRA_USE_SDL3)
    set(SDL3_FOUND FALSE)
    if(SPECTRA_USE_SYSTEM_SDL3 STREQUAL "ON")
        find_package(SDL3 3.2 CONFIG REQUIRED)
    elseif(SPECTRA_USE_SYSTEM_SDL3 STREQUAL "AUTO")
        find_package(SDL3 3.2 CONFIG QUIET)
    endif()
    set(SPECTRA_SDL3_FROM_SYSTEM ${SDL3_FOUND})
    if(SDL3_FOUND AND TARGET SDL3::SDL3-static)
        set(SPECTRA_SDL3_TARGET SDL3::SDL3-static)
    elseif(SDL3_FOUND)
        # Distro packages usually ship only the shared library.
        set(SPECTRA_SDL3_TARGET SDL3::SDL3)
    else()
        set(SPECTRA_SDL3_TARGET SDL3::SDL3-static)
    endif()
    if(NOT SDL3_FOUND)
        include(FetchContent)
        # Fetched over git (pinned tag) rather than a release tarball; some
        # proxies block github.com release downloads but allow git.
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        release-3.2.0  # 535d80badefc83c5c527ec5748f2a20d6a9310fe
            GIT_SHALLOW    TRUE
        )
        set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
        set(SDL_TESTS   OFF CACHE BOOL "" FORCE)
        set(SDL_VULKAN  ON  CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
        # Build SDL3 static lib with -fPIC so it can be linked into shared libraries
        # (e.g. libspectra_embed.so, Python extension modules, Qt adapters).
        if(TARGET SDL3-static)
            set_target_properties(SDL3-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()

    target_sources(spectra PRIVATE
        src/ui/window/sdl3_adapter.cpp
        src/ui/window/window_sdl3_events.cpp
        src/platform/window_system/sdl3_surface_host.cpp
    )
    # Link PRIVATE: SDL3 is an implementation detail; don't export to downstream consumers.
    target_link_libraries(spectra PRIVATE ${SPECTRA_SDL3_TARGET})
    target_include_directories(spectra PRIVATE
        $<TARGET_PROPERTY:${SPECTRA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_SDL3)

    # FetchContent produces real (non-IMPORTED) targets. CMake requires every target in
    # the dependency chain of an exported target to also be in the export set.
    # SDL3-static links SDL3_Headers (interface). We add both so the export validates.
    # IMPORTED targets from find_package don't have this restriction.
    if(NOT SDL3_FOUND)
        set(_sdl3_export_targets SDL3-static)
        foreach(_t SDL3_Headers SDL3main)
            if(TARGET ${_t})
                list(APPEND _sdl3_export_targets ${_t})
            endif()
        endforeach()
        install(TARGETS ${_sdl3_export_targets} EXPORT spectraTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
endif()

# Qt adapter (optional)
if(SPECTRA_USE_QT)
    find_package(Qt6 COMPONENTS Gui REQUIRED)

    add_library(spectra_qt_adapter
        src/adapters/qt/qt_runtime.cpp
        src/adapters/qt/qt_surface_host.cpp
    )
    target_link_libraries(spectra_qt_adapter PUBLIC spectra Qt6::Gui)
    target_compile_features(spectra_qt_adapter PRIVATE cxx_std_20)
    target_include_directories(spectra_qt_adapter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_compile_definitions(spectra_qt_adapter PUBLIC SPECTRA_USE_QT)
endif()

# Dear ImGui (optional — requires GLFW or SDL3)
if(SPECTRA_USE_IMGUI AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3))
    include(FetchContent)
    # Fetched over git rather than as a release archive: some proxies block
    # github.com/.../archive downloads but allow git. For offline builds, point
    # FETCHCONTENT_SOURCE_DIR_IMGUI at a local checkout of the same tag.
    # v1.92.6-docking = commit 2a1b69f05748ad909f03acf4533447cac1331611
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.6-docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # ImGui core (always present when ImGui is enabled)
    if(SPECTRA_USE_GLFW)
        set(_imgui_window_backend_src ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    elseif(SPECTRA_USE_SDL3)
        set(_imgui_window_backend_src ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
    endif()
    target_sources(spectra PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${_imgui_window_backend_src}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        src/ui/imgui/imgui_integration.cpp
        src/ui/imgui/imgui_command_bar.cpp
        src/ui/imgui/imgui_panels.cpp
        src/ui/imgui/imgui_pane_tabs.cpp
        src/ui/imgui/imgui_animation.cpp
        src/ui/imgui/imgui_selection.cpp
        src/ui/imgui/imgui_dialogs.cpp
        src/ui/imgui/imgui_preview.cpp
    )

    # GCC 14 warns on an upstream ImGui backend memset pattern; keep -Werror for
    # project code while relaxing this one third-party source file.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-error=class-memaccess"
        )
    endif()

    # ImGui-dependent UI sources — guarded so parallel agents don't break the build.
    # When adding a new .cpp that depends on ImGui, add an if(EXISTS) block here.
    foreach(_imgui_src
        src/ui/theme/theme.cpp
        src/ui/theme/theme_api.cpp
        src/ui/theme/icons.cpp
        src/ui/imgui/widgets.cpp
        src/ui/overlay/inspector.cpp
        src/ui/overlay/tooltip.cpp
        src/ui/overlay/crosshair.cpp
        src/ui/overlay/data_marker.cpp
        src/ui/overlay/annotation.cpp
        src/ui/overlay/data_interaction.cpp
        src/ui/input/region_select.cpp
        src/ui/overlay/legend_interaction.cpp
        src/ui/input/box_zoom_overlay.cpp
        src/ui/commands/command_palette.cpp
        src/ui/commands/command_registry.cpp
        src/ui/imgui/axes_context_menu_model.cpp
        src/ui/commands/welcome_actions.cpp
        src/ui/commands/shortcut_manager.cpp
        src/ui/commands/undo_manager.cpp
        src/ui/workspace/workspace.cpp
        src/ui/workspace/workspace_autosave.cpp
        src/ui/commands/shortcut_config.cpp
        src/ui/workspace/plugin_api.cpp
        src/ui/workspace/plugin_guard.cpp
        src/ui/workspace/plugin_manifest.cpp
        src/ui/figures/tab_drag_controller.cpp
        src/ui/app/register_commands.cpp
        src/ui/app/commands/command_descriptor.cpp
        src/ui/app/commands/commands_view.cpp
        src/ui/app/commands/commands_edit.cpp
        src/ui/app/commands/commands_file.cpp
        src/ui/app/commands/commands_figure.cpp
        src/ui/app/commands/commands_series.cpp
        src/ui/app/commands/commands_animation.cpp
        src/ui/app/commands/commands_panel.cpp
        src/ui/app/commands/commands_theme.cpp
        src/ui/app/commands/commands_tools.cpp
        src/ui/app/commands/commands_plot.cpp
        src/ui/app/commands/commands_data.cpp
        src/ui/app/commands/commands_app.cpp
        src/ui/app/commands/plugin_export_commands.cpp
        src/ui/topics/topics_panel.cpp
        src/ui/settings/settings_store.cpp
        src/ui/settings/settings_panel.cpp
    )
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_imgui_src})
            target_sources(spectra PRIVATE ${_imgui_src})
        endif() 
    endforeach()
    target_include_directories(spectra PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    if(SPECTRA_USE_SDL3)
        target_include_directories(spectra PRIVATE
            $<TARGET_PROPERTY:${SPECTRA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
        )
    endif()
    # 32-bit ImWchar: code points above U+FFFF (emoji, maths alphanumerics,
    # CJK extension B) are decoded instead of shown as U+FFFD.  PUBLIC so every
    # translation unit that includes imgui.h agrees on sizeof(ImWchar).
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_IMGUI IMGUI_USE_WCHAR32)
endif()

# ROS2 adapter (optional) — all ROS2 build logic lives in src/adapters/ros2/CMakeLists.txt
add_subdirectory(src/adapters/ros2)
# Humble ${std_msgs_TARGETS} genexes reference fastcdr at generate time when tests
# link spectra_ros2_adapter. find_package in the subdirectory alone is not always
# visible to add_subdirectory(tests); re-find in parent scope when ROS2 is enabled.
if(SPECTRA_USE_ROS2)
    find_package(fastcdr REQUIRED)
    find_package(fastrtps QUIET)
endif()
# Eigen adapter (optional)
if(SPECTRA_USE_EIGEN)
    find_package(Eigen3 QUIET)
    if(NOT Eigen3_FOUND)
        message(STATUS "Eigen3 not found — disabling Eigen adapters (SPECTRA_USE_EIGEN=OFF)")
        set(SPECTRA_USE_EIGEN OFF)
    else()
        target_link_libraries(spectra PUBLIC Eigen3::Eigen)
        target_compile_definitions(spectra PUBLIC SPECTRA_USE_EIGEN)
    endif()
endif()

# ─── WebGPU backend (optional) ───────────────────────────────────────────────
# Implements the Backend interface using the WebGPU C API.  Supports native
# builds with Google Dawn and Emscripten/wasm browser targets.
#
# Usage:
#   Native (Dawn):     cmake -DSPECTRA_USE_WEBGPU=ON -Ddawn_DIR=<dawn_install>/lib/cmake/dawn
#   Emscripten/wasm:   emcmake cmake -DSPECTRA_USE_WEBGPU=ON
#
# The WebGPU backend sources are conditionally compiled into the main spectra
# library when this option is ON.  No extra library target is created — the
# backend is selected at runtime by the application.
#
# WGSL shader embedding is always generated (unconditionally, above) so the
# shader content tests can run even when SPECTRA_USE_WEBGPU=OFF.
if(SPECTRA_USE_WEBGPU)
    # ── WebGPU / Dawn dependency ─────────────────────────────────────────────
    if(NOT EMSCRIPTEN)
        # Native build: require Dawn (Google's WebGPU implementation).
        # Users must set dawn_DIR or DAWN_INSTALL_DIR.
        find_package(Dawn QUIET)
        if(NOT Dawn_FOUND)
            # Try finding via pkg-config as fallback.
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(DAWN QUIET dawn webgpu_dawn)
            endif()
        endif()

        if(Dawn_FOUND OR DAWN_FOUND)
            message(STATUS "WebGPU backend enabled (Dawn found)")
        else()
            message(STATUS
                "Dawn not found — WebGPU backend enabled but you must link "
                "webgpu_dawn manually (set Dawn_DIR or DAWN_INSTALL_DIR).")
        endif()
    else()
        message(STATUS "WebGPU backend enabled (Emscripten target)")
    endif()

    # ── Add WebGPU sources to the spectra library ────────────────────────────
    target_sources(spectra PRIVATE
        src/render/webgpu/wgpu_backend.cpp
    )
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_WEBGPU)

    # Ensure WGSL shaders are generated before building the backend.
    if(TARGET spectra_wgsl_shaders)
        add_dependencies(spectra spectra_wgsl_shaders)
    endif()

    # Link Dawn when found natively.
    if(NOT EMSCRIPTEN)
        if(TARGET dawn::webgpu_dawn)
            target_link_libraries(spectra PUBLIC dawn::webgpu_dawn)
        elseif(TARGET webgpu_dawn)
            target_link_libraries(spectra PUBLIC webgpu_dawn)
        elseif(DAWN_LIBRARIES)
            target_link_libraries(spectra PUBLIC ${DAWN_LIBRARIES})
            if(DAWN_INCLUDE_DIRS)
                target_include_directories(spectra PUBLIC ${DAWN_INCLUDE_DIRS})
            endif()
        endif()
    else()
        # Emscripten 4.0.10+ replaced -sUSE_WEBGPU with the emdawnwebgpu port,
        # which tracks the current webgpu.h (WGPUStringView, callback infos).
        target_link_options(spectra PUBLIC "--use-port=emdawnwebgpu")
        target_compile_options(spectra PUBLIC "--use-port=emdawnwebgpu")
        # The backend blocks on adapter/device/map callbacks; ASYNCIFY lets
        # those waits yield to the browser event loop.
        target_link_options(spectra PUBLIC "SHELL:-s ASYNCIFY=1" "SHELL:-s ALLOW_MEMORY_GROWTH=1")
    endif()

    message(STATUS "SPECTRA_USE_WEBGPU: ON (inproc mode only; IPC/multiproc not supported on wasm)")
endif()

# PX4 / ULog adapter (optional) — standalone flight log viewer + MAVLink bridge.
# No external dependencies beyond POSIX sockets — always buildable.
if(SPECTRA_USE_PX4)
    add_library(spectra_px4_adapter
        src/adapters/px4/px4_adapter.cpp
        src/adapters/px4/ulog_reader.cpp
        src/adapters/px4/px4_bridge.cpp
        src/adapters/px4/px4_plot_manager.cpp
        src/adapters/px4/px4_app_shell.cpp
        src/adapters/px4/px4_app_shell_menus.cpp
        src/adapters/px4/px4_recent_files.cpp
        src/adapters/px4/ui/ulog_file_panel.cpp
        src/adapters/px4/ui/live_connection_panel.cpp
        src/adapters/px4/ui/px4_panel_helpers.cpp
    )

    target_link_libraries(spectra_px4_adapter PUBLIC spectra)
    target_compile_features(spectra_px4_adapter PRIVATE cxx_std_20)

    target_include_directories(spectra_px4_adapter PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4
    )
    target_include_directories(spectra_px4_adapter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_compile_definitions(spectra_px4_adapter PUBLIC SPECTRA_USE_PX4)

    if(WIN32)
        target_link_libraries(spectra_px4_adapter PRIVATE ws2_32)
    endif()

    # Forward ImGui include paths so UI panels can find imgui.h.
    if(SPECTRA_USE_IMGUI AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3) AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(spectra_px4_adapter PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()

    # spectra-px4 standalone executable
    if(SPECTRA_BUILD_APPS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4/main.cpp)
        add_executable(spectra-px4
            src/adapters/px4/main.cpp
        )
        target_link_libraries(spectra-px4 PRIVATE spectra_px4_adapter)
        target_compile_features(spectra-px4 PRIVATE cxx_std_20)
        target_include_directories(spectra-px4 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
    endif()

    message(STATUS "PX4/ULog adapter enabled")
endif()

# ─── Convenience INTERFACE targets ────────────────────────────────────────────
# spectra-core is now a real library (defined above).  These remaining
# INTERFACE targets are thin aliases for backward compatibility.

add_library(spectra-ipc INTERFACE)
target_link_libraries(spectra-ipc INTERFACE spectra)

add_library(spectra-render INTERFACE)
target_link_libraries(spectra-render INTERFACE spectra)

# ─── Shared library for embedding (Python ctypes, C FFI) ────────────────────
# Build with: cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..
option(SPECTRA_BUILD_EMBED_SHARED "Build libspectra_embed shared library for FFI" OFF)
if(SPECTRA_BUILD_EMBED_SHARED)
    add_library(spectra_embed SHARED
        src/embed/embed_surface.cpp
        src/embed/spectra_embed_c.cpp
    )
    target_link_libraries(spectra_embed PRIVATE spectra)
    target_compile_features(spectra_embed PRIVATE cxx_std_20)
    # Consumers of the C FFI only need include/spectra/spectra_embed_c.h.
    target_include_directories(spectra_embed INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_include_directories(spectra_embed PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    # When ImGui is enabled, the embed surface uses it for full UI rendering.
    # Forward ImGui include paths so embed_surface.cpp can find imgui headers.
    if(SPECTRA_USE_IMGUI AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3) AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(spectra_embed PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()
    set_target_properties(spectra_embed PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME spectra_embed
    )
endif()

# spectra — main application executable (welcome screen, no initial figure)
if(SPECTRA_BUILD_APPS AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3)
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp)
    add_executable(spectra-app src/app/main.cpp)
    target_link_libraries(spectra-app PRIVATE spectra)
    # glfw is PRIVATE to spectra, so its include directories don't propagate;
    # link directly so <GLFW/glfw3.h> resolves in app/main.cpp.
    if(SPECTRA_USE_GLFW AND DEFINED _spectra_glfw_link)
        target_link_libraries(spectra-app PRIVATE ${_spectra_glfw_link})
    endif()
    target_compile_features(spectra-app PRIVATE cxx_std_20)
    target_include_directories(spectra-app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    set_target_properties(spectra-app PROPERTIES OUTPUT_NAME spectra)
endif()

# Multi-process daemon + agent binaries (Phase 4 & 5)
if(SPECTRA_BUILD_APPS AND SPECTRA_RUNTIME_MODE STREQUAL "multiproc")
    # spectra-backend daemon
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/daemon/main.cpp)
        add_executable(spectra-backend
            src/daemon/main.cpp
            src/daemon/daemon_server.cpp
            src/daemon/agent_message_handler.cpp
            src/daemon/python_message_handler.cpp
            src/daemon/topic_message_handler.cpp
            src/daemon/topic_registry.cpp
            src/daemon/heartbeat_monitor.cpp
            src/daemon/session_graph.cpp
            src/daemon/process_manager.cpp
            src/daemon/figure_model.cpp
        )
        target_link_libraries(spectra-backend PRIVATE spectra)
        target_compile_features(spectra-backend PRIVATE cxx_std_20)
        target_include_directories(spectra-backend PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
    endif()

    # spectra-window agent — uses the SAME UI stack as inproc
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/agent/main.cpp)
        add_executable(spectra-window
            src/agent/main.cpp
        )
        target_link_libraries(spectra-window PRIVATE spectra)
        # glfw is PRIVATE to the spectra library target, so its include
        # directories don't propagate here — link directly so the GLFW/glfw3.h
        # include in agent/main.cpp resolves.
        if(SPECTRA_USE_GLFW AND DEFINED _spectra_glfw_link)
            target_link_libraries(spectra-window PRIVATE ${_spectra_glfw_link})
        endif()
        target_compile_features(spectra-window PRIVATE cxx_std_20)
        target_include_directories(spectra-window PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
        if(SPECTRA_USE_IMGUI)
            target_include_directories(spectra-window PRIVATE
                ${imgui_SOURCE_DIR}
                ${imgui_SOURCE_DIR}/backends
            )
            if(SPECTRA_USE_SDL3)
                target_include_directories(spectra-window PRIVATE
                    $<TARGET_PROPERTY:${SPECTRA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
                )
            endif()
        endif()
    endif()
endif()

# ─── Install targets ─────────────────────────────────────────────────────────
include(CMakePackageConfigHelpers)

set(SPECTRA_CMAKE_PACKAGE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/spectra)

# Every target that is part of the spectra:: package.
set(_spectra_export_targets spectra-core spectra)
if(TARGET spectra_embed)
    list(APPEND _spectra_export_targets spectra_embed)
endif()
# When GLFW was built from FetchContent it is a real target in this project and
# must be part of the export set (consumers see it as spectra::glfw).  A system
# GLFW is an imported target and is re-found by spectraConfig.cmake instead.
if(SPECTRA_USE_GLFW AND NOT EMSCRIPTEN AND NOT SPECTRA_GLFW_FROM_SYSTEM AND TARGET glfw)
    set(_spectra_bundled_glfw TRUE)
else()
    set(_spectra_bundled_glfw FALSE)
endif()

if(SPECTRA_INSTALL)

# Library
install(TARGETS ${_spectra_export_targets} EXPORT spectraTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(_spectra_bundled_glfw)
    install(TARGETS glfw EXPORT spectraTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/spectra
    )
endif()

# Public headers (C++ *.hpp and the C FFI header spectra_embed_c.h).  The
# Eigen adapter headers are only usable when Spectra was built with Eigen.
set(_spectra_header_excludes)
if(NOT SPECTRA_USE_EIGEN)
    list(APPEND _spectra_header_excludes
        PATTERN "eigen.hpp" EXCLUDE
        PATTERN "eigen_easy.hpp" EXCLUDE)
endif()
install(DIRECTORY include/spectra/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h"
        ${_spectra_header_excludes}
)

# Generated version header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/public/spectra/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra
)

# Main application binary (inproc mode)
if(TARGET spectra-app)
    install(TARGETS spectra-app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Multiproc binaries
if(SPECTRA_RUNTIME_MODE STREQUAL "multiproc" AND NOT SPECTRA_PYTHON_WHEEL)
    if(TARGET spectra-backend)
        install(TARGETS spectra-backend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(TARGET spectra-window)
        install(TARGETS spectra-window RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# ROS2 adapter deployment (only when built with SPECTRA_USE_ROS2=ON)
if(TARGET spectra_ros2_adapter)
    install(TARGETS spectra_ros2_adapter
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TARGET spectra-ros)
    install(TARGETS spectra-ros RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(EXISTS ${PROJECT_SOURCE_DIR}/sessions/presets)
        install(DIRECTORY sessions/presets/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/ros/sessions/presets
            FILES_MATCHING PATTERN "*.spectra-ros-session"
        )
    endif()
    if(EXISTS ${PROJECT_SOURCE_DIR}/examples/ros2_launch)
        install(DIRECTORY examples/ros2_launch/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/ros/launch
            FILES_MATCHING PATTERN "*.launch.py"
        )
    endif()
endif()

# PX4 adapter deployment (only when built with SPECTRA_USE_PX4=ON)
if(TARGET spectra_px4_adapter)
    install(TARGETS spectra_px4_adapter
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TARGET spectra-px4)
    install(TARGETS spectra-px4 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

endif() # SPECTRA_INSTALL

# Python wheel: bundle the backend daemon AND the window agent it spawns
# (src/daemon/main.cpp resolves spectra-window next to its own binary) into the
# Python package. Both go into the dedicated "python_wheel" install component;
# pyproject.toml sets install.components = ["python_wheel"], so the wheel gets
# only these two binaries and none of the dev/system install (headers, static
# libs, CMake config, man page, desktop file, fonts, inproc app).
if(SPECTRA_PYTHON_WHEEL)
    foreach(_spectra_wheel_bin spectra-backend spectra-window)
        if(TARGET ${_spectra_wheel_bin})
            install(TARGETS ${_spectra_wheel_bin}
                RUNTIME DESTINATION spectra/_bin
                COMPONENT python_wheel
            )
        endif()
    endforeach()
    # Redistribution notices for the bundled binaries (ImGui, GLFW, FlatBuffers, fonts).
    install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION spectra/licenses
        COMPONENT python_wheel
    )
    install(DIRECTORY licenses/
        DESTINATION spectra/licenses
        COMPONENT python_wheel
    )
endif()

if(SPECTRA_INSTALL)

# Data assets (fonts, icons)
install(FILES
    third_party/Inter-Regular.ttf
    third_party/SpectraIcons.ttf
    third_party/SpectraIcons.otf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/fonts
)
if(SPECTRA_HICOLOR_ICONS_GENERATED)
    install(DIRECTORY ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        FILES_MATCHING PATTERN "spectra.png"
    )
else()
    # No Pillow: fall back to the committed full-size icon (desktop
    # environments also look up Icon=spectra in share/pixmaps/).
    install(FILES icons/spectra_icon.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
        RENAME spectra.png
    )
endif()
install(FILES icons/spectra.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Man page
install(FILES docs/man/spectra-backend.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

# Shell completions
install(FILES packaging/completions/spectra-backend.bash
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
    RENAME spectra-backend
)
install(FILES packaging/completions/spectra-backend.zsh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions
    RENAME _spectra-backend
)
install(FILES packaging/completions/spectra-backend.fish
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d
    RENAME spectra-backend.fish
)

# License, third-party notices and the full third-party license texts.  They
# land in every install and therefore in every CPack package (.deb/.rpm/.tgz).
install(FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(DIRECTORY licenses/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
)

# CMake package config (find_package(spectra) support)
install(EXPORT spectraTargets
    FILE spectraTargets.cmake
    NAMESPACE spectra::
    DESTINATION ${SPECTRA_CMAKE_PACKAGE_DIR}
)

# Build-tree package: find_package(spectra) also works with
# -Dspectra_DIR=<build dir>, without an install step.
# Skipped with a bundled (FetchContent) SDL3: SDL writes its own build-tree
# export files (SDL3headersTargets.cmake, ...), and exporting SDL3_Headers here
# as well puts it in two build-tree export sets, which CMake rejects at
# generate time.  The installed package is unaffected.
if(SPECTRA_USE_SDL3 AND NOT SPECTRA_SDL3_FROM_SYSTEM)
    message(STATUS "Bundled SDL3: build-tree spectraTargets.cmake export disabled "
                   "(use the installed package)")
else()
    export(EXPORT spectraTargets
        FILE ${CMAKE_CURRENT_BINARY_DIR}/spectraTargets.cmake
        NAMESPACE spectra::
    )
endif()

endif() # SPECTRA_INSTALL

# Values substituted into spectraConfig.cmake so it only looks for the
# dependencies this particular build actually used.
set(SPECTRA_CONFIG_USE_EIGEN ${SPECTRA_USE_EIGEN})
set(SPECTRA_CONFIG_SYSTEM_GLFW OFF)
if(SPECTRA_USE_GLFW AND SPECTRA_GLFW_FROM_SYSTEM)
    set(SPECTRA_CONFIG_SYSTEM_GLFW ON)
endif()
set(SPECTRA_CONFIG_SYSTEM_SDL3 OFF)
if(SPECTRA_USE_SDL3 AND SPECTRA_SDL3_FROM_SYSTEM)
    set(SPECTRA_CONFIG_SYSTEM_SDL3 ON)
endif()

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/spectraConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    INSTALL_DESTINATION ${SPECTRA_CMAKE_PACKAGE_DIR}
)

# While the major version is 0, minor releases may break the API.
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(_spectra_version_compat SameMinorVersion)
else()
    set(_spectra_version_compat SameMajorVersion)
endif()
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ${_spectra_version_compat}
)

if(SPECTRA_INSTALL)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
        DESTINATION ${SPECTRA_CMAKE_PACKAGE_DIR}
    )
endif()

# Namespaced aliases so add_subdirectory()/FetchContent consumers can use the
# same target names as find_package(spectra) consumers.
add_library(spectra::spectra ALIAS spectra)
add_library(spectra::spectra-core ALIAS spectra-core)
if(TARGET spectra_embed)
    add_library(spectra::spectra_embed ALIAS spectra_embed)
endif()

# ─── CPack ───────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "spectra")
set(CPACK_PACKAGE_VENDOR "spectra-plot")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPU-accelerated scientific plotting library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "danlil240")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README  ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

# Debian
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# SHLIBDEPS only scans executables and shared libraries, not static
# libraries.  With the bundled GLFW (SPECTRA_USE_SYSTEM_GLFW=OFF, recommended
# for release packages) libspectra.a's GLFW symbols are satisfied by the
# installed spectra::glfw archive; with a system GLFW, find_package(spectra)
# consumers need its development package.  Eigen3 is re-found by
# spectraConfig.cmake only when Spectra was built with it.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1 (>= 1.3.204.0)")
set(_spectra_deb_recommends)
if(SPECTRA_CONFIG_SYSTEM_GLFW)
    list(APPEND _spectra_deb_recommends "libglfw3-dev")
endif()
if(SPECTRA_USE_EIGEN)
    list(APPEND _spectra_deb_recommends "libeigen3-dev")
endif()
string(REPLACE ";" ", " CPACK_DEBIAN_PACKAGE_RECOMMENDS "${_spectra_deb_recommends}")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libvulkan-dev, pkg-config, cmake")
if(SPECTRA_PKG_DISTRO_CODENAME)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1~${SPECTRA_PKG_DISTRO_CODENAME}")
endif()

# RPM
set(CPACK_RPM_PACKAGE_REQUIRES "vulkan-loader >= 1.2")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Science")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

# Only a top-level build owns packaging; a parent project keeps its own CPack.
if(SPECTRA_IS_TOP_LEVEL)
    include(CPack)
endif()

# ─── Packaging manifests (Homebrew / Scoop / AUR / Doxyfile version) ────────
# Regenerates packaging/* and docs/Doxyfile's PROJECT_NUMBER from version.txt
# (see tools/update_packaging.sh; the release job passes the SHA256 values).
if(SPECTRA_IS_TOP_LEVEL)
    add_custom_target(spectra_update_packaging
        COMMAND ${CMAKE_COMMAND} -DSPECTRA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/UpdatePackaging.cmake
        COMMENT "Updating packaging manifests from version.txt"
        VERBATIM
    )
endif()

# ─── Examples ────────────────────────────────────────────────────────────────
if(SPECTRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ─── Tests ───────────────────────────────────────────────────────────────────
if(SPECTRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    # Packaging manifests (Homebrew/Scoop/AUR/Doxyfile) must track version.txt.
    add_test(NAME packaging_manifests_match_version
        COMMAND ${CMAKE_COMMAND} -DSPECTRA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DSPECTRA_CHECK=ON -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/UpdatePackaging.cmake)
    set_tests_properties(packaging_manifests_match_version PROPERTIES LABELS "packaging")
endif()
