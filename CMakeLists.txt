cmake_minimum_required(VERSION 3.20)

# Read version from single source-of-truth
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/version.txt _ver_raw)
string(STRIP "${_ver_raw}" SPECTRA_VERSION)

project(spectra VERSION ${SPECTRA_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra/version.hpp
    @ONLY
)

# Embed font data into C arrays (eliminates runtime file dependency)
include(cmake/EmbedAssets.cmake)
embed_binary_file(
    INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Inter-Regular.ttf
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp
    VARIABLE InterFont_ttf
)

# Feature flags
option(SPECTRA_USE_GLFW    "Enable GLFW windowing adapter"       ON)
option(SPECTRA_USE_IMGUI   "Enable Dear ImGui menu bar/settings" ON)
option(SPECTRA_USE_FFMPEG  "Enable video export via ffmpeg pipe"  OFF)
option(SPECTRA_USE_EIGEN   "Enable Eigen vector adapters"         OFF)
option(SPECTRA_BUILD_EXAMPLES "Build example programs"            ON)
option(SPECTRA_BUILD_TESTS    "Build unit tests and benchmarks"   ON)
option(SPECTRA_PYTHON_WHEEL  "Install backend into Python package _bin/" OFF)

# Runtime mode: inproc (single-process, all windows in one process)
#               multiproc (future: each window in its own process via IPC)
set(SPECTRA_RUNTIME_MODE "multiproc" CACHE STRING "Runtime mode: inproc | multiproc")
set_property(CACHE SPECTRA_RUNTIME_MODE PROPERTY STRINGS "inproc" "multiproc")

# Skip git network checks when FetchContent source dirs already exist.
# On a clean checkout these are absent so the download still happens normally.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

message(STATUS "SPECTRA_RUNTIME_MODE: ${SPECTRA_RUNTIME_MODE}")

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

# Shader compilation (GLSL -> SPIR-V)
include(cmake/CompileShaders.cmake)

# Collect source files
file(GLOB_RECURSE SPECTRA_CORE_SOURCES   src/core/*.cpp)
file(GLOB_RECURSE SPECTRA_RENDER_SOURCES src/render/*.cpp)
file(GLOB_RECURSE SPECTRA_ANIM_SOURCES   src/anim/*.cpp)
file(GLOB_RECURSE SPECTRA_IO_SOURCES     src/io/*.cpp)
file(GLOB_RECURSE SPECTRA_DATA_SOURCES  src/data/*.cpp)
file(GLOB_RECURSE SPECTRA_MATH_SOURCES  src/math/*.cpp)
file(GLOB_RECURSE SPECTRA_IPC_SOURCES   src/ipc/*.cpp)
file(GLOB SPECTRA_DAEMON_SOURCES
    src/daemon/session_graph.cpp
    src/daemon/process_manager.cpp
    src/daemon/figure_model.cpp
)

# UI sources (non-ImGui) — guarded so parallel agents don't break each other's builds.
# app.cpp is always present; others are included only if they exist on disk.
file(GLOB SPECTRA_UI_SOURCES src/ui/app/app.cpp src/ui/app/app_inproc.cpp src/ui/app/app_multiproc.cpp src/ui/app/app_step.cpp)
foreach(_ui_src
    src/ui/input/input.cpp
    src/ui/layout/layout_manager.cpp
    src/ui/figures/tab_bar.cpp
    src/ui/animation/animation_controller.cpp
    src/ui/input/gesture_recognizer.cpp
    src/ui/animation/transition_engine.cpp
    src/ui/figures/figure_manager.cpp
    src/ui/animation/timeline_editor.cpp
    src/ui/animation/recording_export.cpp
    src/ui/docking/split_view.cpp
    src/ui/docking/dock_system.cpp
    src/ui/data/axis_link.cpp
    src/ui/animation/keyframe_interpolator.cpp
    src/ui/animation/camera_animator.cpp
    src/ui/animation/animation_curve_editor.cpp
    src/ui/animation/mode_transition.cpp
    src/ui/overlay/knob_manager.cpp
    src/ui/camera/camera.cpp
    src/ui/imgui/axes3d_renderer.cpp
    src/ui/window/window_manager.cpp
    src/ui/app/window_runtime.cpp
    src/ui/app/session_runtime.cpp
    src/ui/figures/figure_registry.cpp
    src/ui/data/csv_loader.cpp
    src/ui/workspace/figure_serializer.cpp
    src/ui/commands/series_clipboard.cpp
)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_ui_src})
        list(APPEND SPECTRA_UI_SOURCES ${_ui_src})
    endif()
endforeach()

# Core library (headless-capable)
# Use -DBUILD_SHARED_LIBS=ON to build as shared library (libspectra.so)
add_library(spectra
    ${SPECTRA_CORE_SOURCES}
    ${SPECTRA_RENDER_SOURCES}
    third_party/tinyfiledialogs.cpp
    ${SPECTRA_ANIM_SOURCES}
    ${SPECTRA_IO_SOURCES}
    ${SPECTRA_DATA_SOURCES}
    ${SPECTRA_MATH_SOURCES}
    ${SPECTRA_IPC_SOURCES}
    ${SPECTRA_DAEMON_SOURCES}
    ${SPECTRA_UI_SOURCES}
)

set_target_properties(spectra PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(spectra PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(spectra PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Suppress warnings in third-party headers (stb_image.h tc16 array — GCC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(spectra PRIVATE -Wno-error=maybe-uninitialized)
endif()

target_link_libraries(spectra PUBLIC Vulkan::Vulkan)

target_compile_definitions(spectra PRIVATE
    $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
    $<$<BOOL:${SPECTRA_USE_EIGEN}>:SPECTRA_USE_EIGEN>
)
target_compile_definitions(spectra PUBLIC SPECTRA_RUNTIME_MODE_${SPECTRA_RUNTIME_MODE})

# MSVC requires _USE_MATH_DEFINES to expose M_PI, M_E etc. from <cmath>.
# PUBLIC so examples and tests also get it (transitive via linking spectra).
if(MSVC)
    target_compile_definitions(spectra PUBLIC _USE_MATH_DEFINES)
endif()

# Ensure shaders are compiled before the library
if(TARGET spectra_shaders)
    add_dependencies(spectra spectra_shaders)
endif()

# GLFW adapter (optional)
if(SPECTRA_USE_GLFW)
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG        3.4
            GIT_SHALLOW    TRUE
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    endif()

    target_sources(spectra PRIVATE src/ui/window/glfw_adapter.cpp)
    target_link_libraries(spectra PUBLIC glfw)
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_GLFW)
endif()

# Dear ImGui (optional — requires GLFW)
if(SPECTRA_USE_IMGUI AND SPECTRA_USE_GLFW)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # ImGui core (always present when ImGui is enabled)
    target_sources(spectra PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        src/ui/imgui/imgui_integration.cpp
    )

    # ImGui-dependent UI sources — guarded so parallel agents don't break the build.
    # When adding a new .cpp that depends on ImGui, add an if(EXISTS) block here.
    foreach(_imgui_src
        src/ui/theme/theme.cpp
        src/ui/theme/icons.cpp
        src/ui/imgui/widgets.cpp
        src/ui/overlay/inspector.cpp
        src/ui/overlay/tooltip.cpp
        src/ui/overlay/crosshair.cpp
        src/ui/overlay/data_marker.cpp
        src/ui/overlay/data_interaction.cpp
        src/ui/input/region_select.cpp
        src/ui/overlay/legend_interaction.cpp
        src/ui/input/box_zoom_overlay.cpp
        src/ui/commands/command_palette.cpp
        src/ui/commands/command_registry.cpp
        src/ui/commands/shortcut_manager.cpp
        src/ui/commands/undo_manager.cpp
        src/ui/workspace/workspace.cpp
        src/ui/commands/shortcut_config.cpp
        src/ui/workspace/plugin_api.cpp
        src/ui/figures/tab_drag_controller.cpp
        src/ui/app/register_commands.cpp
    )
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_imgui_src})
            target_sources(spectra PRIVATE ${_imgui_src})
        endif()
    endforeach()
    target_include_directories(spectra PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_IMGUI)
endif()

# Eigen adapter (optional)
if(SPECTRA_USE_EIGEN)
    find_package(Eigen3 REQUIRED)
    target_link_libraries(spectra PUBLIC Eigen3::Eigen)
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_EIGEN)
endif()

# ─── Logical library split (spectra-core, spectra-ipc, spectra-render) ────────
# These are INTERFACE targets that expose subsets of the monolithic spectra lib.
# Downstream consumers can depend on spectra-core (no rendering), spectra-ipc
# (protocol + transport only), or spectra-render (full rendering stack).

add_library(spectra-core INTERFACE)
target_link_libraries(spectra-core INTERFACE spectra)
target_compile_definitions(spectra-core INTERFACE SPECTRA_CORE_ONLY)

add_library(spectra-ipc INTERFACE)
target_link_libraries(spectra-ipc INTERFACE spectra)
target_compile_definitions(spectra-ipc INTERFACE SPECTRA_IPC_ONLY)

add_library(spectra-render INTERFACE)
target_link_libraries(spectra-render INTERFACE spectra)

# Multi-process daemon + agent binaries (Phase 4 & 5)
if(SPECTRA_RUNTIME_MODE STREQUAL "multiproc")
    # spectra-backend daemon
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/daemon/main.cpp)
        add_executable(spectra-backend
            src/daemon/main.cpp
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
        endif()
    endif()
endif()

# ─── Install targets ─────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Library
install(TARGETS spectra EXPORT spectraTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Public headers
install(DIRECTORY include/spectra/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra
    FILES_MATCHING PATTERN "*.hpp"
)

# Generated version header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra
)

# Multiproc binaries
if(SPECTRA_RUNTIME_MODE STREQUAL "multiproc")
    if(TARGET spectra-backend)
        install(TARGETS spectra-backend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(TARGET spectra-window)
        install(TARGETS spectra-window RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# Python wheel: bundle backend binary into the Python package
if(SPECTRA_PYTHON_WHEEL AND TARGET spectra-backend)
    install(TARGETS spectra-backend
        RUNTIME DESTINATION spectra/_bin
    )
endif()

# Data assets (fonts, icons)
install(FILES
    third_party/Inter-Regular.ttf
    third_party/SpectraIcons.ttf
    third_party/SpectraIcons.otf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/fonts
)
install(FILES icons/spectra_icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME spectra.png
)
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

# CMake package config (find_package(spectra) support)
install(EXPORT spectraTargets
    FILE spectraTargets.cmake
    NAMESPACE spectra::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/spectraConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

# ─── CPack ───────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "spectra")
set(CPACK_PACKAGE_VENDOR "spectra-plot")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPU-accelerated scientific plotting library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "danlil240")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README  ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

# Debian
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1 (>= 1.2)")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# RPM
set(CPACK_RPM_PACKAGE_REQUIRES "vulkan-loader >= 1.2")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Science")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)

# ─── Examples ────────────────────────────────────────────────────────────────
if(SPECTRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ─── Tests ───────────────────────────────────────────────────────────────────
if(SPECTRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
