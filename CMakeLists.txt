cmake_minimum_required(VERSION 3.20)
project(plotix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Feature flags
option(PLOTIX_USE_GLFW    "Enable GLFW windowing adapter"       ON)
option(PLOTIX_USE_IMGUI   "Enable Dear ImGui menu bar/settings" ON)
option(PLOTIX_USE_FFMPEG  "Enable video export via ffmpeg pipe"  OFF)
option(PLOTIX_USE_EIGEN   "Enable Eigen vector adapters"         OFF)
option(PLOTIX_BUILD_EXAMPLES "Build example programs"            ON)
option(PLOTIX_BUILD_TESTS    "Build unit tests and benchmarks"   ON)

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

# Shader compilation (GLSL -> SPIR-V)
include(cmake/CompileShaders.cmake)

# Collect source files
file(GLOB_RECURSE PLOTIX_CORE_SOURCES   src/core/*.cpp)
file(GLOB_RECURSE PLOTIX_RENDER_SOURCES src/render/*.cpp)
file(GLOB_RECURSE PLOTIX_TEXT_SOURCES   src/text/*.cpp)
file(GLOB_RECURSE PLOTIX_ANIM_SOURCES   src/anim/*.cpp)
file(GLOB_RECURSE PLOTIX_IO_SOURCES     src/io/*.cpp)
file(GLOB_RECURSE PLOTIX_DATA_SOURCES  src/data/*.cpp)

file(GLOB PLOTIX_UI_SOURCES src/ui/app.cpp)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/input.cpp)
    list(APPEND PLOTIX_UI_SOURCES src/ui/input.cpp)
endif()

# Core library (headless-capable)
add_library(plotix STATIC
    ${PLOTIX_CORE_SOURCES}
    ${PLOTIX_RENDER_SOURCES}
    ${PLOTIX_TEXT_SOURCES}
    ${PLOTIX_ANIM_SOURCES}
    ${PLOTIX_IO_SOURCES}
    ${PLOTIX_DATA_SOURCES}
    ${PLOTIX_UI_SOURCES}
)

target_include_directories(plotix PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(plotix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(plotix PUBLIC Vulkan::Vulkan)

target_compile_definitions(plotix PRIVATE
    $<$<BOOL:${PLOTIX_USE_FFMPEG}>:PLOTIX_USE_FFMPEG>
    $<$<BOOL:${PLOTIX_USE_EIGEN}>:PLOTIX_USE_EIGEN>
)

# Ensure shaders are compiled before the library
if(TARGET plotix_shaders)
    add_dependencies(plotix plotix_shaders)
endif()

# GLFW adapter (optional)
if(PLOTIX_USE_GLFW)
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

    target_sources(plotix PRIVATE src/ui/glfw_adapter.cpp)
    target_link_libraries(plotix PUBLIC glfw)
    target_compile_definitions(plotix PUBLIC PLOTIX_USE_GLFW)
endif()

# Dear ImGui (optional — requires GLFW)
if(PLOTIX_USE_IMGUI AND PLOTIX_USE_GLFW)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8
    )
    FetchContent_MakeAvailable(imgui)

    target_sources(plotix PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        src/ui/imgui_integration.cpp
    )
    target_include_directories(plotix PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(plotix PUBLIC PLOTIX_USE_IMGUI)
endif()

# Eigen adapter (optional)
if(PLOTIX_USE_EIGEN)
    find_package(Eigen3 REQUIRED)
    target_link_libraries(plotix PUBLIC Eigen3::Eigen)
    target_compile_definitions(plotix PUBLIC PLOTIX_USE_EIGEN)
endif()

# Examples
if(PLOTIX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(PLOTIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
