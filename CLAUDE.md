# Spectra - Claude Code Project Guide

## Project Overview

Spectra is a **GPU-accelerated C++20 plotting library** (v0.2.2) built on **Vulkan 1.2+** for scientific and engineering visualization. It features real-time animation, live data streaming, headless export, 3D rendering, and a full ImGui-based UI with docking, command palette, undo/redo, themes, plugin API, and workspace save/load. Additional integration layers cover **ROS2** (topics, bags, TF, diagnostics), **PX4/ULog** visualisation, **Qt embedding**, a **C FFI embed surface**, and an experimental **WebGPU** backend.

## Build System

- **Build tool:** CMake 3.20+ with Ninja (preferred) or Make
- **Standard:** C++20 (GCC 12+, Clang 15+, MSVC 2022+)
- **Static library output:** `build/lib/libspectra.a`

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
| `SPECTRA_USE_FFMPEG` | ON | Video export via ffmpeg |
| `SPECTRA_USE_EIGEN` | ON | Eigen vector adapters |
| `SPECTRA_USE_QT` | OFF | Qt adapter scaffolding (embedding) |
| `SPECTRA_USE_ROS2` | ON | ROS2 adapter (requires sourced ROS2 workspace) |
| `SPECTRA_ROS2_BAG` | ON | rosbag2 support (requires rosbag2 packages) |
| `SPECTRA_USE_PX4` | ON | PX4 ULog adapter (ULog visualiser + MAVLink) |
| `SPECTRA_USE_WEBGPU` | OFF | WebGPU/WASM backend (requires Dawn or wgpu-native) |
| `SPECTRA_BUILD_EXAMPLES` | ON | Example programs |
| `SPECTRA_BUILD_QT_EXAMPLE` | OFF | Qt6 embed example (requires `SPECTRA_USE_QT=ON`) |
| `SPECTRA_BUILD_TESTS` | ON | Unit tests and benchmarks |
| `SPECTRA_BUILD_BENCHMARKS` | OFF | Performance benchmark suite |
| `SPECTRA_BUILD_GOLDEN_TESTS` | ON | Visual regression tests |
| `SPECTRA_BUILD_EMBED_SHARED` | OFF | Build `libspectra_embed` shared library for C FFI |
| `SPECTRA_BUILD_QA_AGENT` | OFF | QA stress-testing agent |
| `SPECTRA_PYTHON_WHEEL` | OFF | Install backend binary into Python package |
| `SPECTRA_RUNTIME_MODE` | multiproc | `inproc` or `multiproc` |

## Project Structure

```
include/spectra/    # Public API headers only - keep minimal
src/
  core/             # Data model: Figure, Axes, Axes3D, Series, layout, transforms
  render/           # Renderer abstraction + abstract Backend interface
    vulkan/         # VulkanBackend: device, swapchain, pipeline, buffers
    webgpu/         # WebGPU backend (experimental, 2D only)
  gpu/shaders/      # GLSL 450 shaders (compiled to SPIR-V at build time)
  app/              # Standalone app entry point (main.cpp, inproc topic server)
  ui/
    app/            # App lifecycle: inproc/multiproc paths, SessionRuntime, WindowUIContext
    imgui/          # ImGuiIntegration, Axes3DRenderer, widget helpers
    figures/        # FigureRegistry, FigureManager, TabBar, TabDragController
    window/         # WindowManager, GlfwAdapter
    input/          # InputHandler, BoxZoomOverlay, GestureRecognizer, RegionSelect
    overlay/        # Crosshair, DataEditor, DataInteraction, DataMarker, Inspector, KnobManager
    commands/       # CommandRegistry, CommandPalette, ShortcutManager, UndoManager,
                    #   SeriesClipboard, UndoableProperty
    docking/        # DockSystem, SplitViewManager
    automation/     # UI automation hooks
    animation/      # AnimationController, TimelineEditor, KeyframeInterpolator, CameraAnimator,
                    #   ModeTransition, RecordingExport, TransitionEngine
    panel/          # Dedicated UI panels
    theme/          # ThemeManager, DesignTokens, Icons
    data/           # AxisLinkManager, CsvLoader, ClipboardExport
    camera/         # Camera
    layout/         # LayoutManager
    viewmodel/      # View-model layer for ImGui-facing code
    welcome/        # Welcome / onboarding screens
    workspace/      # Workspace save/load v3, FigureSerializer, PluginAPI
  embed/            # EmbedSurface, C FFI wrapper (spectra_embed_c)
  platform/
    window_system/  # SurfaceHost abstraction, GlfwSurfaceHost
  adapters/         # Optional integration adapters
    qt/             # QtRuntime, QtSurfaceHost (Qt embedding)
    ros2/           # ROS2: RosAppShell, BagPlayer, ExpressionEngine, TopicDiscovery,
                    #   GenericSubscriber, SubplotManager + ROS2 UI panels
    px4/            # PX4 ULog adapter, MAVLink, ULog reader
  anim/             # Animator, easing (7 modes), FrameScheduler, FrameProfiler
  data/             # Decimation (LTTB, min-max), filters
  math/             # DataTransform, TransformPipeline, TransformRegistry
  io/               # PNG/SVG/MP4 export
  ipc/              # Binary IPC: Codec (TLV + FlatBuffers), Message, Transport, BlobStore
  daemon/           # Multi-process backend daemon: FigureModel, SessionGraph, ProcessManager
  agent/            # Multi-process window agent entry point
python/             # Python bindings via IPC (spectra package)
examples/           # ~50 runnable C++ demos
tests/
  unit/             # 145+ Google Test unit test files (~1,200+ individual tests)
  golden/           # Visual regression tests (pixel comparison, 30+ baselines)
  bench/            # Google Benchmark performance tests (100+ benchmarks)
  qa/               # QA agent automated visual/ROS testing
  util/             # Test utilities (GPU hang detector, fixtures, validation guard)
third_party/        # Bundled: stb, VMA, tinyfiledialogs, Inter font, SpectraIcons font
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
- **Registry** for commands, shortcuts, figures, series types, data sources, transforms
- **Composite** hierarchy: App -> Figure -> Axes -> Series
- **EventBus** for decoupled intra-process notifications (`include/spectra/event_bus.hpp`)
- **Topics** pub/sub for live data streaming (`include/spectra/topic.hpp`)

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
- **IPC codec:** Supports both TLV (legacy) and FlatBuffers encoding; migration to FlatBuffers-by-default is planned
- **Workspace format:** v3 with backward compatibility for v1/v2
- **Plugin API:** Runtime-loadable plugins for series types, data sources, overlays, and transforms
- **WebGPU backend:** Experimental 2D-only backend (`src/render/webgpu/`); not production-ready

## Commit Style

Follow conventional commits: `type: description`
- Types: `feat`, `fix`, `refactor`, `test`, `docs`, `ci`, `perf`, `chore`
- Recent examples: `fix: remove hardcoded lavapipe ICD path, discover dynamically`

## Current Status

- Phases 1-3: Complete (themes, commands, undo, docking, MATLAB API, plugins)
- 3D Phase 1: Complete (MVP, depth, math library, camera)
- Phase 4 (multi-process): In progress
