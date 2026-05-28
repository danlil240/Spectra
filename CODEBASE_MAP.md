# Spectra — Comprehensive Codebase Map

> **Purpose**: This document is the maintained map of the Spectra repository.
> **All agents should read it before changing code.**
> Last updated: 2026-05-28

---

## 1. Project Identity

**Spectra** is a GPU-accelerated scientific / engineering plotting stack centered on modern C++20,
a Vulkan renderer, and an ImGui-driven interactive UI. The repository now spans:

- Core 2D + 3D plotting (`Figure`, `Axes`, `Series`, statistical plots, shapes, animation).
- Two rendering backends: **Vulkan** (primary, production) and **WebGPU** (experimental/inproc-first).
- Two runtime modes: **inproc** and **multiproc** (daemon + window agents over IPC).
- Python bindings, ROS2 integration, PX4/ULog tooling, plugin loading, topics/pub-sub, and automation/MCP.
- Interactive UI subsystems for docking, commands, workspace persistence, accessibility, settings, and testing.

High-signal repository counts (checked against the working tree):

- `include/spectra/`: **32** public headers.
- `src/gpu/shaders/`: **28** GLSL shader files plus **5** WGSL shaders.
- `src/adapters/ros2/`: **110** ROS2 adapter files across core/display/messages/scene/tf/urdf/ui.
- `src/adapters/px4/`: **20** PX4 files across core/messages/ui.
- `tests/unit/`: **164** unit-test source files.
- `tests/bench/`: **11** benchmark files.
- `examples/`: **53** runnable C++ examples plus support assets; `python/examples/`: **18** demos.

---

## 2. Directory Structure

```
Spectra/
├── include/spectra/                  # Public API headers (32 headers)
├── src/
│   ├── app/                          # Standalone app entry + inproc topic server
│   ├── core/                         # Figure / Axes / Series data model + transforms
│   ├── render/
│   │   ├── vulkan/                   # Production Vulkan backend
│   │   └── webgpu/                   # Experimental WebGPU backend
│   ├── ui/
│   │   ├── app/                      # SessionRuntime, WindowRuntime, WindowUIContextBuilder
│   │   │   └── commands/             # Command registration split by domain
│   │   ├── imgui/                    # Main ImGui integration and widgets
│   │   ├── figures/                  # FigureManager, FigureRegistry, tab handling
│   │   ├── window/                   # GLFW/SDL3 window adapters and lifecycle
│   │   ├── input/                    # Mouse/keyboard/gesture handling
│   │   ├── overlay/                  # Crosshair, inspector, data editor, annotations, tooltips
│   │   ├── commands/                 # Command palette, shortcuts, undo, clipboard
│   │   ├── docking/                  # Dock system and split views
│   │   ├── animation/                # Timeline, transitions, recording, camera animation
│   │   ├── theme/                    # Theme manager, icons, design tokens
│   │   ├── data/                     # CSV import/export, axis linking, HTML table export
│   │   ├── camera/                   # UI-facing camera implementation
│   │   ├── layout/                   # Layout manager
│   │   ├── viewmodel/                # Axes/Figure/Series view models
│   │   ├── workspace/                # Workspace persistence + plugin loading
│   │   ├── accessibility/            # Accessible summaries + sonification
│   │   ├── settings/                 # Settings panel and storage
│   │   ├── topics/                   # Topic browser/panel
│   │   ├── panel/                    # Panel detaching helpers
│   │   ├── automation/               # Automation server + MCP server
│   │   │   └── handlers/             # Capture/command/figure/input/utility/window handlers
│   │   └── *.hpp                     # Shared UI utility headers
│   ├── embed/                        # EmbedSurface + C FFI implementation
│   ├── platform/
│   │   └── window_system/            # GLFW/SDL3 surface hosts
│   ├── anim/                         # Animator, easing, frame scheduling/profiling
│   ├── data/                         # Chunked arrays, decimation, filters, LOD cache, mapped files
│   ├── math/                         # Data transforms + expression evaluator
│   ├── io/                           # PNG/SVG/video export + ffmpeg helpers
│   ├── ipc/
│   │   └── schemas/                  # FlatBuffers schema
│   ├── daemon/                       # Multiprocess backend daemon
│   ├── agent/                        # Window agent entry point
│   ├── gpu/shaders/
│   │   └── wgsl/                     # WGSL sources embedded for WebGPU/tests
│   └── adapters/
│       ├── qt/                       # Qt runtime + surface host
│       ├── px4/
│       │   ├── messages/             # PX4 telemetry adapters
│       │   └── ui/                   # PX4 live/file panels
│       └── ros2/
│           ├── display/              # 3D/scene display plugins
│           ├── messages/             # ROS message adapters
│           ├── scene/                # Scene manager/renderer/mesh primitives
│           ├── tf/                   # TF buffer
│           ├── urdf/                 # URDF parsing
│           └── ui/                   # ROS2 tooling panels
├── tests/
│   ├── unit/                         # 164 unit test files
│   ├── bench/                        # 11 benchmarks
│   ├── golden/                       # 5 golden test sources + helpers + baselines
│   ├── qa/                           # QA executables
│   └── util/                         # Shared test utilities
├── examples/
│   └── plugins/                      # Plugin example implementations + manifests
├── python/
│   ├── spectra/                      # Python package
│   │   ├── _fb_generated/            # FlatBuffers-generated Python package placeholder
│   │   └── backends/                 # Qt/Agg backend support
│   ├── examples/                     # Python demos
│   └── tests/                        # Python tests
├── cmake/                            # Shader / asset / FlatBuffers codegen modules
├── tools/                            # Repo utilities + sanitizer config
├── docs/                             # HTML docs, Markdown guides, Doxygen, man page, plans
├── packaging/                        # AppImage, apt, AUR, shell completions, Homebrew, Scoop
├── docker/                           # Container recipes
├── plans/                            # Active + archived design / QA / roadmap docs
├── skills/                           # Specialized skill prompts/reports
├── icons/                            # Desktop/app artwork
└── third_party/                      # Fonts, icon data, stb, tinyfiledialogs, VMA
```

---

## 3. Public API (include/spectra/)

Everything in `include/spectra/` is user-facing. Internal implementation lives under `src/`.

| Header | Contents |
|---|---|
| `animator.hpp` | Public animation primitives: `Animator`, `Keyframe<T>`, easing helpers, frame callbacks. |
| `app.hpp` | Top-level `App` API for inproc and multiproc execution, figure/window lifecycle, and frame stepping. |
| `axes.hpp` | 2D axes model, axis limits/ticks/grid configuration, and subplot ownership. |
| `axes3d.hpp` | 3D axes model with camera integration, bounds, lighting, and 3D interaction state. |
| `camera.hpp` | Orbit camera math and serialization for 3D views. |
| `chunked_series.hpp` | Large-dataset series API built around chunked storage / progressive upload patterns. |
| `color.hpp` | RGBA color type, named colors, palette helpers. |
| `custom_series.hpp` | Extension hooks for user-defined/custom renderable series. |
| `easy.hpp` | MATLAB-style free-function API (`plot`, `scatter`, `subplot`, `figure`, `show`, `plot3`, `surf`, ...). |
| `easy_embed.hpp` | One-shot offscreen/embed helpers for rendering simple figures without a full app runtime. |
| `eigen.hpp` | Eigen adapters for vector/matrix-based plotting inputs. |
| `eigen_easy.hpp` | Eigen overloads for the easy API. |
| `embed.hpp` | EmbedSurface API for offscreen or foreign-window rendering and GPU readback/interoperability. |
| `event_bus.hpp` | Public event bus contracts used by the application and plugin surfaces. |
| `export.hpp` | PNG/SVG/video export entry points. |
| `figure.hpp` | Figure object, subplot ownership, titles/legend/style, animation hooks. |
| `figure_registry.hpp` | Public registry/lookup APIs for figure lifecycle. |
| `frame.hpp` | Per-frame timing state passed to animation/update callbacks. |
| `fwd.hpp` | Forward declarations for the public API surface. |
| `knob_manager.hpp` | Public parameter/knob management interfaces. |
| `logger.hpp` | Logging API, log levels, sinks, and macros. |
| `math3d.hpp` | Minimal `vec*` / `mat4` math types used by the public 3D API. |
| `plot_style.hpp` | Line, marker, dash, and MATLAB-style format-string parsing. |
| `series.hpp` | Base 2D series plus line/scatter and common styling/data mutation methods. |
| `series3d.hpp` | 3D line/scatter/surface/mesh series APIs. |
| `series_shapes.hpp` | 2D shape-oriented series helpers and types. |
| `series_shapes3d.hpp` | 3D shape-oriented series helpers and types. |
| `series_stats.hpp` | Statistical plots: histogram, bar, violin, box plot, and related helpers. |
| `spectra.hpp` | Umbrella include for the entire public C++ API. |
| `spectra_embed_c.h` | Pure-C embedding/FFI surface for non-C++ consumers. |
| `theme_api.hpp` | Public theme selection / registration API. |
| `topic.hpp` | Public publish/subscribe topic API for streaming data topics. |

---

## 4. Class Hierarchy & Ownership

### 4.1 Core data model

```
App / EmbedSurface
 ├── FigureRegistry
 │    └── Figure
 │         ├── Axes (2D)
 │         │    ├── LineSeries / ScatterSeries
 │         │    ├── statistical series (box / violin / histogram / bar)
 │         │    ├── shape-oriented series
 │         │    └── ChunkedSeries / CustomSeries variants
 │         └── Axes3D
 │              ├── LineSeries3D / ScatterSeries3D
 │              ├── SurfaceSeries / MeshSeries
 │              └── 3D shape/image/point-cloud-oriented display paths
 ├── Backend (abstract)
 │    ├── VulkanBackend
 │    └── WebGPUBackend
 ├── Renderer
 │    ├── TextRenderer
 │    └── SeriesTypeRegistry
 └── Runtime/UI services (WindowManager, InputHandler, DockSystem, CommandRegistry, ...)
```

### 4.2 Runtime layer (`src/ui/app/`)

```
App::run()
 ├── inproc path      -> src/ui/app/app_inproc.cpp
 ├── multiproc path   -> src/ui/app/app_multiproc.cpp
 ├── frame stepping   -> src/ui/app/app_step.cpp
 └── SessionRuntime
      ├── WindowRuntime (per-window update/render loop)
      ├── WindowUIContext / WindowUIContextBuilder
      ├── AnimationTickGate, RedrawTracker, ResourceMonitor, PerfMetrics
      └── domain command registrars in src/ui/app/commands/
```

### 4.3 Per-window UI bundle (`WindowUIContext`)

Each interactive window owns a `WindowUIContext` that wires together ImGui integration,
figure tabs, docking, input, overlays, commands, animation tools, settings/topics panels, and
automation hooks. Secondary windows are first-class peers; there is no hidden “main window”
special case in the intended architecture.

### 4.4 Multiprocess ownership

```
Python / topic publishers / UI commands
        │
        ▼
Backend daemon
 ├── FigureModel          # authoritative session state
 ├── SessionGraph         # figure↔agent↔client graph
 ├── TopicRegistry        # topic subscriptions/publications
 ├── ProcessManager       # window agent processes
 ├── PythonMessageHandler # Python API requests
 ├── AgentMessageHandler  # window-agent sync
 └── TopicMessageHandler  # topic transport bridge
        │
        ▼
Window agents (`src/agent/main.cpp`) -> WindowRuntime + Renderer + Backend
```

---

## 5. Rendering Pipeline

### 5.1 GPU architecture

- `src/render/backend.hpp` defines the renderer/backend contract.
- `src/render/renderer.*` owns draw orchestration, upload paths, pipeline selection, and text.
- `src/render/vulkan/*` implements swapchains, buffers, frames, pipelines, capture, multi-window, and textures.
- `src/render/webgpu/*` provides the experimental WebGPU path and consumes embedded WGSL shaders.
- `src/render/series_type_registry.*` centralizes series-type to render-path registration.
- Upload-heavy paths are split across `render_upload.cpp`, `render_2d.cpp`, `render_3d.cpp`, and `render_geometry.cpp`.

### 5.2 Shader programs (`src/gpu/shaders/`)

| Shader | Pipeline / backend use | Purpose |
|---|---|---|
| `line.vert` + `line.frag` | Vulkan 2D line pipeline | 2D lines, dash patterns, line joins, markers. |
| `scatter.vert` + `scatter.frag` | Vulkan 2D scatter pipeline | 2D point/marker rendering. |
| `grid.vert` + `grid.frag` | Vulkan grid / overlay pipeline | 2D grids, borders, ticks, overlays. |
| `stat_fill.vert` + `stat_fill.frag` | Vulkan stat-fill pipeline | Histograms, bar fills, violin/box fills. |
| `line3d.vert` + `line3d.frag` | Vulkan 3D line pipeline | Depth-tested 3D lines and trajectories. |
| `scatter3d.vert` + `scatter3d.frag` | Vulkan 3D scatter pipeline | Depth-tested 3D markers with lighting. |
| `mesh3d.vert` + `mesh3d.frag` | Vulkan mesh pipeline | Lit triangle meshes / wireframe variants. |
| `surface3d.vert` + `surface3d.frag` | Vulkan surface pipeline | Surface meshes with colormaps and lighting. |
| `grid3d.vert` + `grid3d.frag` | Vulkan 3D grid pipeline | 3D grid planes, boxes, guides. |
| `arrow3d.vert` + `arrow3d.frag` | Vulkan axis-arrow pipeline | 3D axis arrows and orientation cues. |
| `image3d.vert` + `image3d.frag` | Vulkan image display path | Textured image planes / ROS image display support. |
| `marker3d.vert` + `marker3d.frag` | Vulkan marker display path | 3D markers / display markers beyond base scatter. |
| `pointcloud.vert` + `pointcloud.frag` | Vulkan point-cloud path | Dense point cloud rendering for ROS/PX4-style spatial data. |
| `text.vert` + `text.frag` | Vulkan text / text-depth pipelines | SDF/MSDF text in 2D and 3D overlays. |
| `wgsl/grid.wgsl` | WebGPU grid pipeline | WGSL grid rendering. |
| `wgsl/line.wgsl` | WebGPU line pipeline | WGSL line rendering. |
| `wgsl/scatter.wgsl` | WebGPU scatter pipeline | WGSL point/marker rendering. |
| `wgsl/stat_fill.wgsl` | WebGPU stat-fill pipeline | WGSL filled statistical geometry. |
| `wgsl/text.wgsl` | WebGPU text pipeline | WGSL text / glyph rendering path. |

### 5.3 Per-frame render flow

```
FrameScheduler::begin_frame()
  -> SessionRuntime::tick() / WindowRuntime::update()
     -> figure updates / animation callbacks / topic draining / deferred removals
     -> ImGuiIntegration::new_frame() + UI build
     -> Input / overlay / docking / command handling
  -> WindowRuntime::render()
     -> Backend::begin_frame()
     -> Renderer upload + draw passes (2D, 3D, stats, overlays, text)
     -> ImGui draw data
     -> optional framebuffer capture / export / automation snapshot
     -> Backend::end_frame()
  -> SessionRuntime post-processing (detach/move/close windows)
  -> FrameScheduler::end_frame()
```

---

## 6. Data Flow Diagrams

### 6.1 Series data → CPU caches → GPU

```
User / adapter / topic input
   -> Figure / Axes / Series mutation
   -> dirty flags + optional chunked storage / mapped-file / LOD cache updates
   -> Renderer upload path (`render_upload.cpp`)
   -> backend buffers / textures / descriptors
   -> draw pipelines selected by series type registry
```

### 6.2 Workspace + plugin + view-model flow

```
Workspace file / plugin manifest / command action
   -> Workspace / PluginAPI / PluginGuard
   -> FigureSerializer + OverlaySnapshot
   -> Figure/Axes/Series view models
   -> ImGui panels, inspectors, topic/settings panels
```

### 6.3 IPC / multiprocess / topic transport

```
Python client / topic publisher / automation client
        │
        ├── TLV codec (`codec.*`) and FlatBuffers codec (`codec_fb.*`)
        ├── Unix-socket transport (`transport.*`)
        └── PublisherClient / message routing
                │
                ▼
           daemon server
                │
                ├── figure state diffs
                ├── topic registry updates
                └── agent sync / automation responses
```

---

## 7. UI System (ImGui-based)

- `src/ui/imgui/` is the main UI composition layer: menu bars, panes, selections, dialogs, previews, panels.
- `src/ui/figures/`, `src/ui/docking/`, and `src/ui/window/` make multi-window + tab tear-off + split views work.
- `src/ui/overlay/` contains interaction overlays (crosshair, annotations, inspector, legend, tooltips, markers).
- `src/ui/commands/` and `src/ui/app/commands/` split generic command plumbing from command-domain registration.
- `src/ui/accessibility/`, `src/ui/settings/`, and `src/ui/topics/` add non-plot UI subsystems missing from the older map.
- `src/ui/automation/` exposes the UI to automation/MCP-style remote control.

Canonical window layout:

```
Menu / command bar
  -> docked panes + tab bars
  -> plot canvases + overlays + inspectors + side panels
  -> status/perf/topic/settings surfaces
```

---

## 8. Key Internal Classes

### 8.1 `FigureRegistry` / `FigureManager`
- Registry owns figure lifetimes; manager owns per-window tab/pane state, ordering, duplication, and transfers.
- `FigureViewModel`, `AxesViewModel`, and `SeriesViewModel` provide UI-facing state projections.

### 8.2 `DockSystem` / `SplitViewManager` / `PanelDetachController`
- These files implement dock trees, split panes, drop targets, detached panels, and pane-local tab bars.

### 8.3 `InputHandler` / `GestureRecognizer` / `SelectionContext`
- Input is split by responsibility (`input_pan_zoom.cpp`, `input_select.cpp`, `input_measure.cpp`, `input_annotate.cpp`).

### 8.4 `OverlayRegistry` + overlay widgets
- Central registration for overlay behavior; concrete overlays include annotations, crosshair, data editor, markers, inspector, tooltip, and legend interaction.

### 8.5 `DataTransform` / `TransformPipeline` / `ExpressionEval`
- `src/math/data_transform.*` handles reusable transforms; `src/math/expression_eval.*` adds expression parsing/evaluation shared with ROS2 expression plotting.

### 8.6 `ChunkedArray` / `ChunkedSeries` / `LodCache` / `MappedFile`
- These files are the large-data pipeline: chunked storage, memory-mapped data sources, and level-of-detail caches for scalable plotting.

### 8.7 `FrameScheduler` / `FrameProfiler` / `AnimationTickGate`
- Frame pacing, profiling, and redraw throttling are now spread across `src/anim/` and `src/ui/app/animation_tick_gate.hpp`.

### 8.8 `Renderer` / `SeriesTypeRegistry` / `TextRenderer`
- `Renderer` is the central draw orchestrator; `SeriesTypeRegistry` decouples series type from render path; `TextRenderer` manages glyph batching.

### 8.9 `VulkanBackend`
- Owns device/swapchain/frame/pipeline/buffer/texture subsystems, multi-window render state, and capture/readback.

### 8.10 `WebGPUBackend`
- Experimental backend in `src/render/webgpu/`; paired with WGSL embedding and tests such as `test_webgpu_backend.cpp`.

### 8.11 IPC codecs (`codec.*`, `codec_fb.*`)
- Spectra now ships both legacy/TLV and FlatBuffers codecs. Cross-codec tests verify compatibility (`test_cross_codec`, Python cross-codec tests, benchmark codec).

### 8.12 `AutomationServer` / `McpServer`
- Remote-control surface for screenshots, window management, command execution, input synthesis, and figure inspection.

### 8.13 `PluginAPI` / `PluginManifest` / `PluginGuard`
- The plugin subsystem now covers exported data sources, overlays, transforms, and series-type extension points with manifest/version negotiation.

### 8.14 Accessibility subsystem
- `AccessibleSummary` generates non-visual summaries; `Sonification` turns plot structure into auditory cues.

### 8.15 Settings subsystem
- `SettingsStore` persists settings; `SettingsPanel` exposes them in the UI.

### 8.16 PX4 subsystem
- `Px4Bridge`, `Px4PlotManager`, `Px4AppShell`, telemetry adapters, and ULog parsing form a parallel adapter stack beside ROS2.

### 8.17 Topic subsystem
- `include/spectra/topic.hpp`, `src/app/inproc_topic_server.*`, `src/ipc/publisher_client.cpp`, `src/daemon/topic_*`, and `src/ui/topics/topics_panel.*` together provide pub/sub streaming.

---

## 9. Export System

| Format | Primary implementation | Notes |
|---|---|---|
| PNG | `src/io/png_export.cpp` | GPU readback / offscreen export path. |
| SVG | `src/io/svg_export.cpp` | CPU-side vector export from scene graph / series data. |
| Video | `src/io/video_export.cpp`, `src/io/ffmpeg_command.hpp` | ffmpeg-backed frame streaming / recording export. |
| Registry | `src/io/export_registry.*` | Central export capability registration. |
| UI wiring | `src/ui/animation/recording_export.*`, `src/ui/export_dialog.hpp` | Recording workflow and dialog-level integration. |

---

## 10. Python Bindings (`python/`)

- `python/spectra/` contains 21 checked-in core module files and the public `embed.py` / `topic.py` entry points.
- New since the old map: `_codec_fb.py`, `_download.py`, and `topic.py`.
- `python/spectra/_fb_generated/` is reserved for generated FlatBuffers Python classes (currently an empty checked-in placeholder directory).
- `python/spectra/backends/` contains Qt/Agg backend glue for embedding.
- `python/examples/` now contains 18 demos, including topic publish/subscribe samples.
- `python/tests/` now contains 12 test modules plus package init, including `test_cross_codec.py`, `test_download.py`, and `test_windows_compat.py`.

Typical Python flow:

```
Python API -> _codec / _codec_fb -> _transport -> daemon/backend launcher
          -> figure/session/topic proxies -> window agents / embed surface
```

---

## 11. Build System

### 11.1 CMake options

| Option | Default | Purpose |
|---|---|---|
| `SPECTRA_USE_GLFW` | ON | GLFW windowing adapter. |
| `SPECTRA_USE_SDL3` | OFF | SDL3 windowing adapter. |
| `SPECTRA_USE_IMGUI` | ON | Dear ImGui UI. |
| `SPECTRA_USE_QT` | OFF | Qt adapter scaffolding. |
| `SPECTRA_USE_FFMPEG` | ON | Video export via ffmpeg. |
| `SPECTRA_USE_EIGEN` | ON | Eigen vector adapters. |
| `SPECTRA_USE_ROS2` | ON | ROS2 adapter. |
| `SPECTRA_ROS2_BAG` | ON | rosbag2 support. |
| `SPECTRA_USE_PX4` | ON | PX4/ULog adapter stack. |
| `SPECTRA_USE_WEBGPU` | OFF | WebGPU/WGSL backend. |
| `SPECTRA_BUILD_EXAMPLES` | ON | Build examples. |
| `SPECTRA_BUILD_QT_EXAMPLE` | OFF | Build Qt embed example. |
| `SPECTRA_BUILD_TESTS` | ON | Build unit tests + benchmarks + golden tests. |
| `SPECTRA_BUILD_EMBED_SHARED` | OFF | Build shared `spectra_embed` FFI library. |
| `SPECTRA_PYTHON_WHEEL` | OFF | Install backend into Python package wheel layout. |
| `SPECTRA_RUNTIME_MODE` | `multiproc` | `inproc` or `multiproc`. |
| `SPECTRA_PRESENT_MODE` | `FIFO` | Vulkan present mode (`FIFO`, `MAILBOX`, `IMMEDIATE`). |

### 11.2 Key build targets

- `spectra-core` — headless/core plotting, math, data, I/O, animation primitives.
- `spectra` — full renderer/UI library.
- `spectra_qt_adapter` — Qt embedding support.
- `spectra_ros2_adapter` and `spectra-ros` — ROS2 adapter library + app.
- `spectra_px4_adapter` and `spectra-px4` — PX4 adapter library + app.
- `spectra-ipc` and `spectra-render` — interface/helper targets exposed by CMake.
- `spectra_embed` — optional shared FFI/embed library.
- `spectra-app` — standalone native application.
- `spectra-backend` — multiprocess backend daemon.
- `spectra-window` — multiprocess window agent.
- `spectra_wgsl_shaders` — WGSL embedding/codegen target.
- Example, unit-test, benchmark, golden-test, and QA executables.

### 11.3 CMake modules

- `cmake/CompileShaders.cmake` — GLSL → SPIR-V compilation.
- `cmake/CompileFlatBuffers.cmake` — FlatBuffers code generation.
- `cmake/EmbedAssets.cmake` — binary/font/icon embedding.
- `cmake/EmbedShaders.cmake` — compiled shader embedding.
- `cmake/EmbedWGSLShaders.cmake` — WGSL source embedding.
- `cmake/spectraConfig.cmake.in` — package config export.
- `cmake/version.hpp.in` — generated version header template.

---

## 12. Threading Model

- Rendering is primarily frame-driven per window; backends own GPU resource lifetime and defer destruction safely.
- Registry/graph/state managers (`FigureRegistry`, `SessionGraph`, `FigureModel`, `ProcessManager`, `TopicRegistry`) are explicit synchronization points.
- UI, input, and most figure mutations are intended to occur on the owning window/app thread.
- ROS2/PX4/topic/automation paths can feed data asynchronously, but visible model mutations still converge through synchronized app/daemon boundaries.

---

## 13. Important Patterns & Gotchas

### 13.1 Deferred GPU deletion
- Renderer and backend use deferred resource-free queues/rings; do not destroy GPU resources directly on the app thread.

### 13.2 Multi-window peer semantics
- Secondary windows are peers; avoid introducing “primary window only” behavior in rendering, input, docking, or teardown paths.

### 13.3 Dual-codec IPC
- The repository now carries TLV and FlatBuffers encode/decode paths. Cross-codec tests/benchmarks exist and should stay green together.

### 13.4 WebGPU is intentionally constrained
- WebGPU support is experimental and currently documented as inproc-first / wasm-friendly rather than a full replacement for Vulkan/multiproc.

### 13.5 Plugin ABI/version discipline
- Plugin loading is guarded by manifests, compatibility checks, and dedicated tests; extension points are intentionally narrow.

### 13.6 Topic pipeline is cross-cutting
- Topics touch public API, app runtime, daemon routing, Python, examples, and docs. Changes often need coordinated updates across those layers.

### 13.7 Accessibility/settings/automation are now first-class subsystems
- They are not add-ons hidden inside `imgui_integration.cpp`; the source tree has dedicated directories and tests for them.

---

## 14. File Cross-Reference (Complete Inventory)

This section is the exhaustive source-tree index for authored code and key repo assets. Baseline image corpora
and other large binary collections are summarized rather than enumerated file-by-file.

### 14.1 Repository root (18 files)
`.clang-format`, `.clang-tidy`, `.clangd`, `.dockerignore`, `.gitignore`, `.live-agents`,
    `CLAUDE.md`, `CMakeLists.txt`, `CODEBASE_MAP.md`, `FORMAT.md`, `LICENSE`, `Makefile`,
    `Makefile.format`, `README.md`, `Vision.png`, `format_project.sh`, `pyproject.toml`,
    `version.txt`

### 14.2 `include/spectra/` (32 files)
`animator.hpp`, `app.hpp`, `axes.hpp`, `axes3d.hpp`, `camera.hpp`, `chunked_series.hpp`,
    `color.hpp`, `custom_series.hpp`, `easy.hpp`, `easy_embed.hpp`, `eigen.hpp`, `eigen_easy.hpp`,
    `embed.hpp`, `event_bus.hpp`, `export.hpp`, `figure.hpp`, `figure_registry.hpp`, `frame.hpp`,
    `fwd.hpp`, `knob_manager.hpp`, `logger.hpp`, `math3d.hpp`, `plot_style.hpp`, `series.hpp`,
    `series3d.hpp`, `series_shapes.hpp`, `series_shapes3d.hpp`, `series_stats.hpp`, `spectra.hpp`,
    `spectra_embed_c.h`, `theme_api.hpp`, `topic.hpp`

### 14.3 `src/app/` (3 files)
`inproc_topic_server.cpp`, `inproc_topic_server.hpp`, `main.cpp`
Contains the standalone app entry point plus the in-process topic server.

### 14.4 `src/core/` (18 files)
`axes.cpp`, `axes3d.cpp`, `axes3d.hpp`, `chunked_series.cpp`, `custom_series.cpp`, `figure.cpp`,
    `layout.cpp`, `layout.hpp`, `logger.cpp`, `pending_series_data.hpp`, `series.cpp`,
    `series3d.cpp`, `series_shapes.cpp`, `series_shapes3d.cpp`, `series_stats.cpp`, `spinlock.hpp`,
    `transform.cpp`, `transform.hpp`

### 14.5 `src/render/` (11 files)
`backend.hpp`, `render_2d.cpp`, `render_3d.cpp`, `render_geometry.cpp`, `render_upload.cpp`,
    `renderer.cpp`, `renderer.hpp`, `series_type_registry.cpp`, `series_type_registry.hpp`,
    `text_renderer.cpp`, `text_renderer.hpp`

### 14.6 `src/render/vulkan/` (15 files)
`vk_backend.cpp`, `vk_backend.hpp`, `vk_buffer.cpp`, `vk_buffer.hpp`, `vk_capture.cpp`,
    `vk_device.cpp`, `vk_device.hpp`, `vk_frame.cpp`, `vk_multi_window.cpp`, `vk_pipeline.cpp`,
    `vk_pipeline.hpp`, `vk_swapchain.cpp`, `vk_swapchain.hpp`, `vk_texture.cpp`,
    `window_context.hpp`

### 14.7 `src/render/webgpu/` (2 files)
`wgpu_backend.cpp`, `wgpu_backend.hpp`

### 14.8 `src/ui/` (2 files)
`dialog_env_guard.hpp`, `export_dialog.hpp`
Root-level shared UI utility headers.

### 14.9 `src/ui/app/` (18 files)
`animation_tick_gate.hpp`, `app.cpp`, `app_inproc.cpp`, `app_multiproc.cpp`, `app_step.cpp`,
    `perf_metrics.hpp`, `redraw_tracker.hpp`, `register_commands.cpp`, `register_commands.hpp`,
    `resource_monitor.hpp`, `ros2_adapter_state.hpp`, `session_runtime.cpp`, `session_runtime.hpp`,
    `window_runtime.cpp`, `window_runtime.hpp`, `window_ui_context.hpp`,
    `window_ui_context_builder.cpp`, `window_ui_context_builder.hpp`

### 14.10 `src/ui/app/commands/` (15 files)
`command_context.hpp`, `command_descriptor.cpp`, `command_descriptor.hpp`, `command_groups.hpp`,
    `commands_animation.cpp`, `commands_app.cpp`, `commands_data.cpp`, `commands_edit.cpp`,
    `commands_figure.cpp`, `commands_file.cpp`, `commands_panel.cpp`, `commands_series.cpp`,
    `commands_theme.cpp`, `commands_tools.cpp`, `commands_view.cpp`

### 14.11 `src/ui/imgui/` (14 files)
`axes3d_renderer.cpp`, `axes3d_renderer.hpp`, `imgui_animation.cpp`, `imgui_command_bar.cpp`,
    `imgui_dialogs.cpp`, `imgui_integration.cpp`, `imgui_integration.hpp`,
    `imgui_integration_internal.hpp`, `imgui_pane_tabs.cpp`, `imgui_panels.cpp`,
    `imgui_preview.cpp`, `imgui_selection.cpp`, `widgets.cpp`, `widgets.hpp`

### 14.12 `src/ui/figures/` (8 files)
`figure_manager.cpp`, `figure_manager.hpp`, `figure_registry.cpp`, `figure_registry.hpp`,
    `tab_bar.cpp`, `tab_bar.hpp`, `tab_drag_controller.cpp`, `tab_drag_controller.hpp`

### 14.13 `src/ui/window/` (12 files)
`glfw_adapter.cpp`, `glfw_adapter.hpp`, `glfw_utils.hpp`, `sdl3_adapter.cpp`, `sdl3_adapter.hpp`,
    `sdl3_key_map.hpp`, `window_figure_ops.cpp`, `window_glfw_callbacks.cpp`,
    `window_lifecycle.cpp`, `window_manager.cpp`, `window_manager.hpp`, `window_sdl3_events.cpp`

### 14.14 `src/ui/input/` (13 files)
`box_zoom_overlay.cpp`, `box_zoom_overlay.hpp`, `gesture_recognizer.cpp`, `gesture_recognizer.hpp`,
    `input.cpp`, `input.hpp`, `input_annotate.cpp`, `input_measure.cpp`, `input_pan_zoom.cpp`,
    `input_select.cpp`, `region_select.cpp`, `region_select.hpp`, `selection_context.hpp`

### 14.15 `src/ui/overlay/` (22 files)
`annotation.cpp`, `annotation.hpp`, `crosshair.cpp`, `crosshair.hpp`, `custom_transform_dialog.cpp`,
    `custom_transform_dialog.hpp`, `data_editor.cpp`, `data_editor.hpp`, `data_interaction.cpp`,
    `data_interaction.hpp`, `data_marker.cpp`, `data_marker.hpp`, `inspector.cpp`, `inspector.hpp`,
    `knob_manager.cpp`, `knob_manager.hpp`, `legend_interaction.cpp`, `legend_interaction.hpp`,
    `overlay_registry.cpp`, `overlay_registry.hpp`, `tooltip.cpp`, `tooltip.hpp`

### 14.16 `src/ui/commands/` (16 files)
`command_descriptor.cpp`, `command_descriptor.hpp`, `command_palette.cpp`, `command_palette.hpp`,
    `command_queue.hpp`, `command_registry.cpp`, `command_registry.hpp`, `series_clipboard.cpp`,
    `series_clipboard.hpp`, `shortcut_config.cpp`, `shortcut_config.hpp`, `shortcut_manager.cpp`,
    `shortcut_manager.hpp`, `undo_manager.cpp`, `undo_manager.hpp`, `undoable_property.hpp`

### 14.17 `src/ui/docking/` (4 files)
`dock_system.cpp`, `dock_system.hpp`, `split_view.cpp`, `split_view.hpp`

### 14.18 `src/ui/animation/` (16 files)
`animation_controller.cpp`, `animation_controller.hpp`, `animation_curve_editor.cpp`,
    `animation_curve_editor.hpp`, `camera_animator.cpp`, `camera_animator.hpp`,
    `keyframe_interpolator.cpp`, `keyframe_interpolator.hpp`, `mode_transition.cpp`,
    `mode_transition.hpp`, `recording_export.cpp`, `recording_export.hpp`, `timeline_editor.cpp`,
    `timeline_editor.hpp`, `transition_engine.cpp`, `transition_engine.hpp`

### 14.19 `src/ui/theme/` (6 files)
`design_tokens.hpp`, `icons.cpp`, `icons.hpp`, `theme.cpp`, `theme.hpp`, `theme_api.cpp`

### 14.20 `src/ui/data/` (9 files)
`axis_link.cpp`, `axis_link.hpp`, `clipboard_export.cpp`, `clipboard_export.hpp`, `csv_loader.cpp`,
    `csv_loader.hpp`, `data_transform.hpp`, `html_table_export.cpp`, `html_table_export.hpp`

### 14.21 `src/ui/camera/` (2 files)
`camera.cpp`, `camera.hpp`

### 14.22 `src/ui/layout/` (2 files)
`layout_manager.cpp`, `layout_manager.hpp`

### 14.23 `src/ui/viewmodel/` (6 files)
`axes_view_model.cpp`, `axes_view_model.hpp`, `figure_view_model.cpp`, `figure_view_model.hpp`,
    `series_view_model.cpp`, `series_view_model.hpp`

### 14.24 `src/ui/workspace/` (13 files)
`figure_serializer.cpp`, `figure_serializer.hpp`, `overlay_snapshot.hpp`, `plugin_api.cpp`,
    `plugin_api.hpp`, `plugin_guard.cpp`, `plugin_guard.hpp`, `plugin_manifest.cpp`,
    `plugin_manifest.hpp`, `workspace.cpp`, `workspace.hpp`, `workspace_autosave.cpp`,
    `workspace_autosave.hpp`

### 14.25 `src/ui/accessibility/` (4 files)
`accessible_summary.cpp`, `accessible_summary.hpp`, `sonification.cpp`, `sonification.hpp`

### 14.26 `src/ui/settings/` (4 files)
`settings_panel.cpp`, `settings_panel.hpp`, `settings_store.cpp`, `settings_store.hpp`

### 14.27 `src/ui/topics/` (2 files)
`topics_panel.cpp`, `topics_panel.hpp`

### 14.28 `src/ui/panel/` (2 files)
`panel_detach_controller.cpp`, `panel_detach_controller.hpp`

### 14.29 `src/ui/automation/` (6 files)
`automation_handler.hpp`, `automation_json.hpp`, `automation_server.cpp`, `automation_server.hpp`,
    `mcp_server.cpp`, `mcp_server.hpp`

### 14.30 `src/ui/automation/handlers/` (6 files)
`handlers_capture.cpp`, `handlers_command.cpp`, `handlers_figure.cpp`, `handlers_input.cpp`,
    `handlers_utility.cpp`, `handlers_window.cpp`

### 14.31 `src/embed/` (2 files)
`embed_surface.cpp`, `spectra_embed_c.cpp`

### 14.32 `src/platform/` (2 files)
`clipboard_image.cpp`, `clipboard_image.hpp`

### 14.33 `src/platform/window_system/` (5 files)
`glfw_surface_host.cpp`, `glfw_surface_host.hpp`, `sdl3_surface_host.cpp`, `sdl3_surface_host.hpp`,
    `surface_host.hpp`

### 14.34 `src/anim/` (5 files)
`animator.cpp`, `easing.cpp`, `frame_profiler.hpp`, `frame_scheduler.cpp`, `frame_scheduler.hpp`

### 14.35 `src/data/` (8 files)
`chunked_array.hpp`, `decimation.cpp`, `decimation.hpp`, `filters.cpp`, `filters.hpp`,
    `lod_cache.hpp`, `mapped_file.cpp`, `mapped_file.hpp`

### 14.36 `src/math/` (4 files)
`data_transform.cpp`, `data_transform.hpp`, `expression_eval.cpp`, `expression_eval.hpp`

### 14.37 `src/io/` (7 files)
`export_registry.cpp`, `export_registry.hpp`, `ffmpeg_command.hpp`, `png_export.cpp`,
    `stb_impl.cpp`, `svg_export.cpp`, `video_export.cpp`

### 14.38 `src/ipc/` (9 files)
`blob_store.hpp`, `codec.cpp`, `codec.hpp`, `codec_fb.cpp`, `codec_fb.hpp`, `message.hpp`,
    `publisher_client.cpp`, `transport.cpp`, `transport.hpp`

### 14.39 `src/ipc/schemas/` (1 files)
`spectra_ipc.fbs`

### 14.40 `src/daemon/` (20 files)
`agent_message_handler.cpp`, `agent_message_handler.hpp`, `client_router.hpp`, `daemon_server.cpp`,
    `daemon_server.hpp`, `figure_model.cpp`, `figure_model.hpp`, `heartbeat_monitor.cpp`,
    `heartbeat_monitor.hpp`, `main.cpp`, `process_manager.cpp`, `process_manager.hpp`,
    `python_message_handler.cpp`, `python_message_handler.hpp`, `session_graph.cpp`,
    `session_graph.hpp`, `topic_message_handler.cpp`, `topic_message_handler.hpp`,
    `topic_registry.cpp`, `topic_registry.hpp`

### 14.41 `src/agent/` (1 files)
`main.cpp`

### 14.42 `src/gpu/shaders/` (28 files)
`arrow3d.frag`, `arrow3d.vert`, `grid.frag`, `grid.vert`, `grid3d.frag`, `grid3d.vert`,
    `image3d.frag`, `image3d.vert`, `line.frag`, `line.vert`, `line3d.frag`, `line3d.vert`,
    `marker3d.frag`, `marker3d.vert`, `mesh3d.frag`, `mesh3d.vert`, `pointcloud.frag`,
    `pointcloud.vert`, `scatter.frag`, `scatter.vert`, `scatter3d.frag`, `scatter3d.vert`,
    `stat_fill.frag`, `stat_fill.vert`, `surface3d.frag`, `surface3d.vert`, `text.frag`, `text.vert`

### 14.43 `src/gpu/shaders/wgsl/` (5 files)
`grid.wgsl`, `line.wgsl`, `scatter.wgsl`, `stat_fill.wgsl`, `text.wgsl`

### 14.44 `src/adapters/` (3 files)
`adapter_interface.hpp`, `data_source_registry.cpp`, `data_source_registry.hpp`

### 14.45 `src/adapters/qt/` (4 files)
`qt_runtime.cpp`, `qt_runtime.hpp`, `qt_surface_host.cpp`, `qt_surface_host.hpp`

### 14.46 `src/adapters/px4/` (11 files)
`main.cpp`, `px4_adapter.cpp`, `px4_adapter.hpp`, `px4_app_shell.cpp`, `px4_app_shell.hpp`,
    `px4_bridge.cpp`, `px4_bridge.hpp`, `px4_plot_manager.cpp`, `px4_plot_manager.hpp`,
    `ulog_reader.cpp`, `ulog_reader.hpp`

### 14.47 `src/adapters/px4/messages/` (5 files)
`actuator_adapter.hpp`, `attitude_adapter.hpp`, `battery_adapter.hpp`, `gps_adapter.hpp`,
    `imu_adapter.hpp`

### 14.48 `src/adapters/px4/ui/` (4 files)
`live_connection_panel.cpp`, `live_connection_panel.hpp`, `ulog_file_panel.cpp`,
    `ulog_file_panel.hpp`

### 14.49 `src/adapters/ros2/` (40 files)
`axis_mode.hpp`, `bag_player.cpp`, `bag_player.hpp`, `bag_reader.cpp`, `bag_reader.hpp`,
    `bag_recorder.cpp`, `bag_recorder.hpp`, `expression_engine.cpp`, `expression_engine.hpp`,
    `expression_plot.cpp`, `expression_plot.hpp`, `generic_subscriber.cpp`,
    `generic_subscriber.hpp`, `main.cpp`, `message_introspector.cpp`, `message_introspector.hpp`,
    `ros2_adapter.cpp`, `ros2_adapter.hpp`, `ros2_bridge.cpp`, `ros2_bridge.hpp`,
    `ros_app_shell.cpp`, `ros_app_shell.hpp`, `ros_clipboard_export.cpp`,
    `ros_clipboard_export.hpp`, `ros_csv_export.cpp`, `ros_csv_export.hpp`, `ros_log_viewer.cpp`,
    `ros_log_viewer.hpp`, `ros_plot_manager.cpp`, `ros_plot_manager.hpp`,
    `ros_screenshot_export.cpp`, `ros_screenshot_export.hpp`, `ros_session.cpp`, `ros_session.hpp`,
    `service_caller.cpp`, `service_caller.hpp`, `subplot_manager.cpp`, `subplot_manager.hpp`,
    `topic_discovery.cpp`, `topic_discovery.hpp`

### 14.50 `src/adapters/ros2/display/` (21 files)
`display_plugin.hpp`, `display_registry.cpp`, `display_registry.hpp`, `grid_display.cpp`,
    `grid_display.hpp`, `image_display.cpp`, `image_display.hpp`, `laserscan_display.cpp`,
    `laserscan_display.hpp`, `marker_display.cpp`, `marker_display.hpp`, `path_display.cpp`,
    `path_display.hpp`, `pointcloud_display.cpp`, `pointcloud_display.hpp`, `pose_display.cpp`,
    `pose_display.hpp`, `robot_model_display.cpp`, `robot_model_display.hpp`, `tf_display.cpp`,
    `tf_display.hpp`

### 14.51 `src/adapters/ros2/messages/` (7 files)
`image_adapter.hpp`, `joint_state_adapter.hpp`, `laserscan_adapter.hpp`, `marker_adapter.hpp`,
    `path_adapter.hpp`, `pointcloud_adapter.hpp`, `tf_adapter.hpp`

### 14.52 `src/adapters/ros2/scene/` (6 files)
`mesh_primitives.cpp`, `mesh_primitives.hpp`, `scene_manager.cpp`, `scene_manager.hpp`,
    `scene_renderer.cpp`, `scene_renderer.hpp`

### 14.53 `src/adapters/ros2/tf/` (2 files)
`tf_buffer.cpp`, `tf_buffer.hpp`

### 14.54 `src/adapters/ros2/urdf/` (2 files)
`urdf_parser.cpp`, `urdf_parser.hpp`

### 14.55 `src/adapters/ros2/ui/` (32 files)
`bag_info_panel.cpp`, `bag_info_panel.hpp`, `bag_playback_panel.cpp`, `bag_playback_panel.hpp`,
    `diagnostics_panel.cpp`, `diagnostics_panel.hpp`, `displays_panel.cpp`, `displays_panel.hpp`,
    `expression_editor.cpp`, `expression_editor.hpp`, `field_drag_drop.cpp`, `field_drag_drop.hpp`,
    `inspector_panel.cpp`, `inspector_panel.hpp`, `log_viewer_panel.cpp`, `log_viewer_panel.hpp`,
    `node_graph_panel.cpp`, `node_graph_panel.hpp`, `param_editor_panel.cpp`,
    `param_editor_panel.hpp`, `scene_viewport.cpp`, `scene_viewport.hpp`,
    `service_caller_panel.cpp`, `service_caller_panel.hpp`, `tf_tree_panel.cpp`,
    `tf_tree_panel.hpp`, `topic_echo_panel.cpp`, `topic_echo_panel.hpp`, `topic_list_panel.cpp`,
    `topic_list_panel.hpp`, `topic_stats_overlay.cpp`, `topic_stats_overlay.hpp`

### 14.56 `tests/unit/` (164 files)
`mock_integration_plugin.cpp`, `mock_overlay_plugin.cpp`, `mock_plugin_v1_0.cpp`,
    `mock_plugin_v1_1.cpp`, `mock_plugin_v1_99.cpp`, `mock_transform_plugin.cpp`,
    `test_3d_integration.cpp`, `test_3d_pipelines.cpp`, `test_3d_regression.cpp`,
    `test_accessibility.cpp`, `test_accessible_summary.cpp`, `test_animation_controller.cpp`,
    `test_animation_tick_gate.cpp`, `test_axes3d.cpp`, `test_axes_view_model.cpp`,
    `test_axis_link.cpp`, `test_bag_info_panel.cpp`, `test_bag_player.cpp`, `test_bag_reader.cpp`,
    `test_bag_recorder.cpp`, `test_box_zoom_overlay.cpp`, `test_camera.cpp`,
    `test_camera_animator.cpp`, `test_chunked_array.cpp`, `test_chunked_series.cpp`,
    `test_clipboard_export.cpp`, `test_command_palette_registry.cpp`, `test_command_queue.cpp`,
    `test_command_registry.cpp`, `test_cross_codec.cpp`, `test_csv_loader.cpp`,
    `test_data_editor.cpp`, `test_data_interaction.cpp`, `test_data_transform.cpp`,
    `test_decimation.cpp`, `test_depth_buffer.cpp`, `test_diagnostics_panel.cpp`,
    `test_discovery_introspection.cpp`, `test_display_registry.cpp`, `test_dock_system.cpp`,
    `test_easing.cpp`, `test_easy_api.cpp`, `test_easy_embed.cpp`, `test_eigen_support.cpp`,
    `test_embed_surface.cpp`, `test_event_bus.cpp`, `test_expression_engine.cpp`,
    `test_expression_eval.cpp`, `test_field_drag_drop.cpp`, `test_figure_manager.cpp`,
    `test_figure_registry.cpp`, `test_figure_serializer.cpp`, `test_figure_view_model.cpp`,
    `test_figure_window_api.cpp`, `test_filters.cpp`, `test_generic_subscriber.cpp`,
    `test_gesture_recognizer.cpp`, `test_image_display.cpp`, `test_input.cpp`, `test_inspector.cpp`,
    `test_inspector_stats.cpp`, `test_ipc.cpp`, `test_ipc_flatbuffers.cpp`,
    `test_keyframe_interpolator.cpp`, `test_knob_manager.cpp`, `test_laserscan_display.cpp`,
    `test_layout.cpp`, `test_layout_manager.cpp`, `test_legend_interaction.cpp`,
    `test_lod_cache.cpp`, `test_lod_cache_metrics.cpp`, `test_log_viewer.cpp`,
    `test_mapped_file.cpp`, `test_marker_display.cpp`, `test_math3d.cpp`,
    `test_message_introspector.cpp`, `test_mode_transition.cpp`, `test_multi_window.cpp`,
    `test_node_graph_panel.cpp`, `test_param_editor_panel.cpp`, `test_path_pose_display.cpp`,
    `test_pending_series_data.cpp`, `test_phase2_integration.cpp`, `test_phase3_integration.cpp`,
    `test_phase_a_integration.cpp`, `test_phase_c_integration.cpp`, `test_plot_style.cpp`,
    `test_plugin_api.cpp`, `test_plugin_data_source.cpp`, `test_plugin_diagnostics.cpp`,
    `test_plugin_export.cpp`, `test_plugin_guard.cpp`, `test_plugin_integration.cpp`,
    `test_plugin_manifest.cpp`, `test_plugin_overlays.cpp`, `test_plugin_series_type.cpp`,
    `test_plugin_transforms.cpp`, `test_plugin_version_negotiation.cpp`,
    `test_pointcloud_display.cpp`, `test_process_manager.cpp`, `test_px4_app_shell.cpp`,
    `test_px4_bridge.cpp`, `test_px4_plot_manager.cpp`, `test_python_ipc.cpp`,
    `test_recording_export.cpp`, `test_region_select.cpp`, `test_resize_layout.cpp`,
    `test_ring_buffer.cpp`, `test_robot_model_display.cpp`, `test_ros2_bridge.cpp`,
    `test_ros2_menu_integration.cpp`, `test_ros_app_shell.cpp`, `test_ros_clipboard_export.cpp`,
    `test_ros_csv_export.cpp`, `test_ros_plot_manager.cpp`, `test_ros_screenshot_export.cpp`,
    `test_ros_session.cpp`, `test_scene_manager.cpp`, `test_scene_viewport.cpp`,
    `test_series3d.cpp`, `test_series_clipboard.cpp`, `test_series_data.cpp`,
    `test_series_reorder.cpp`, `test_series_stats.cpp`, `test_series_thread_safe.cpp`,
    `test_series_view_model.cpp`, `test_series_visibility.cpp`, `test_service_caller.cpp`,
    `test_session_graph.cpp`, `test_shared_cursor.cpp`, `test_shortcut_config.cpp`,
    `test_shortcut_manager.cpp`, `test_split_view.cpp`, `test_subplot_manager.cpp`,
    `test_svg_export.cpp`, `test_text_renderer.cpp`, `test_tf_buffer.cpp`, `test_tf_tree_panel.cpp`,
    `test_theme.cpp`, `test_theme_colorblind.cpp`, `test_tick_generation.cpp`,
    `test_timeline_editor.cpp`, `test_topic_discovery.cpp`, `test_topic_echo_panel.cpp`,
    `test_topic_list_panel.cpp`, `test_topic_stats_overlay.cpp`, `test_transform.cpp`,
    `test_transition_engine.cpp`, `test_transparency.cpp`, `test_ui_icons.cpp`,
    `test_ulog_reader.cpp`, `test_ultra_zoom_precision.cpp`, `test_undo_manager.cpp`,
    `test_undo_property.cpp`, `test_undo_redo.cpp`, `test_urdf_parser.cpp`,
    `test_webgpu_backend.cpp`, `test_window_manager.cpp`, `test_window_ui_context_builder.cpp`,
    `test_workspace.cpp`, `test_workspace_3d.cpp`, `test_workspace_autosave.cpp`,
    `test_workspace_v2.cpp`, `test_workspace_v3.cpp`
Includes mock plugin fixtures plus the 164 compiled unit-test sources.

### 14.57 `tests/bench/` (11 files)
`bench_3d.cpp`, `bench_3d_phase3.cpp`, `bench_decimation.cpp`, `bench_ipc_codec.cpp`,
    `bench_multi_window.cpp`, `bench_phase2.cpp`, `bench_phase3.cpp`, `bench_render.cpp`,
    `bench_ros3d.cpp`, `bench_startup.cpp`, `bench_ui.cpp`

### 14.58 `tests/golden/` (6 files)
`golden_test.cpp`, `golden_test_3d.cpp`, `golden_test_3d_phase3.cpp`, `golden_test_phase2.cpp`,
    `golden_test_phase3.cpp`, `image_diff.hpp`
Also contains `baseline/`, the golden-image corpus used by the test sources listed above.

### 14.59 `tests/qa/` (2 files)
`qa_agent.cpp`, `ros_qa_agent.cpp`

### 14.60 `tests/util/` (3 files)
`gpu_hang_detector.hpp`, `multi_window_fixture.hpp`, `validation_guard.hpp`

### 14.61 `examples/` (54 files)
`CMakeLists.txt`, `README_3D.md`, `advanced_animation_demo.cpp`, `animated_scatter.cpp`,
    `axes3d_demo.cpp`, `axes_menu_demo.cpp`, `basic_line.cpp`, `camera_animation_3d.cpp`,
    `camera_animator_demo.cpp`, `comprehensive_subplot_demo.cpp`, `demo_3d.cpp`,
    `easy_api_demo.cpp`, `easy_embed_demo.cpp`, `easy_realtime_demo.cpp`, `eigen_demo.cpp`,
    `embed_cpp_demo.cpp`, `empty_launch_csv.cpp`, `knob_demo.cpp`, `legend_panel_test.cpp`,
    `lit_surface_demo.cpp`, `live_stream.cpp`, `logger_example.cpp`, `mind_blowing_3d.cpp`,
    `mode_transition_demo.cpp`, `multi_figure_demo.cpp`, `multi_subplot.cpp`,
    `multi_window_demo.cpp`, `multi_window_tabs_demo.cpp`, `multiproc_demo.sh`,
    `offscreen_export.cpp`, `parametric_3d_line.cpp`, `plot_styles_demo.cpp`, `plugin_api_demo.cpp`,
    `qt_embed_demo.cpp`, `realtime_3d_stream.cpp`, `ros2_demo.cpp`, `ros2_ui_preview.cpp`,
    `sample_data.csv`, `shapes3d_demo.cpp`, `shapes_demo.cpp`, `shortcut_config_demo.cpp`,
    `shortcut_usage_demo.cpp`, `simple_3d_scatter.cpp`, `stats_demo.cpp`, `surface_3d.cpp`,
    `test_menubar.cpp`, `timeline_animation_demo.cpp`, `timeline_curve_demo.cpp`,
    `topic_publisher.cpp`, `transparency_demo.cpp`, `video_record.cpp`, `webgpu_demo.cpp`,
    `webgpu_shell.html`, `window_resize_test.cpp`
Top-level examples also include support assets (`README_3D.md`, `sample_data.csv`, `webgpu_shell.html`, `multiproc_demo.sh`).

### 14.62 `examples/plugins/` (9 files)
`CMakeLists.txt`, `export_csv.cpp`, `export_csv.plugin.json`, `overlay_crosshair.cpp`,
    `overlay_crosshair.plugin.json`, `series_heatmap.cpp`, `series_heatmap.plugin.json`,
    `transform_smooth.cpp`, `transform_smooth.plugin.json`
Contains four plugin examples plus their `.plugin.json` manifests.

### 14.63 `python/` (4 files)
`MANIFEST.in`, `VERSION`, `imgui.ini`, `pyproject.toml`
Python package metadata/check-in files at the package root.

### 14.64 `python/spectra/` (21 files)
`__init__.py`, `_animation.py`, `_axes.py`, `_blob.py`, `_cli.py`, `_codec.py`, `_codec_fb.py`,
    `_download.py`, `_easy.py`, `_embed.py`, `_errors.py`, `_figure.py`, `_launcher.py`, `_log.py`,
    `_persistence.py`, `_protocol.py`, `_series.py`, `_session.py`, `_transport.py`, `embed.py`,
    `topic.py`

### 14.65 `python/spectra/_fb_generated/` (0 files)
_Checked-in placeholder directory for generated FlatBuffers Python modules._

### 14.66 `python/spectra/backends/` (3 files)
`__init__.py`, `_qt_compat.py`, `backend_qtagg.py`

### 14.67 `python/examples/` (18 files)
`basic_line.py`, `easy_3d.py`, `easy_embed_demo.py`, `easy_embed_pyqt.py`, `easy_live_dashboard.py`,
    `easy_minimal.py`, `easy_multi_live.py`, `easy_multi_tab.py`, `easy_one_liner.py`,
    `easy_showcase.py`, `easy_subplots.py`, `pyqt_embed.py`, `qt_backend_demo.py`,
    `qt_dynamic_demo.py`, `qt_minimal.py`, `streaming_update.py`, `topic_publisher.py`,
    `topic_subscriber.py`

### 14.68 `python/tests/` (13 files)
`__init__.py`, `test_codec.py`, `test_cross_codec.py`, `test_download.py`, `test_easy.py`,
    `test_easy_embed.py`, `test_embed.py`, `test_phase2.py`, `test_phase3.py`, `test_phase4.py`,
    `test_phase5.py`, `test_qt_backend.py`, `test_windows_compat.py`

### 14.69 `cmake/` (7 files)
`CompileFlatBuffers.cmake`, `CompileShaders.cmake`, `EmbedAssets.cmake`, `EmbedShaders.cmake`,
    `EmbedWGSLShaders.cmake`, `spectraConfig.cmake.in`, `version.hpp.in`

### 14.70 `tools/` (7 files)
`architecture_metrics.py`, `find_unused.py`, `generate_atlas.py`, `generate_icon_font.py`,
    `list_github_models.sh`, `run_simplifier_loop.py`, `run_simplifier_loop.sh`

### 14.71 `tools/sanitizers/` (1 files)
`lsan.supp`

### 14.72 `docs/` (18 files)
`2026-04-04-last-3-days-branch-walkthrough.md`, `ARCHITECTURE_REVIEW_V3.md`, `Doxyfile`,
    `architecture.html`, `easy_embed_guide.md`, `examples.html`, `features.html`, `getting-
    started.html`, `getting_started.md`, `gpg-signing.md`, `index.html`,
    `plugin_developer_guide.md`, `px4-adapter.html`, `qt-embed.html`, `ros2-adapter.html`,
    `styles.css`, `topics.md`, `webgpu.html`

### 14.73 `docs/archive/` (2 files)
`ARCHITECTURE_REVIEW.md`, `ARCHITECTURE_REVIEW_V2.md`

### 14.74 `docs/plans/` (4 files)
`2026-04-04-next-3-phases-roadmap.md`, `2026-04-04-window-ui-context-builder-design.md`,
    `2026-04-04-window-ui-context-builder-plan.md`, `CUSTOM_SERIES_DESIGN.md`

### 14.75 `docs/man/` (1 files)
`spectra-backend.1`

### 14.76 `packaging/AppImage/` (2 files)
`AppImageBuilder.yml`, `build-appimage.sh`

### 14.77 `packaging/apt/` (3 files)
`README.md`, `distributions`, `options`

### 14.78 `packaging/aur/` (1 files)
`PKGBUILD`

### 14.79 `packaging/completions/` (3 files)
`spectra-backend.bash`, `spectra-backend.fish`, `spectra-backend.zsh`

### 14.80 `packaging/homebrew/` (1 files)
`spectra.rb`

### 14.81 `packaging/scoop/` (1 files)
`spectra.json`

### 14.82 `docker/` (1 files)
`spectra-publisher/Dockerfile`

### 14.83 `icons/` (5 files)
`S.png`, `spectra.desktop`, `spectra_banner.png`, `spectra_icon.png`, `spectra_icon_alt.png`

### 14.84 `plans/` (5 files)
`QA_design_review.md`, `QA_results.md`, `QA_update.md`, `ROADMAP.md`, `SPECTRA_TOPICS_PLAN.md`

### 14.85 `plans/archive/` (25 files)
`3D_ARCHITECTURE_PLAN.md`, `DEPLOYMENT_PLAN.md`, `LT8_DATA_VIRTUALIZATION.md`,
    `MULTI_WINDOW_ARCHITECTURE.md`, `PYTHON_IPC_ARCHITECTURE.md`, `PYTHON_USER_AGENT_LOG.md`,
    `QA_agent_instructions.md`, `QA_design_review.md`, `QA_prd.md`, `QA_results.md`, `QA_update.md`,
    `QT_embed_plan.md`, `ROS_UI_FIX_PLAN.md`, `SPECTRA_ROS_BREATHING_PLAN.md`,
    `SPECTRA_ROS_STUDIO_PLAN.md`, `SPECTRA_UI_REDESIGN.md`, `USER_AGENT_LOG.md`,
    `VISUAL_SYSTEM_REDESIGN.md`, `VISUAL_SYSTEM_ROADMAP.md`, `agents_plan.md`,
    `dependency_report.md`, `no_main_window_plan.md`, `plan.md`, `tab-tearoff-plan.md`,
    `upgrade_plan.md`

### 14.86 `skills/` (29 files)
`3d-rendering/SKILL.md`, `code-simplifier/EXPLORATION.md`, `code-simplifier/REPORT.md`, `code-
    simplifier/SKILL.md`, `code-simplifier/agents/openai.yaml`, `data-pipeline/SKILL.md`,
    `graphical-change-workflow/SKILL.md`, `ipc-protocol-dev/SKILL.md`, `python-bindings/SKILL.md`,
    `qa-accessibility-agent/REPORT.md`, `qa-accessibility-agent/SKILL.md`, `qa-api-agent/REPORT.md`,
    `qa-api-agent/SKILL.md`, `qa-designer-agent/REPORT.md`, `qa-designer-agent/SKILL.md`, `qa-
    designer-agent/agents/openai.yaml`, `qa-designer-agent/references/qa-designer-reference.md`,
    `qa-memory-agent/REPORT.md`, `qa-memory-agent/SKILL.md`, `qa-performance-agent/REPORT.md`, `qa-
    performance-agent/SKILL.md`, `qa-performance-agent/agents/openai.yaml`, `qa-performance-
    agent/references/qa-performance-reference.md`, `qa-regression-agent/REPORT.md`, `qa-regression-
    agent/SKILL.md`, `qa-ros-performance-agent/REPORT.md`, `qa-ros-performance-agent/SKILL.md`, `qa-
    ros-performance-agent/agents/openai.yaml`, `qa-ros-performance-agent/references/qa-ros-
    performance-reference.md`
Skill prompts/reports used by specialized agents; not production code, but part of the maintained repo surface.

### 14.87 `third_party/` (12 files)
`Inter-Regular.ttf`, `SpectraIcons.otf`, `SpectraIcons.ttf`, `fa_solid_900.hpp`,
    `icon_font_data.hpp`, `inter_font.hpp`, `stb/stb_image.h`, `stb/stb_image_write.h`,
    `stb/stb_truetype.h`, `tinyfiledialogs.cpp`, `tinyfiledialogs.h`, `vma/vk_mem_alloc.h`

---

## 15. Test Structure

| Directory | Current contents |
|---|---|
| `tests/unit/` | **164** files. Coverage spans core plotting, UI/view-models, IPC, FlatBuffers, accessibility, WebGPU, plugins, ROS2, PX4, workspace persistence, and large-data utilities. |
| `tests/bench/` | **11** benchmark files: 3D, render, startup, ROS3D, decimation, IPC codec, multi-window, and phase regressions. |
| `tests/golden/` | **5 golden test sources** plus `image_diff.hpp` and the `baseline/` image corpus. |
| `tests/qa/` | QA executables: `qa_agent.cpp`, `ros_qa_agent.cpp`. |
| `tests/util/` | Shared utilities: GPU hang detection, multi-window fixtures, validation guards. |
| `python/tests/` | Python API/codec/embed/backend regression tests. |

Representative new coverage areas missing from the old map: accessibility summaries, animation tick gating, view models, LOD cache, FlatBuffers cross-codec behavior, WebGPU backend, PX4 adapter flow, scene/URDF/TF displays, workspace autosave, and plugin ABI/versioning.

---

## 16. Naming Conventions

- Namespaces in active use include `spectra`, `spectra::ui`, `spectra::ipc`, `spectra::daemon`, `spectra::ros2`, and `spectra::px4`.
- Types/classes: `PascalCase` (`FigureManager`, `WebGPUBackend`, `Px4PlotManager`).
- Functions and variables: `snake_case` (`render_plot_text`, `topic_registry_`).
- Members: trailing underscore (`backend_`, `active_figure_id_`).
- Macros / compile-time toggles: `SPECTRA_*` (`SPECTRA_USE_WEBGPU`, `SPECTRA_LOG_INFO`).
- Files: `snake_case` (`window_ui_context_builder.cpp`, `test_cross_codec.py`).

---

## 17. Embedding & Platform Abstraction

- `include/spectra/embed.hpp` + `src/embed/embed_surface.cpp`: offscreen/foreign-window rendering surface.
- `include/spectra/spectra_embed_c.h` + `src/embed/spectra_embed_c.cpp`: C ABI for FFI consumers.
- `src/platform/window_system/surface_host.hpp`: abstraction for platform surface creation.
- `src/platform/window_system/glfw_surface_host.*` and `sdl3_surface_host.*`: concrete host implementations.
- `src/adapters/qt/*`: Qt runtime and surface host integration.
- `src/platform/clipboard_image.*`: platform clipboard image helpers used by export/workflow tooling.

---

## 18. ROS2 Adapter (`src/adapters/ros2/`)

The ROS2 adapter is now a large subsystem rather than a thin bridge. It includes:

- Core session/runtime files: bridge, adapter, app shell, topic discovery, generic subscribers, message introspection, bag player/reader/recorder, expression engine, subplot manager, plot manager, service caller, screenshot/CSV/clipboard export, and ROS session persistence.
- Display plugins under `display/`: grid, image, laserscan, marker, path, pointcloud, pose, robot-model, TF, plus the display registry/plugin contract.
- Message adapters under `messages/`: image, joint state, laserscan, marker, path, pointcloud, TF.
- Scene stack under `scene/`: mesh primitives, scene manager, scene renderer.
- TF and URDF helpers under `tf/` and `urdf/`.
- UI panels under `ui/`: bag info/playback, diagnostics, displays, expression editor, field drag-drop, inspector, log viewer, node graph, param editor, scene viewport, service caller, TF tree, topic echo/list/stats.

Data path summary:

```
ROS topic / bag / service / TF
   -> discovery + introspection + adapters
   -> plot manager / display registry / scene manager
   -> Figure/Axes/Series + overlays + inspectors
   -> Vulkan renderer (and selected 3D/display shader paths)
```

---

## 19. PX4 Adapter (`src/adapters/px4/`)

The PX4 adapter is the second major robotics/telemetry integration stack in the repo.

- `px4_adapter.*`: top-level adapter wiring.
- `px4_app_shell.*`: application shell analogous to the ROS2 shell.
- `px4_bridge.*`: PX4/MAVLink-style bridge and live connection integration.
- `px4_plot_manager.*`: plot-oriented telemetry routing into Spectra figures/series.
- `ulog_reader.*`: ULog parsing/import.
- `messages/*.hpp`: telemetry adapters for actuators, attitude, battery, GPS, and IMU.
- `ui/live_connection_panel.*` and `ui/ulog_file_panel.*`: live telemetry and offline file workflows.
- `main.cpp`: standalone `spectra-px4` entry point when the adapter is enabled in CMake.

PX4 fills the same architectural niche as ROS2, but with a PX4-specific telemetry and ULog ingestion stack.

---

## 20. WebGPU Backend (`src/render/webgpu/`)

- `wgpu_backend.hpp/.cpp` contains the experimental WebGPU backend.
- `src/gpu/shaders/wgsl/*.wgsl` are embedded by `cmake/EmbedWGSLShaders.cmake` into generated headers.
- CMake supports `SPECTRA_USE_WEBGPU=ON`; comments in `CMakeLists.txt` explicitly document native Dawn and wasm/Emscripten use cases.
- Current docs/tests position this backend as incremental and complementary, not a drop-in replacement for Vulkan/multiprocess rendering.
- Verification exists in `tests/unit/test_webgpu_backend.cpp`, docs (`docs/webgpu.html`), and example assets (`examples/webgpu_demo.cpp`, `examples/webgpu_shell.html`).

---

## 21. Automation / MCP Server (`src/ui/automation/`)

- `automation_server.*` exposes scripted UI automation.
- `mcp_server.*` adds MCP-oriented control/inspection endpoints.
- `automation_handler.hpp` and `automation_json.hpp` define request/response plumbing.
- Handler split:
  - `handlers_capture.cpp` — screenshots/capture/export style actions.
  - `handlers_command.cpp` — command execution.
  - `handlers_figure.cpp` — figure queries/manipulation.
  - `handlers_input.cpp` — input synthesis.
  - `handlers_utility.cpp` — helper operations.
  - `handlers_window.cpp` — window lifecycle/selection commands.
- This subsystem is cross-linked with QA, screenshot workflows, and remote-control testing.

---

## 22. Plugin System (`src/ui/workspace/`, `examples/plugins/`, plugin tests)

- `plugin_api.*` defines runtime extension points.
- `plugin_manifest.*` defines plugin metadata / compatibility expectations.
- `plugin_guard.*` enforces safety/version checks during plugin loading.
- `workspace.*`, `workspace_autosave.*`, `figure_serializer.*`, and `overlay_snapshot.hpp` persist plugin-augmented sessions.
- Example plugins: `export_csv`, `overlay_crosshair`, `series_heatmap`, `transform_smooth`.
- Plugin-focused unit coverage includes API, diagnostics, export, overlays, transforms, series types, data sources, integration, manifests, and version negotiation.

The plugin system is no longer just a future hook; it is a tested, documented extension surface with concrete examples.

---

## 23. Topics System

Topics are Spectra’s built-in publish/subscribe streaming mechanism for live data.

- Public API: `include/spectra/topic.hpp`.
- In-process server: `src/app/inproc_topic_server.cpp/.hpp`.
- IPC/client transport: `src/ipc/publisher_client.cpp` and the normal codec/transport layers.
- Daemon-side routing: `src/daemon/topic_registry.*` and `src/daemon/topic_message_handler.*`.
- UI surface: `src/ui/topics/topics_panel.*`.
- Python surface: `python/spectra/topic.py`.
- Examples/docs/plans: `examples/topic_publisher.cpp`, `python/examples/topic_publisher.py`, `python/examples/topic_subscriber.py`, `docs/topics.md`, `plans/SPECTRA_TOPICS_PLAN.md`.

Conceptually:

```
publisher -> topic registry/server -> subscribers / figures / Python / UI panels
```

This subsystem is important because it links easy API usage, multiprocess transport, automation, and adapter-driven live streaming into one consistent model.
