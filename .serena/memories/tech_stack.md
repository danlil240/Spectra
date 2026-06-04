# Spectra — Tech Stack

## Language & toolchain

- **C++20** (GCC 12+/13+ preferred, Clang 15+/17+, MSVC 2022+)
- **CMake 3.20+**, **Ninja** preferred
- **OS reference:** Ubuntu 24.04 LTS (Noble) — see `BUILD_ENVIRONMENT.md`

## GPU & shaders

- Vulkan ≥ 1.2 via `libvulkan-dev` (APT; no LunarG SDK required on Linux)
- `glslang-tools` / `glslangValidator` for GLSL 450 → SPIR-V
- Golden/CI headless: lavapipe ICD + `xvfb-run` (see `mem:task_completion`)

## Fetched at configure (FetchContent)

GLFW 3.4, Dear ImGui v1.92.6-docking, FlatBuffers, GoogleTest — first `cmake` configures them.

## Bundled (`third_party/`)

stb, VMA, tinyfiledialogs, Inter + SpectraIcons fonts.

## Optional integrations (CMake flags)

| Area | Default | Notes |
|------|---------|-------|
| GLFW + ImGui | ON | SDL3 alternative (`SPECTRA_USE_SDL3`, exclusive with GLFW) |
| FFmpeg export | ON | `ffmpeg` in PATH at runtime |
| Eigen | ON | `libeigen3-dev` |
| ROS2 + rosbag2 | ON in tree, **OFF in canonical agent build** | Needs `source /opt/ros/humble/setup.bash` |
| PX4/ULog | ON | `libspectra_px4_adapter.a`, `spectra-px4` |
| Qt6 embed | OFF | `qt6-base-dev` |
| WebGPU | OFF | Dawn or Emscripten; 2D only, not production |
| Python wheel backend | OFF | `SPECTRA_PYTHON_WHEEL` embeds multiproc binary |

## Test & bench libs

- Google Test (`tests/unit/`)
- Google Benchmark (`tests/bench/`, `SPECTRA_BUILD_BENCHMARKS` OFF by default)

## Outputs (Release canonical build)

- `build/libspectra.a` — full library
- `build/libspectra-core.a` — headless core
- `build/spectra`, `build/spectra-backend`, `build/spectra-window`
- Public headers: `include/spectra/` only