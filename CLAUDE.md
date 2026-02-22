# Spectra - Claude Code Project Guide

## Project Overview

Spectra is a **GPU-accelerated C++20 plotting library** built on **Vulkan 1.2+** for scientific and engineering visualization. It features real-time animation, live data streaming, headless export, 3D rendering, and a full ImGui-based UI with docking, command palette, undo/redo, and themes.

## Build System

- **Build tool:** CMake 3.20+ with Ninja (preferred) or Make
- **Standard:** C++20 (GCC 12+, Clang 15+, MSVC 2022+)
- **Static library output:** `build/libspectra.a`

### Common Build Commands

```bash
# Configure (from project root)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Run all non-GPU tests
ctest --test-dir build -LE gpu --output-on-failure

# Run GPU tests only
ctest --test-dir build -L gpu --output-on-failure

# Run golden image tests
ctest --test-dir build -L golden -j1 --output-on-failure

# Run a specific test
./build/tests/unit_test_name

# Run benchmarks
./build/tests/bench_name
```

### CMake Feature Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `SPECTRA_USE_GLFW` | ON | GLFW windowing |
| `SPECTRA_USE_IMGUI` | ON | Dear ImGui UI |
| `SPECTRA_USE_FFMPEG` | OFF | Video export via ffmpeg |
| `SPECTRA_USE_EIGEN` | OFF | Eigen vector adapters |
| `SPECTRA_BUILD_EXAMPLES` | ON | Example programs |
| `SPECTRA_BUILD_TESTS` | ON | Unit tests |
| `SPECTRA_BUILD_GOLDEN_TESTS` | ON | Visual regression tests |
| `SPECTRA_RUNTIME_MODE` | inproc | `inproc` or `multiproc` |

## Project Structure

```
include/spectra/    # Public API headers only - keep minimal
src/
  core/             # Data model: Figure, Axes, Axes3D, Series, layout, transforms
  render/           # Renderer abstraction + abstract Backend interface
    vulkan/         # VulkanBackend: device, swapchain, pipeline, buffers
  gpu/shaders/      # GLSL 450 shaders (compiled to SPIR-V at build time)
  anim/             # Animator, easing (7 modes), frame scheduler
  ui/               # App, ImGui integration, commands, themes, docking, input
  io/               # PNG/SVG/MP4 export
  ipc/              # Binary IPC protocol (multi-process mode)
  daemon/           # Multi-process backend daemon
  agent/            # Multi-process window agent
  data/             # Decimation, filters
  math/             # Data transforms (log, normalize, FFT)
python/             # Python bindings via IPC
examples/           # 39+ runnable C++ demos
tests/
  unit/             # 120+ Google Test unit tests
  golden/           # Visual regression tests (pixel comparison)
  bench/            # Google Benchmark performance tests
third_party/        # Bundled: stb, VMA, tinyfiledialogs
plans/              # Architecture documents and roadmaps
```

## Code Conventions

### Style & Formatting

- **clang-format** config in `.clang-format` (Google base + C++20, Allman braces, 4-space indent, 100-col limit)
- Run `clang-format -i <file>` before committing or use `format_project.sh`
- Naming: `PascalCase` classes, `snake_case` functions/variables, trailing `_` for members, `UPPER_SNAKE` for macros

### Architecture Rules

- **No global state** - all managers are instance members of App, passed by pointer
- **RAII throughout** - stack allocation preferred, `std::unique_ptr` for ownership
- **Zero-copy where possible** - use `std::span<const float>` for data interfaces
- **No exceptions in render backend** - return codes only
- **Thread safety** - SPSC ring buffer between app and render threads; explicit `std::mutex` elsewhere
- **Deferred GPU cleanup** - never destroy GPU resources on the app thread; queue for render thread

### Patterns in Use

- **Builder** for configs (AnimationBuilder, AppConfig)
- **Strategy** for backends (abstract `Backend`, `VulkanBackend` impl)
- **Registry** for commands, shortcuts, figures
- **Composite** hierarchy: App -> Figure -> Axes -> Series

### Public API Guidelines

- Public headers live in `include/spectra/` - keep them minimal, implementation details in `src/`
- Public API uses `std::span`, `std::string_view`, value types
- MATLAB-style format strings supported: `"r--o"` = red dashed line with circle markers

## Testing

- **Unit tests** use Google Test. Add tests for any new functionality.
- **GPU tests** are labeled `gpu` in CMake - excluded from sanitizer CI runs (`ctest -LE gpu`)
- **Golden tests** compare headless PNG renders against baseline images in `tests/golden/baseline/`
- **Benchmarks** use Google Benchmark for performance regression tracking
- Sanitizers (ASan, UBSan) run in CI on non-GPU tests

## Key Architectural Notes

- **Rendering pipeline:** Renderer queues draw commands -> VulkanBackend executes them on GPU
- **Shaders:** GLSL 450 -> SPIR-V (compiled via `cmake/CompileShaders.cmake`), embedded as byte arrays
- **SDF rendering:** Lines and markers use signed-distance-field anti-aliasing in fragment shaders
- **MSDF text:** Multi-channel SDF font atlas for resolution-independent text
- **Two runtime modes:** in-process (single binary) and multi-process (daemon + window agents via IPC)
- **Workspace format:** v3 with backward compatibility for v1/v2

## Commit Style

Follow conventional commits: `type: description`
- Types: `feat`, `fix`, `refactor`, `test`, `docs`, `ci`, `perf`, `chore`
- Recent examples: `fix: remove hardcoded lavapipe ICD path, discover dynamically`

## Current Status

- Phases 1-3: Complete (themes, commands, undo, docking, MATLAB API, plugins)
- 3D Phase 1: Complete (MVP, depth, math library, camera)
- Phase 4 (multi-process): In progress
