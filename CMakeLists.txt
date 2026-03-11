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
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp)
    message("Font file Inter-Regular.ttf already embedded, skipping font embedding")
else()
    embed_binary_file(
        INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Inter-Regular.ttf
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp
        VARIABLE InterFont_ttf
    )
endif()

# Feature flags
option(SPECTRA_USE_GLFW    "Enable GLFW windowing adapter"       ON)
option(SPECTRA_USE_IMGUI   "Enable Dear ImGui menu bar/settings" ON)
option(SPECTRA_USE_QT      "Enable Qt adapter scaffolding"        OFF)
option(SPECTRA_USE_FFMPEG  "Enable video export via ffmpeg pipe"  ON)
option(SPECTRA_USE_EIGEN   "Enable Eigen vector adapters"         ON)
option(SPECTRA_USE_ROS2    "Enable ROS2 adapter (requires sourced ROS2 workspace)" ON)
option(SPECTRA_ROS2_BAG    "Enable rosbag2 support (requires rosbag2 packages)"    ON)
option(SPECTRA_USE_PX4     "Enable PX4 ULog adapter (ULog visualizer + MAVLink)"   ON)
option(SPECTRA_BUILD_EXAMPLES "Build example programs"            ON)
option(SPECTRA_BUILD_QT_EXAMPLE "Build the Qt6 embed example"     OFF)
option(SPECTRA_BUILD_TESTS    "Build unit tests and benchmarks"   ON)
option(SPECTRA_PYTHON_WHEEL  "Install backend into Python package _bin/" OFF)

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

# Skip git network checks when FetchContent source dirs already exist.
# On a clean checkout these are absent so the download still happens normally.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

message(STATUS "SPECTRA_RUNTIME_MODE: ${SPECTRA_RUNTIME_MODE}")
message(STATUS "SPECTRA_PRESENT_MODE: ${SPECTRA_PRESENT_MODE}")

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

# Shader compilation (GLSL -> SPIR-V)
include(cmake/CompileShaders.cmake)

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
    src/ui/panel/panel_detach_controller.cpp
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
    src/ui/data/clipboard_export.cpp
    src/ui/workspace/figure_serializer.cpp
    src/ui/commands/series_clipboard.cpp
    src/ui/overlay/data_editor.cpp
    src/ui/overlay/custom_transform_dialog.cpp
    src/embed/embed_surface.cpp
    src/embed/spectra_embed_c.cpp
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
    ${SPECTRA_PLATFORM_SOURCES}
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
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_include_directories(spectra SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
)

# Suppress warnings in third-party headers (stb_image.h tc16 array — GCC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(spectra PRIVATE -Wno-error=maybe-uninitialized)
endif()

target_link_libraries(spectra PUBLIC Vulkan::Vulkan)

target_compile_definitions(spectra PUBLIC
    $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
    SPECTRA_RUNTIME_MODE_${SPECTRA_RUNTIME_MODE}
)
target_compile_definitions(spectra PRIVATE
    $<$<BOOL:${SPECTRA_USE_EIGEN}>:SPECTRA_USE_EIGEN>
)
target_compile_definitions(spectra PRIVATE SPECTRA_PRESENT_MODE_${SPECTRA_PRESENT_MODE})

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
    # For Python wheels, prefer building GLFW from source to avoid inheriting
    # ISA requirements from distro-provided binaries in manylinux images.
    if(NOT SPECTRA_PYTHON_WHEEL)
        find_package(glfw3 3.3 QUIET)
    endif()
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

# Dear ImGui (optional — requires GLFW)
if(SPECTRA_USE_IMGUI AND SPECTRA_USE_GLFW)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        # GIT_SHALLOW TRUE is intentionally NOT set here: v1.91.8-docking is on
        # a separate branch from v1.91.8. GIT_SHALLOW TRUE causes FetchContent
        # to sometimes resolve the wrong tag (main branch tip instead of the
        # docking branch). A full clone ensures the correct commit is checked out.
        GIT_TAG        v1.91.8-docking
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

# ROS2 adapter (optional) — gated behind SPECTRA_USE_ROS2
# Requires a sourced ROS2 workspace (Humble+) before running CMake.
# Zero impact on any target when SPECTRA_USE_ROS2=OFF.
if(SPECTRA_USE_ROS2)
    find_package(ament_cmake QUIET)
    if(NOT ament_cmake_FOUND)
        message(STATUS "ament_cmake not found — disabling ROS2 adapter (SPECTRA_USE_ROS2=OFF)")
        set(SPECTRA_USE_ROS2 OFF)
    endif()
endif()
if(SPECTRA_USE_ROS2)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TINYXML2 QUIET tinyxml2)
    endif()
    if(NOT TARGET tinyxml2::tinyxml2)
        find_path(TINYXML2_INCLUDE_DIR tinyxml2.h HINTS ${TINYXML2_INCLUDE_DIRS})
        find_library(TINYXML2_LIBRARY NAMES tinyxml2 HINTS ${TINYXML2_LIBRARY_DIRS})

        if(TINYXML2_INCLUDE_DIR AND TINYXML2_LIBRARY)
            add_library(tinyxml2::tinyxml2 UNKNOWN IMPORTED)
            set_target_properties(tinyxml2::tinyxml2 PROPERTIES
                IMPORTED_LOCATION "${TINYXML2_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${TINYXML2_INCLUDE_DIR}"
            )
        endif()
    endif()
    if(NOT TARGET tinyxml2::tinyxml2)
        message(FATAL_ERROR
            "TinyXML2 not found. Install libtinyxml2-dev or make tinyxml2 discoverable via pkg-config/CMAKE_PREFIX_PATH."
        )
    endif()
    find_package(rclcpp REQUIRED)
    find_package(rcl_interfaces REQUIRED)
    find_package(rosidl_typesupport_introspection_cpp REQUIRED)
    find_package(std_msgs REQUIRED)
    find_package(geometry_msgs REQUIRED)
    find_package(nav_msgs REQUIRED)
    find_package(sensor_msgs REQUIRED)
    find_package(diagnostic_msgs REQUIRED)
    find_package(tf2_msgs REQUIRED)
    find_package(visualization_msgs REQUIRED)
    find_package(rcpputils QUIET)
    find_path(SPECTRA_NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)

    if(NOT SPECTRA_NLOHMANN_JSON_INCLUDE_DIR)
        message(FATAL_ERROR "SPECTRA_USE_ROS2 requires nlohmann/json.hpp to build ros_session.cpp")
    endif()

    if(SPECTRA_ROS2_BAG)
        find_package(rosbag2_cpp REQUIRED)
        find_package(rosbag2_storage REQUIRED)
    endif()

    # Adapter library — all ROS2 adapter source lives under src/adapters/ros2/
    add_library(spectra_ros2_adapter
        src/adapters/ros2/ros2_adapter.cpp
        src/adapters/ros2/ros2_bridge.cpp
        src/adapters/ros2/topic_discovery.cpp
        src/adapters/ros2/message_introspector.cpp
        src/adapters/ros2/generic_subscriber.cpp
        src/adapters/ros2/ros_plot_manager.cpp
        src/adapters/ros2/ui/topic_list_panel.cpp
        src/adapters/ros2/ui/topic_echo_panel.cpp
        src/adapters/ros2/ui/topic_stats_overlay.cpp
        src/adapters/ros2/ui/field_drag_drop.cpp
        src/adapters/ros2/ui/node_graph_panel.cpp
        src/adapters/ros2/display/display_registry.cpp
        src/adapters/ros2/display/grid_display.cpp
        src/adapters/ros2/display/tf_display.cpp
        src/adapters/ros2/display/marker_display.cpp
        src/adapters/ros2/display/pointcloud_display.cpp
        src/adapters/ros2/display/laserscan_display.cpp
        src/adapters/ros2/display/image_display.cpp
        src/adapters/ros2/display/path_display.cpp
        src/adapters/ros2/display/pose_display.cpp
        src/adapters/ros2/display/robot_model_display.cpp
        src/adapters/ros2/scene/scene_manager.cpp
        src/adapters/ros2/scene/mesh_primitives.cpp
        src/adapters/ros2/scene/scene_renderer.cpp
        src/adapters/ros2/tf/tf_buffer.cpp
        src/adapters/ros2/urdf/urdf_parser.cpp
        src/adapters/ros2/ui/displays_panel.cpp
        src/adapters/ros2/ui/inspector_panel.cpp
        src/adapters/ros2/ui/scene_viewport.cpp
        src/adapters/ros2/ui/tf_tree_panel.cpp
        src/adapters/ros2/subplot_manager.cpp
        src/adapters/ros2/expression_engine.cpp
        src/adapters/ros2/expression_plot.cpp
        src/adapters/ros2/ui/expression_editor.cpp
        src/adapters/ros2/ros_csv_export.cpp
        src/adapters/ros2/ros_screenshot_export.cpp
        src/adapters/ros2/ros_clipboard_export.cpp
        src/adapters/ros2/ros_app_shell.cpp
        src/adapters/ros2/service_caller.cpp
        src/adapters/ros2/ui/service_caller_panel.cpp
        src/adapters/ros2/ui/bag_info_panel.cpp
        src/adapters/ros2/bag_player.cpp
        src/adapters/ros2/ui/bag_playback_panel.cpp
        src/adapters/ros2/ui/param_editor_panel.cpp
        src/adapters/ros2/ros_log_viewer.cpp
        src/adapters/ros2/ui/log_viewer_panel.cpp
        src/adapters/ros2/ros_session.cpp
        src/adapters/ros2/ui/diagnostics_panel.cpp
    )

    target_link_libraries(spectra_ros2_adapter PUBLIC
        spectra
        tinyxml2::tinyxml2
        rclcpp::rclcpp
        rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
    )

    target_link_libraries(spectra_ros2_adapter PUBLIC
        ${std_msgs_TARGETS}
        ${geometry_msgs_TARGETS}
        ${nav_msgs_TARGETS}
        ${sensor_msgs_TARGETS}
        ${diagnostic_msgs_TARGETS}
        ${tf2_msgs_TARGETS}
        ${visualization_msgs_TARGETS}
    )

    if(SPECTRA_ROS2_BAG)
        target_sources(spectra_ros2_adapter PRIVATE
            src/adapters/ros2/bag_reader.cpp
            src/adapters/ros2/bag_recorder.cpp
        )
        # bag_player.cpp is already in the library sources above;
        # SPECTRA_ROS2_BAG compile definition enables its full implementation.
        target_link_libraries(spectra_ros2_adapter PUBLIC
            rosbag2_cpp::rosbag2_cpp
            rosbag2_storage::rosbag2_storage
        )
        target_compile_definitions(spectra_ros2_adapter PUBLIC SPECTRA_ROS2_BAG)
    endif()

    target_compile_features(spectra_ros2_adapter PRIVATE cxx_std_20)

    target_include_directories(spectra_ros2_adapter PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/ros2
    )
    target_include_directories(spectra_ros2_adapter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${SPECTRA_NLOHMANN_JSON_INCLUDE_DIR}
    )

    target_compile_definitions(spectra_ros2_adapter PUBLIC SPECTRA_USE_ROS2)

    # Forward ImGui include paths so topic_list_panel.cpp can find imgui.h
    # when SPECTRA_USE_IMGUI is ON (inherited via spectra PUBLIC definition).
    if(SPECTRA_USE_IMGUI AND SPECTRA_USE_GLFW AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(spectra_ros2_adapter PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()

    # spectra-ros standalone executable
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/ros2/main.cpp)
        add_executable(spectra-ros
            src/adapters/ros2/main.cpp
        )
        target_link_libraries(spectra-ros PRIVATE spectra_ros2_adapter)
        target_compile_features(spectra-ros PRIVATE cxx_std_20)
        target_include_directories(spectra-ros PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
    endif()

    message(STATUS "ROS2 adapter enabled (rosbag2: ${SPECTRA_ROS2_BAG})")
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

# PX4 / ULog adapter (optional) — standalone flight log viewer + MAVLink bridge.
# No external dependencies beyond POSIX sockets — always buildable.
if(SPECTRA_USE_PX4)
    add_library(spectra_px4_adapter
        src/adapters/px4/px4_adapter.cpp
        src/adapters/px4/ulog_reader.cpp
        src/adapters/px4/px4_bridge.cpp
        src/adapters/px4/px4_plot_manager.cpp
        src/adapters/px4/px4_app_shell.cpp
        src/adapters/px4/ui/ulog_file_panel.cpp
        src/adapters/px4/ui/live_connection_panel.cpp
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

    # Forward ImGui include paths so UI panels can find imgui.h.
    if(SPECTRA_USE_IMGUI AND SPECTRA_USE_GLFW AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(spectra_px4_adapter PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()

    # spectra-px4 standalone executable
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4/main.cpp)
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
    target_include_directories(spectra_embed PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    # When ImGui is enabled, the embed surface uses it for full UI rendering.
    # Forward ImGui include paths so embed_surface.cpp can find imgui headers.
    if(SPECTRA_USE_IMGUI AND SPECTRA_USE_GLFW AND DEFINED imgui_SOURCE_DIR)
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
# Auto-detect shared-library runtime dependencies from built binaries
# (glibc, X11/Wayland, stdlib, etc.) so apt can resolve them automatically.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
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
