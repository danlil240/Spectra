cmake_minimum_required(VERSION 3.20)
project(spectra VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Feature flags
option(SPECTRA_USE_GLFW    "Enable GLFW windowing adapter"       ON)
option(SPECTRA_USE_IMGUI   "Enable Dear ImGui menu bar/settings" ON)
option(SPECTRA_USE_FFMPEG  "Enable video export via ffmpeg pipe"  OFF)
option(SPECTRA_USE_EIGEN   "Enable Eigen vector adapters"         OFF)
option(SPECTRA_BUILD_EXAMPLES "Build example programs"            ON)
option(SPECTRA_BUILD_TESTS    "Build unit tests and benchmarks"   ON)

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

# UI sources (non-ImGui) — guarded so parallel agents don't break each other's builds.
# app.cpp is always present; others are included only if they exist on disk.
file(GLOB SPECTRA_UI_SOURCES src/ui/app.cpp)
foreach(_ui_src
    src/ui/input.cpp
    src/ui/layout_manager.cpp
    src/ui/dock_node.cpp
    src/ui/tab_bar.cpp
    src/ui/animation_controller.cpp
    src/ui/gesture_recognizer.cpp
    src/ui/transition_engine.cpp
    src/ui/figure_manager.cpp
    src/ui/timeline_editor.cpp
    src/ui/recording_export.cpp
    src/ui/split_view.cpp
    src/ui/dock_system.cpp
    src/ui/axis_link.cpp
    src/ui/data_transform.cpp
    src/ui/keyframe_interpolator.cpp
    src/ui/camera_animator.cpp
    src/ui/animation_curve_editor.cpp
    src/ui/mode_transition.cpp
    src/ui/camera.cpp
    src/ui/axes3d_renderer.cpp
    src/ui/window_manager.cpp
    src/ui/figure_registry.cpp
)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_ui_src})
        list(APPEND SPECTRA_UI_SOURCES ${_ui_src})
    endif()
endforeach()

# Core library (headless-capable)
add_library(spectra STATIC
    ${SPECTRA_CORE_SOURCES}
    ${SPECTRA_RENDER_SOURCES}
    ${SPECTRA_ANIM_SOURCES}
    ${SPECTRA_IO_SOURCES}
    ${SPECTRA_DATA_SOURCES}
    ${SPECTRA_UI_SOURCES}
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

target_link_libraries(spectra PUBLIC Vulkan::Vulkan)

target_compile_definitions(spectra PRIVATE
    $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
    $<$<BOOL:${SPECTRA_USE_EIGEN}>:SPECTRA_USE_EIGEN>
)

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
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    endif()

    target_sources(spectra PRIVATE src/ui/glfw_adapter.cpp)
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
        src/ui/imgui_integration.cpp
    )

    # ImGui-dependent UI sources — guarded so parallel agents don't break the build.
    # When adding a new .cpp that depends on ImGui, add an if(EXISTS) block here.
    foreach(_imgui_src
        src/ui/theme.cpp
        src/ui/icons.cpp
        src/ui/widgets.cpp
        src/ui/inspector.cpp
        src/ui/tooltip.cpp
        src/ui/crosshair.cpp
        src/ui/data_marker.cpp
        src/ui/data_interaction.cpp
        src/ui/region_select.cpp
        src/ui/legend_interaction.cpp
        src/ui/box_zoom_overlay.cpp
        src/ui/command_palette.cpp
        src/ui/command_registry.cpp
        src/ui/shortcut_manager.cpp
        src/ui/undo_manager.cpp
        src/ui/workspace.cpp
        src/ui/shortcut_config.cpp
        src/ui/plugin_api.cpp
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

# Examples
if(SPECTRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(SPECTRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
