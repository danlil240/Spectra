# Spectra — Comprehensive Codebase Map

> **Purpose**: This document is the single source of truth for understanding the Spectra codebase.
> **All agents MUST read this file before executing any task.**
> Last updated: 2026-04-03

---

## 1. Project Identity

**Spectra** is a GPU-accelerated (Vulkan) scientific/engineering plotting library for C++20.
- Cross-platform: Linux, macOS, Windows
- Supports interactive rendering, offscreen rendering, image/video export
- Supports animated plots (time series, trajectories, live sensor streams) with stable frame pacing
- Thread-safe, modern 2026 UI via Dear ImGui
- Two runtime modes: **inproc** (single-process) and **multiproc** (daemon + agent processes via Unix IPC)
- Python bindings via Unix-socket IPC

---

## 2. Directory Structure

```
Spectra/
├── include/spectra/          # PUBLIC API headers (user-facing)
├── src/
│   ├── core/                 # Figure, Axes, Series implementations
│   ├── render/               # Renderer, Backend abstraction, TextRenderer
│   │   └── vulkan/           # VulkanBackend, VkSwapchain, VkBuffer, VkPipeline, WindowContext
│   ├── app/                  # Standalone app entry point (main.cpp)
│   ├── ui/
│   │   ├── app/              # App lifecycle: app.cpp, app_inproc, app_multiproc, app_step,
│   │   │                     #   SessionRuntime, WindowRuntime, WindowUIContext, register_commands
│   │   ├── imgui/            # ImGuiIntegration (main UI), Axes3DRenderer, Widgets
│   │   ├── figures/          # FigureRegistry, FigureManager, TabBar, TabDragController
│   │   ├── window/           # WindowManager, GlfwAdapter, GlfwUtils
│   │   ├── input/            # InputHandler, BoxZoomOverlay, GestureRecognizer, RegionSelect,
│   │   │                     #   SelectionContext
│   │   ├── overlay/          # Crosshair, DataEditor, DataInteraction, DataMarker, Inspector,
│   │   │                     #   KnobManager, LegendInteraction, Tooltip
│   │   ├── commands/         # CommandRegistry, CommandPalette, CommandQueue, ShortcutManager,
│   │   │                     #   UndoManager, SeriesClipboard, ShortcutConfig, UndoableProperty
│   │   ├── docking/          # DockSystem, SplitViewManager, SplitPane
│   │   ├── automation/       # UI automation and scripted interaction hooks
│   │   ├── animation/        # AnimationController, TimelineEditor, KeyframeInterpolator,
│   │   │                     #   AnimationCurveEditor, CameraAnimator, ModeTransition,
│   │   │                     #   RecordingExport, TransitionEngine
│   │   ├── panel/            # Dedicated UI panels and panel helpers
│   │   ├── theme/            # ThemeManager, Theme, DesignTokens, Icons
│   │   ├── data/             # AxisLinkManager, CsvLoader, ClipboardExport
│   │   ├── camera/           # Camera implementation
│   │   ├── layout/           # LayoutManager
│   │   ├── viewmodel/        # View-model layer used by ImGui-facing UI code
│   │   ├── welcome/          # Welcome/start screens and onboarding flows
│   │   └── workspace/        # Workspace save/load, FigureSerializer, PluginAPI
│   ├── embed/                # EmbedSurface impl, C FFI wrapper
│   ├── platform/
│   │   └── window_system/    # SurfaceHost abstraction, GlfwSurfaceHost
│   ├── adapters/
│   │   ├── qt/               # QtRuntime, QtSurfaceHost (Qt embedding)
│   │   └── ros2/             # ROS2 adapter: RosAppShell, BagPlayer, ExpressionEngine,
│   │                         #   TopicDiscovery, GenericSubscriber, SubplotManager, etc.
│   │        └── ui/          # ROS2 UI panels: topic list, diagnostics, TF tree,
│   │                         #   bag playback, param editor, service caller, etc.
│   ├── anim/                 # Animator, Easing, FrameScheduler, FrameProfiler
│   ├── data/                 # Decimation (LTTB, min-max), Filters
│   ├── math/                 # DataTransform, TransformPipeline, TransformRegistry
│   ├── io/                   # PngExport, SvgExport, VideoExport, STB impl
│   ├── ipc/                  # Codec, Message, Transport, BlobStore (Unix socket IPC)
│   ├── daemon/               # Backend daemon: main, FigureModel, SessionGraph, ProcessManager
│   ├── agent/                # Window agent process entry point
│   └── gpu/shaders/          # GLSL shaders (compiled to SPIR-V)
├── tests/
│   ├── unit/                 # ~145 unit test files
│   ├── bench/                # Benchmarks (3D, decimation, multi-window, etc.)
│   ├── golden/               # Golden image regression tests
│   ├── qa/                   # QA agent (automated visual testing, ROS QA)
│   └── util/                 # Test utilities (GPU hang detector, fixtures, validation guard)
├── examples/                 # ~47 example programs
├── python/                   # Python bindings (spectra package)
│   ├── spectra/              # Python module (19 core files + qt backend)
│   ├── examples/             # Python examples (16 demos)
│   └── tests/                # Python tests (11 test files)
├── third_party/              # STB, VMA, fonts
├── plans/                    # Architecture plans (markdown)
├── cmake/                    # CMake modules (shader compilation, asset embedding)
├── tools/                    # Dev tools (find_unused, generate_atlas, icon font gen)
├── skills/                   # Specialized skill agents (14 domains)
├── packaging/                # Distribution: AppImage, AUR, Homebrew, Scoop, completions
├── docs/                     # HTML docs, man pages, getting started guide
└── icons/                    # Desktop icons and banner
```

---

## 3. Public API (include/spectra/)

These are the user-facing headers. Everything in `src/` is internal.

| Header | Contents |
|---|---|
| `spectra.hpp` | Master include — pulls in all public headers |
| `easy.hpp` | MATLAB-style free-function API: `plot()`, `scatter()`, `show()`, `title()`, `subplot()`, `figure()`, `tab()`, `on_update()`, `plot3()`, `surf()`, etc. 7 progressive complexity levels. |
| `app.hpp` | `App` class — top-level application object. Owns `FigureRegistry`, `Backend`, `Renderer`. Two run modes: `run()` (blocking) or `init_runtime()`/`step()`/`shutdown_runtime()` (frame-by-frame). |
| `figure.hpp` | `Figure` class — owns `Axes`/`Axes3D` subplots, style, legend config, animation state. `AnimationBuilder` for fluent animation setup. |
| `axes.hpp` | `AxesBase` (abstract), `Axes` (2D) — owns `Series` vector, axis limits, tick computation, grid, border. |
| `axes3d.hpp` | `Axes3D` : `AxesBase` — 3D axes with Camera, grid planes, bounding box, lighting, zoom/pan on limits. |
| `series.hpp` | `Series` (abstract base), `LineSeries`, `ScatterSeries` — 2D data series with fluent API, dirty tracking. `Rect` struct. |
| `series3d.hpp` | `LineSeries3D`, `ScatterSeries3D`, `SurfaceSeries`, `MeshSeries` — 3D series with colormaps, materials, blend modes, wireframe. |
| `series_stats.hpp` | `BoxPlotSeries`, `ViolinSeries`, `HistogramSeries`, `BarSeries` — statistical plot types with auto-generated geometry. |
| `camera.hpp` | `Camera` — orbit camera (azimuth/elevation/distance), perspective/orthographic projection, serialize/deserialize. |
| `color.hpp` | `Color` struct (RGBA float), named colors (`colors::red`, etc.), `palette::default_cycle` (10 colors). |
| `plot_style.hpp` | `LineStyle`, `MarkerStyle`, `PlotStyle`, `DashPattern`, `parse_format_string()` (MATLAB-style "r--o"). |
| `animator.hpp` | `Animator`, `Keyframe<T>`, easing functions (`ease::linear`, `ease_in_out`, `bounce`, `elastic`, `CubicBezier`). |
| `frame.hpp` | `Frame` struct — `elapsed_sec`, `dt`, `number`, `paused`. |
| `export.hpp` | `ImageExporter::write_png()`, `SvgExporter::write_svg()`, `VideoExporter` (ffmpeg pipe). |
| `math3d.hpp` | `vec3`, `vec4`, `mat4` — minimal math types (no Eigen dependency). |
| `eigen.hpp` | Optional Eigen adapters. |
| `logger.hpp` | `Logger` singleton, `LogLevel`, sink system, `SPECTRA_LOG_*` macros. |
| `fwd.hpp` | Forward declarations for all classes — **91 entries**. |
| `easy_embed.hpp` | One-liner offscreen rendering: `RenderedImage`, `RenderOptions`, `SeriesDesc`, `render()`, `render_scatter()`. |
| `embed.hpp` | `EmbedSurface` class — CPU readback (`render_to_buffer`) and Vulkan interop (`render_to_image`); `EmbedConfig`, `VulkanInteropInfo`. |
| `eigen_easy.hpp` | Eigen overload templates for all easy.hpp functions (`plot`, `scatter`, `plot3`); header-only. |
| `spectra_embed_c.h` | Pure-C FFI: opaque handles `SpectraEmbed`, `SpectraFigure`, `SpectraAxes`, `SpectraSeries` for ctypes/Rust/C# bindings. |
| `custom_series.hpp` | Extensible custom series interfaces for user-defined renderable series types. |
| `event_bus.hpp` | Public event bus contracts and event dispatch interfaces. |
| `figure_registry.hpp` | Public figure registry APIs used for figure lookup and lifecycle control. |
| `knob_manager.hpp` | Public knob/parameter-control integration APIs. |
| `series_shapes.hpp` | 2D shape-oriented series types and helpers. |
| `series_shapes3d.hpp` | 3D shape-oriented series types and helpers. |
| `theme_api.hpp` | Public theme selection and theme API surface. |

---

## 4. Class Hierarchy & Ownership

### 4.1 Core Data Model

```
App
 ├── FigureRegistry            (thread-safe, monotonic uint64 IDs, owns Figure unique_ptrs)
 │    └── Figure               (owns Axes/Axes3D vectors, style, legend, animation state)
 │         ├── Axes : AxesBase  (2D: owns Series vector, xlim/ylim, ticks)
 │         │    ├── LineSeries : Series
 │         │    ├── ScatterSeries : Series
 │         │    ├── BoxPlotSeries : Series
 │         │    ├── ViolinSeries : Series
 │         │    ├── HistogramSeries : Series
 │         │    └── BarSeries : Series
 │         └── Axes3D : AxesBase (3D: owns Camera, xlim/ylim/zlim, lighting)
 │              ├── LineSeries3D : Series
 │              ├── ScatterSeries3D : Series
 │              ├── SurfaceSeries : Series
 │              └── MeshSeries : Series
 ├── Backend (abstract)         → VulkanBackend (Vulkan implementation)
 └── Renderer                   (owns TextRenderer, pipeline handles, per-series GPU data)
```

### 4.2 Runtime Layer (src/ui/app/)

```
App::run()
 └── run_inproc()  OR  run_multiproc()
      │
      ├── SessionRuntime          (session-level orchestration: poll events, iterate windows,
      │    │                       process deferred detaches/moves, check exit conditions)
      │    ├── WindowRuntime       (per-window: update() + render() cycle)
      │    └── FrameScheduler      (frame pacing: TargetFPS / VSync / Uncapped)
      │
      └── Frame-by-frame alternative:
           App::init_runtime() → App::step() → App::shutdown_runtime()
```

### 4.3 Per-Window UI Bundle (WindowUIContext)

Every window (primary + secondary) gets a `WindowUIContext` containing:

```
WindowUIContext                          (src/ui/app/window_ui_context.hpp)
 ├── ImGuiIntegration*                   (ImGui init, full UI: menu bar, inspector, canvas, status bar)
 ├── FigureManager*                      (figure lifecycle, tab switching, per-figure state)
 ├── TabBar*                             (tab widget rendering, drag/reorder/close)
 ├── TabDragController                   (tab tear-off state machine: Idle→DragStart→Detached→Drop)
 ├── DockSystem                          (split view orchestration + drag-to-dock)
 │    └── SplitViewManager               (binary split tree of SplitPanes)
 │         └── SplitPane                 (leaf=figure, internal=two children + ratio)
 ├── DataInteraction*                    (hover/click data point detection)
 ├── BoxZoomOverlay                      (rubber-band zoom rectangle)
 ├── AxisLinkManager                     (linked axes that zoom/pan together)
 ├── CommandRegistry                     (named commands with callbacks)
 ├── CommandPalette                      (Ctrl+P fuzzy command search)
 ├── ShortcutManager                     (keyboard shortcut mapping)
 ├── UndoManager                         (undo/redo stack)
 ├── KnobManager                         (interactive parameter sliders)
 ├── TimelineEditor                      (keyframe timeline UI)
 ├── KeyframeInterpolator                (keyframe evaluation)
 ├── AnimationCurveEditor                (bezier curve editing)
 ├── ModeTransition                      (2D↔3D mode animation)
 ├── AnimationController                 (GLFW-based animation driver)
 ├── GestureRecognizer                   (multi-touch gestures)
 ├── InputHandler                        (mouse/keyboard → axis mutations, tool modes)
 └── SeriesClipboard (shared pointer)    (copy/paste series data)
```

### 4.4 Multi-Window System

```
WindowManager                            (src/ui/window/window_manager.hpp)
 ├── owns vector<unique_ptr<WindowContext>>
 ├── create_initial_window()             (adopts backend's initial WindowContext)
 ├── create_window_with_ui()             (new OS window + full UI stack)
 ├── detach_figure()                     (tab tear-off → new window)
 ├── move_figure()                       (cross-window figure transfer)
 ├── request_preview_window()            (ghost window during drag)
 ├── process_pending_closes()            (deferred window destruction)
 └── shared SeriesClipboard              (all windows share one clipboard)

WindowContext                            (src/render/vulkan/window_context.hpp)
 ├── VkSurfaceKHR surface
 ├── vk::SwapchainContext swapchain
 ├── per-window command buffers + sync objects
 ├── per-window frame UBO
 ├── assigned_figures (vector<FigureId>)
 ├── active_figure_id
 ├── void* imgui_context                 (per-window ImGui context)
 └── unique_ptr<WindowUIContext> ui_ctx  (full UI bundle)
```

---

## 5. Rendering Pipeline

### 5.1 GPU Architecture

```
VulkanBackend : Backend                  (src/render/vulkan/vk_backend.hpp)
 ├── vk::DeviceContext                   (VkInstance, VkDevice, VkPhysicalDevice, queues)
 ├── WindowContext*  active_window_       (multi-window: switch before frame ops)
 ├── VkCommandPool, VkDescriptorPool
 ├── Pipeline layouts (frame UBO + series SSBO + texture)
 ├── Deferred buffer deletion (frame-stamped ring)
 ├── Framebuffer capture (between submit and present)
 └── Headless offscreen framebuffer mode

Renderer                                 (src/render/renderer.hpp)
 ├── TextRenderer                        (SDF font atlas, batched text quads)
 ├── Pipeline handles: line, scatter, grid, overlay, stat_fill, arrow3d,
 │   line3d, scatter3d, mesh3d, surface3d, grid3d, grid_overlay3d,
 │   wireframe variants, transparent variants, text, text_depth
 ├── Per-axes GPU data (grid, border, bbox, tick, arrow buffers + cached limits)
 ├── Per-series GPU data (SSBO, index buffer, fill buffer, type cache)
 └── Deferred series deletion ring (4 slots)
```

### 5.2 Shader Programs (src/gpu/shaders/)

| Shader Pair | Pipeline | Purpose |
|---|---|---|
| `line.vert/frag` | Line | 2D line rendering with dash patterns, markers |
| `scatter.vert/frag` | Scatter | 2D point rendering with marker shapes |
| `grid.vert/frag` | Grid, Overlay | Grid lines, tick marks, 2D overlays |
| `stat_fill.vert/frag` | StatFill | Filled triangles for box/violin/histogram/bar |
| `line3d.vert/frag` | Line3D | 3D lines with depth + optional transparency |
| `scatter3d.vert/frag` | Scatter3D | 3D points with depth + Phong shading |
| `mesh3d.vert/frag` | Mesh3D | Triangle mesh with Phong lighting |
| `surface3d.vert/frag` | Surface3D | Surface mesh with colormaps + lighting |
| `grid3d.vert/frag` | Grid3D | 3D grid planes + bounding box edges |
| `arrow3d.vert/frag` | Arrow3D | 3D axis arrows (depth-tested triangles) |
| `text.vert/frag` | Text, TextDepth | Font atlas textured quads (2D + depth-tested) |

### 5.3 Per-Frame Render Flow

```
1. FrameScheduler::begin_frame()         — timing + dt computation
2. SessionRuntime::tick() for each window:
   a. WindowRuntime::update()
      ├── FigureManager::process_pending()  (tab creates/closes/switches)
      ├── Drive figure animation (on_frame callback)
      ├── flush_deferred_series_removals()
      ├── Figure::compute_layout()
      ├── ImGuiIntegration::new_frame()
      ├── ImGuiIntegration::build_ui()    (menus, inspector, canvas, overlays, tabs)
      ├── DockSystem::update_layout()
      └── InputHandler processing
   b. WindowRuntime::render()
      ├── Backend::begin_frame()          (acquire swapchain image, wait fence)
      ├── Renderer::begin_render_pass()
      ├── Renderer::render_figure_content()  (per-axes: upload series, bind pipeline, draw)
      ├── Renderer::render_plot_text()    (tick labels, axis labels, titles via TextRenderer)
      ├── Renderer::render_plot_geometry() (2D tick marks via overlay pipeline)
      ├── Renderer::render_text()         (flush text vertex batch)
      ├── ImGuiIntegration::render()      (ImGui draw commands)
      ├── Renderer::end_render_pass()
      └── Backend::end_frame()            (submit + present, optional framebuffer capture)
3. Process deferred detaches/moves
4. FrameScheduler::end_frame()            — sleep/spin to hit target FPS
```

---

## 6. Data Flow Diagrams

### 6.1 Series Data → GPU

```
User code:
  ax.line(x, y)  →  creates LineSeries, stores vector<float> x_, y_, sets dirty_=true

Each frame (Renderer::render_figure_content):
  for each axes in figure:
    for each series in axes:
      if series.is_dirty():
        Renderer::upload_series_data(series)
          → interleave x,y into upload_scratch_ (XYXYXY...)
          → Backend::create_buffer(Storage, size) or reuse existing SSBO
          → Backend::upload_buffer(ssbo, data, size)
          → series.clear_dirty()
      Renderer::render_series(series, viewport)
        → Backend::bind_pipeline(line_pipeline)
        → Backend::push_constants(color, line_width, dash_pattern, marker_type)
        → Backend::bind_buffer(ssbo, binding=1)
        → Backend::draw(vertex_count)
```

### 6.2 Figure Lifecycle (Multi-Window)

```
App::figure()
  → FigureRegistry::register_figure() → returns FigureId (monotonic uint64)
  → sibling_map_ tracks which figures share a window (tab grouping)

App::run_inproc()
  → compute_window_groups() → groups figures by sibling relationships
  → WindowManager::create_first_window_with_ui(glfw_window, group[0])
  → For additional groups: WindowManager::create_window_with_ui(...)
  → SessionRuntime loop: tick() until should_exit()

Tab tear-off (during drag):
  TabDragController state machine:
    Idle → DragStartCandidate → DraggingDetached
    → DropOutside: SessionRuntime::queue_detach() → WindowManager::detach_figure()
    → DropOnWindow: SessionRuntime::queue_move() → WindowManager::move_figure()
    → Cancel: restore original state
```

### 6.3 IPC / Multiproc Architecture

```
Python Client ──Unix socket──► Backend Daemon (src/daemon/main.cpp)
                                ├── FigureModel        (authoritative figure state, revisions)
                                ├── SessionGraph       (tracks agents + figure assignments)
                                ├── ProcessManager     (spawns agent processes)
                                └── ClientRouter       (routes messages to correct agent)
                                     │
                          ┌──────────┤
                          ▼          ▼
                    Agent Process  Agent Process    (src/agent/main.cpp)
                    (OS Window 1)  (OS Window 2)

Message types (src/ipc/message.hpp):
  HELLO/WELCOME → handshake
  REQ_CREATE_FIGURE → Python creates figure
  REQ_SET_DATA → Python sets series data
  STATE_SNAPSHOT → full state sync
  STATE_DIFF → incremental update (DiffOp)
  EVT_INPUT → user interaction events

Transport: Unix domain sockets, framed messages (40-byte header + TLV payload)
Codec: encode_message() / decode_message() with PayloadEncoder/PayloadDecoder
```

---

## 7. UI System (ImGui-based)

### 7.1 Layout Structure

```
┌─────────────────────────────────────────────────┐
│  Command Bar (menu bar: File, Edit, View, ...)  │
├───────┬─────────────────────────────┬───────────┤
│       │  Tab Bar (per-pane headers) │           │
│  Nav  ├─────────────────────────────┤ Inspector │
│  Rail │                             │  Panel    │
│       │      Canvas (plot area)     │           │
│       │   [split into SplitPanes]   │           │
│       │                             │           │
├───────┴─────────────────────────────┴───────────┤
│  Status Bar (cursor pos, zoom %, FPS, GPU time) │
└─────────────────────────────────────────────────┘

Canvas can be split (DockSystem → SplitViewManager → binary tree of SplitPanes).
Each SplitPane has its own tab bar with multiple figures.
```

### 7.2 Theme System

```
ThemeManager (singleton)                 (src/ui/theme/theme.hpp)
 ├── Theme { name, ThemeColors, DataPalette, visual props }
 │    └── ThemeColors: ~25 semantic color slots (bg_primary, text_primary, accent, grid_line, etc.)
 ├── DataPalette { colors, colorblind_safe flags }
 ├── CVD simulation (protanopia, deuteranopia, tritanopia, achromatopsia)
 ├── Animated theme transitions
 └── Persistence (export/import JSON)

Design tokens (src/ui/theme/design_tokens.hpp):
  Spacing, border widths, radii, durations, font sizes, z-indices
```

### 7.3 Command System

```
CommandRegistry: named commands with callbacks, category, shortcut hint
CommandPalette: Ctrl+P fuzzy search UI (ranked by score)
ShortcutManager: key binding → command name mapping, configurable
UndoManager: undo/redo stack with UndoableProperty<T> for automatic tracking
ShortcutConfig: persistent shortcut customization (JSON)
```

---

## 8. Key Internal Classes

### 8.1 FigureRegistry (src/ui/figures/figure_registry.hpp)
- Thread-safe figure ownership with monotonic uint64 IDs
- `register_figure()` → `FigureId`, `get()`, `release()`, `unregister_figure()`
- Preserves insertion order for iteration

### 8.2 FigureManager (src/ui/figures/figure_manager.hpp)
- Per-window figure lifecycle: create, close, duplicate, switch, reorder
- Maintains `FigureState` per figure (axis snapshots, inspector selection, scroll, title)
- Wired to `TabBar` for synchronized UI
- Cross-window transfer: `remove_figure()` / `add_figure()` (keeps registry entry)

### 8.3 DockSystem + SplitViewManager (src/ui/docking/)
- `SplitPane`: binary tree node. Leaf = figure(s), internal = two children + ratio
- Multi-figure per pane (per-pane local tab bar)
- `DockSystem`: orchestrates splits + drag-to-dock (drop zones: Left/Right/Top/Bottom/Center)
- Max 8 panes per window

### 8.4 InputHandler (src/ui/input/input.hpp)
- Tool modes: Pan, BoxZoom, Select, Measure
- Mouse/keyboard → axis limit mutations, zoom, scroll
- Hit-tests against multiple axes (finds correct subplot under cursor)
- `CursorReadout` for status bar data coordinates

### 8.5 DataTransform (src/math/data_transform.hpp)
- Transform types: Log10, Ln, Abs, Normalize, Standardize, Derivative, CumulativeSum, FFT, Scale, Offset, Clamp, Custom
- `TransformPipeline`: chain multiple transforms
- `TransformRegistry`: named transform presets

### 8.6 Decimation (src/data/decimation.hpp)
- LTTB (Largest-Triangle-Three-Buckets) — visual shape preservation
- Min-max decimation — peak preservation for waveforms
- Uniform resampling — linear interpolation to even spacing

### 8.7 FrameScheduler (src/anim/frame_scheduler.hpp)
- Modes: TargetFPS (sleep + spin-wait), VSync, Uncapped
- Fixed timestep support for deterministic replay
- Hitch detection: rolling window stats (avg, p95, max, hitch count)

### 8.8 FrameProfiler (src/anim/frame_profiler.hpp)
- DEBUG-only per-stage timing (`SPECTRA_PROFILE_SCOPE` macro)
- Per-stage rolling stats (avg, p95, max)
- Periodic logging summary

---

## 9. Export System

| Format | Class | Mechanism |
|---|---|---|
| PNG | `ImageExporter::write_png()` | STB image write from GPU readback RGBA buffer |
| SVG | `SvgExporter::write_svg()` | CPU-side traversal of Figure→Axes→Series, emits SVG elements directly |
| Video | `VideoExporter` | FFmpeg pipe (`libx264`, `yuv420p`), frame-by-frame RGBA write |

Export flow:
- `Figure::save_png()` sets `png_export_path_` → executed after render
- `Figure::save_svg()` sets `svg_export_path_` → executed after layout
- `AnimationBuilder::record()` sets `video_record_path_` → VideoExporter writes each frame

---

## 10. Python Bindings (python/)

```
python/spectra/
 ├── __init__.py               # Public API re-exports
 ├── _animation.py             # Animation helpers
 ├── _axes.py                  # Axes class (proxy)
 ├── _blob.py                  # Blob data handling
 ├── _cli.py                   # CLI entry points
 ├── _codec.py                 # Python-side message encode/decode
 ├── _easy.py                  # Python easy API (MATLAB-style)
 ├── _embed.py                 # Embedding support
 ├── _errors.py                # Error types
 ├── _figure.py                # Figure class (proxy to daemon)
 ├── _launcher.py              # Backend daemon launcher
 ├── _log.py                   # Logging
 ├── _persistence.py           # Session persistence
 ├── _protocol.py              # Protocol constants
 ├── _series.py                # Series class (proxy)
 ├── _session.py               # Session management
 ├── _transport.py             # Socket transport layer
 ├── embed.py                  # Public embed API
 └── backends/
      ├── _qt_compat.py        # Qt compatibility shim
      └── backend_qtagg.py     # Qt/Agg matplotlib-style backend

Flow: Python → encode message → Unix socket → Backend daemon → FigureModel mutation
      Backend daemon → STATE_DIFF → Agent process → WindowRuntime renders
```

---

## 11. Build System

### 11.1 CMake Options

| Option | Default | Purpose |
|---|---|---|
| `SPECTRA_USE_GLFW` | ON | GLFW windowing |
| `SPECTRA_USE_IMGUI` | ON | Dear ImGui UI |
| `SPECTRA_USE_FFMPEG` | OFF | Video export |
| `SPECTRA_USE_EIGEN` | OFF | Eigen adapters |
| `SPECTRA_BUILD_EXAMPLES` | ON | Example programs |
| `SPECTRA_BUILD_TESTS` | ON | Unit tests + benchmarks |
| `SPECTRA_RUNTIME_MODE` | multiproc | inproc or multiproc |

### 11.2 Key Build Targets

- `spectra` — main shared library
- `spectra-backend` — daemon process (multiproc mode)
- `spectra-window` — agent process (multiproc mode)
- `spectra-ros` — ROS2 adapter standalone application
- ~47 example executables
- ~145 unit test executables + benchmarks + golden tests

### 11.3 Compile Defines

- `SPECTRA_USE_GLFW` — enables GLFW-dependent code
- `SPECTRA_USE_IMGUI` — enables ImGui-dependent code
- `SPECTRA_USE_FFMPEG` — enables video export
- `SPECTRA_USE_EIGEN` — enables Eigen adapters
- `SPECTRA_BUILD_GOLDEN_TESTS` — enables golden image regression tests

---

## 12. Threading Model

- **Main thread**: GLFW event loop, ImGui, Vulkan command recording/submission
- **FigureRegistry**: mutex-protected (safe to call from any thread)
- **SplitViewManager**: mutex-protected
- **SessionGraph** (daemon): mutex-protected
- **FigureModel** (daemon): mutex-protected
- **ProcessManager** (daemon): mutex-protected
- **Logger**: mutex-protected singleton
- **Series data**: NOT thread-safe — must be mutated from main thread or user's on_frame callback

---

## 13. Important Patterns & Gotchas

### 13.1 Deferred Deletion
- **Series GPU resources**: `Renderer` uses a 4-slot deletion ring. When a series is removed, its GPU buffers go into the ring and are freed after `MAX_FRAMES_IN_FLIGHT + 2` frames.
- **Vulkan buffers**: `VulkanBackend` has frame-stamped `pending_buffer_frees_` flushed each frame after fence wait.
- **Series removal from commands**: Keyboard shortcuts fire during `glfwPollEvents()` at end of frame N. User's `on_frame` callback runs at start of frame N+1. So series deletion is deferred via `ImGuiIntegration::defer_series_removal()`, flushed in `WindowRuntime::update()` after `drive_figure_anim`.

### 13.2 Multi-Window ImGui Contexts
- Each window gets its own `ImGuiContext` + `ImFontAtlas` (owned by `ImGuiIntegration`).
- After `ImGui::CreateContext()`, explicitly call `SetCurrentContext()` (CreateContext restores previous context).
- Before update/render of secondary windows, switch to that window's ImGui context; restore primary context afterward.

### 13.3 Framebuffer Capture
- `readback_framebuffer()` after present reads stale data. Use `request_framebuffer_capture()` which executes between `vkQueueSubmit` and `vkQueuePresentKHR` in `end_frame()`.

### 13.4 Swapchain Recreation
- For windows with ImGui: use `recreate_swapchain_for_with_imgui()` which also updates ImGui's MinImageCount and re-inits the Vulkan backend if the render pass handle changes.

### 13.5 Dirty Flag Pattern
- All `Series` subclasses have `dirty_` flag. Set on any data/style mutation. Cleared after GPU upload in `Renderer::upload_series_data()`.
- Statistical series (`BoxPlotSeries`, etc.) have `rebuild_geometry()` called when dirty before render.

### 13.6 Series Removed Callback
- `AxesBase::set_series_removed_callback()` wires up deferred GPU cleanup. The framework installs this so `Renderer::notify_series_removed()` is called before the series is destroyed.

---

## 14. File Cross-Reference (Quick Lookup)

### Where to find a specific feature:

| Feature | Primary File(s) |
|---|---|
| App lifecycle | `src/app/main.cpp`, `src/ui/app/app.cpp`, `app_inproc.cpp`, `app_multiproc.cpp`, `app_step.cpp` |
| Per-window update/render loop | `src/ui/app/window_runtime.cpp` |
| Session orchestration | `src/ui/app/session_runtime.cpp` |
| Per-window UI bundle | `src/ui/app/window_ui_context.hpp` |
| Command registration | `src/ui/app/register_commands.cpp` |
| Vulkan backend | `src/render/vulkan/vk_backend.cpp` (~98K) |
| 2D/3D rendering | `src/render/renderer.cpp` (~99K) |
| Text rendering | `src/render/text_renderer.cpp` |
| Full ImGui UI | `src/ui/imgui/imgui_integration.cpp` (~224K) |
| Widgets (ImGui) | `src/ui/imgui/widgets.cpp` |
| Figure registry | `src/ui/figures/figure_registry.cpp` |
| Figure manager | `src/ui/figures/figure_manager.cpp` |
| Tab bar | `src/ui/figures/tab_bar.cpp` |
| Tab drag controller | `src/ui/figures/tab_drag_controller.cpp` |
| Window manager | `src/ui/window/window_manager.cpp` (~70K) |
| GLFW adapter | `src/ui/window/glfw_adapter.cpp` |
| Split view / docking | `src/ui/docking/split_view.cpp`, `dock_system.cpp` |
| Input handling | `src/ui/input/input.cpp` (~63K) |
| Box zoom | `src/ui/input/box_zoom_overlay.cpp` |
| Region select | `src/ui/input/region_select.cpp` |
| Inspector panel | `src/ui/overlay/inspector.cpp` (~38K) |
| Data interaction | `src/ui/overlay/data_interaction.cpp` |
| Crosshair overlay | `src/ui/overlay/crosshair.cpp` |
| Legend interaction | `src/ui/overlay/legend_interaction.cpp` |
| Knob manager | `src/ui/overlay/knob_manager.cpp` |
| Theme system | `src/ui/theme/theme.cpp` (~46K) |
| Design tokens | `src/ui/theme/design_tokens.hpp` |
| Icons | `src/ui/theme/icons.cpp` |
| Command palette | `src/ui/commands/command_palette.cpp` |
| Command registry | `src/ui/commands/command_registry.cpp` |
| Shortcut manager | `src/ui/commands/shortcut_manager.cpp` |
| Undo manager | `src/ui/commands/undo_manager.cpp` |
| Series clipboard | `src/ui/commands/series_clipboard.cpp` |
| Axis linking | `src/ui/data/axis_link.cpp` |
| CSV loader | `src/ui/data/csv_loader.cpp` |
| Timeline editor | `src/ui/animation/timeline_editor.cpp` |
| Keyframe interpolator | `src/ui/animation/keyframe_interpolator.cpp` |
| Curve editor | `src/ui/animation/animation_curve_editor.cpp` |
| Camera animator | `src/ui/animation/camera_animator.cpp` |
| Mode transition | `src/ui/animation/mode_transition.cpp` |
| Recording/export | `src/ui/animation/recording_export.cpp` |
| Transition engine | `src/ui/animation/transition_engine.cpp` |
| Layout manager | `src/ui/layout/layout_manager.cpp` |
| Workspace save/load | `src/ui/workspace/workspace.cpp` |
| Figure serializer | `src/ui/workspace/figure_serializer.cpp` |
| Plugin API | `src/ui/workspace/plugin_api.cpp` |
| Easing functions | `src/anim/easing.cpp` |
| Frame scheduler | `src/anim/frame_scheduler.cpp` |
| Data transforms | `src/math/data_transform.cpp` |
| Decimation | `src/data/decimation.cpp` |
| Filters | `src/data/filters.cpp` |
| IPC codec | `src/ipc/codec.cpp` |
| IPC transport | `src/ipc/transport.cpp` |
| IPC messages | `src/ipc/message.hpp` |
| Daemon main | `src/daemon/main.cpp` (~88K) |
| Figure model | `src/daemon/figure_model.cpp` |
| Session graph | `src/daemon/session_graph.cpp` |
| Process manager | `src/daemon/process_manager.cpp` |
| PNG export | `src/io/png_export.cpp` |
| SVG export | `src/io/svg_export.cpp` |
| Video export | `src/io/video_export.cpp` |
| Embed surface | `src/embed/embed_surface.cpp` |
| C FFI wrapper | `src/embed/spectra_embed_c.cpp` |
| SurfaceHost abstraction | `src/platform/window_system/surface_host.hpp` |
| GLFW surface host | `src/platform/window_system/glfw_surface_host.cpp` |
| Qt runtime | `src/adapters/qt/qt_runtime.cpp` |
| Qt surface host | `src/adapters/qt/qt_surface_host.cpp` |
| ROS2 adapter entry | `src/adapters/ros2/main.cpp` |
| ROS2 app shell | `src/adapters/ros2/ros_app_shell.cpp` |
| ROS2 bridge | `src/adapters/ros2/ros2_bridge.cpp` |
| ROS2 topic discovery | `src/adapters/ros2/topic_discovery.cpp` |
| ROS2 generic subscriber | `src/adapters/ros2/generic_subscriber.cpp` |
| ROS2 message introspector | `src/adapters/ros2/message_introspector.cpp` |
| ROS2 expression engine | `src/adapters/ros2/expression_engine.cpp` |
| ROS2 bag player | `src/adapters/ros2/bag_player.cpp` |
| ROS2 bag reader | `src/adapters/ros2/bag_reader.cpp` |
| ROS2 bag recorder | `src/adapters/ros2/bag_recorder.cpp` |
| ROS2 subplot manager | `src/adapters/ros2/subplot_manager.cpp` |
| ROS2 plot manager | `src/adapters/ros2/ros_plot_manager.cpp` |
| ROS2 session | `src/adapters/ros2/ros_session.cpp` |
| ROS2 log viewer | `src/adapters/ros2/ros_log_viewer.cpp` |
| ROS2 CSV export | `src/adapters/ros2/ros_csv_export.cpp` |
| ROS2 clipboard export | `src/adapters/ros2/ros_clipboard_export.cpp` |
| ROS2 screenshot export | `src/adapters/ros2/ros_screenshot_export.cpp` |
| ROS2 service caller | `src/adapters/ros2/service_caller.cpp` |
| ROS2 expression plot | `src/adapters/ros2/expression_plot.cpp` |
| ROS2 UI: topic list panel | `src/adapters/ros2/ui/topic_list_panel.cpp` |
| ROS2 UI: topic echo panel | `src/adapters/ros2/ui/topic_echo_panel.cpp` |
| ROS2 UI: topic stats overlay | `src/adapters/ros2/ui/topic_stats_overlay.cpp` |
| ROS2 UI: diagnostics panel | `src/adapters/ros2/ui/diagnostics_panel.cpp` |
| ROS2 UI: TF tree panel | `src/adapters/ros2/ui/tf_tree_panel.cpp` |
| ROS2 UI: node graph panel | `src/adapters/ros2/ui/node_graph_panel.cpp` |
| ROS2 UI: bag playback panel | `src/adapters/ros2/ui/bag_playback_panel.cpp` |
| ROS2 UI: bag info panel | `src/adapters/ros2/ui/bag_info_panel.cpp` |
| ROS2 UI: param editor panel | `src/adapters/ros2/ui/param_editor_panel.cpp` |
| ROS2 UI: service caller panel | `src/adapters/ros2/ui/service_caller_panel.cpp` |
| ROS2 UI: expression editor | `src/adapters/ros2/ui/expression_editor.cpp` |
| ROS2 UI: field drag-drop | `src/adapters/ros2/ui/field_drag_drop.cpp` |
| ROS2 UI: log viewer panel | `src/adapters/ros2/ui/log_viewer_panel.cpp` |
| Clipboard export | `src/ui/data/clipboard_export.cpp` |
| Data editor overlay | `src/ui/overlay/data_editor.cpp` |
| Tooltip overlay | `src/ui/overlay/tooltip.cpp` |
| Command queue | `src/ui/commands/command_queue.hpp` |
| Selection context | `src/ui/input/selection_context.hpp` |

---

## 15. Test Structure

| Directory | Contents |
|---|---|
| `tests/unit/` | ~145 files: test_axes, test_series, test_figure, test_renderer, test_backend, test_3d_*, test_command_*, test_dock_*, test_theme, test_workspace, test_ipc_*, test_input, test_animation_*, test_ros_*, test_bag_*, test_topic_*, test_expression_*, test_diagnostics_*, test_embed_*, etc. |
| `tests/bench/` | 8 benchmarks: bench_3d, bench_3d_phase3, bench_decimation, bench_multi_window, bench_phase2, bench_phase3, bench_render, bench_ui |
| `tests/golden/` | Golden image regression: renders figures, compares against baseline PNGs (2D + 3D + phase variants) |
| `tests/qa/` | QA agents: qa_agent.cpp (visual testing), ros_qa_agent.cpp (ROS2 QA) |
| `tests/util/` | Test utilities: gpu_hang_detector.hpp, multi_window_fixture.hpp, validation_guard.hpp |

---

## 16. Naming Conventions

- **Namespace**: `spectra` (public), `spectra::ui` (theme), `spectra::ipc` (IPC), `spectra::daemon` (daemon), `spectra::data` (decimation), `spectra::ros2` (ROS2 adapter)
- **Classes**: PascalCase (`FigureManager`, `VulkanBackend`)
- **Methods**: snake_case (`create_figure()`, `begin_frame()`)
- **Members**: snake_case with trailing underscore (`active_figure_id_`, `backend_`)
- **Constants**: ALL_CAPS or inline constexpr PascalCase (`INVALID_FIGURE_ID`, `MAX_FRAMES_IN_FLIGHT`)
- **Enums**: PascalCase enum class (`LineStyle::Dashed`, `ToolMode::Pan`)
- **Files**: snake_case (`figure_manager.cpp`, `vk_backend.hpp`)
- **Macros**: `SPECTRA_` prefix (`SPECTRA_LOG_INFO`, `SPECTRA_USE_GLFW`)

---

## 17. Embedding & Platform Abstraction

### 17.1 EmbedSurface (include/spectra/embed.hpp)
- Headless or foreign-window Vulkan rendering without GLFW ownership
- `render_to_buffer()` for CPU readback (PNG export, testing)
- `render_to_image()` for Vulkan interop (external compositors, Qt)
- Owns `VulkanBackend`, `Renderer`, `FigureRegistry`, `InputHandler`

### 17.2 SurfaceHost (src/platform/window_system/)
- Abstract `SurfaceHost` interface for platform-specific Vulkan surface creation
- `GlfwSurfaceHost` — GLFW implementation (default)
- `QtSurfaceHost` — Qt/QVulkanInstance implementation

### 17.3 Qt Adapter (src/adapters/qt/)
- `QtRuntime` — owns VulkanBackend + Renderer, manages multiple QWindow canvases
- Integrates with Qt event loop via `QVulkanInstance`

### 17.4 C FFI (include/spectra/spectra_embed_c.h)
- Opaque handle API for non-C++ consumers (Python ctypes, Rust, C#)
- Wraps `EmbedSurface`, `Figure`, `Axes`, `Series` as C handles

---

## 18. ROS2 Adapter (src/adapters/ros2/)

### 18.1 Architecture
```
spectra-ros (standalone executable)
 └── RosAppShell                         (src/adapters/ros2/ros_app_shell.hpp)
      ├── Ros2Bridge                     (ROS2 node, spins in background thread)
      ├── TopicDiscovery                 (enumerates topics + types)
      ├── GenericSubscriber              (type-erased subscription to any topic)
      ├── MessageIntrospector            (parses message fields at runtime)
      ├── SubplotManager                 (NxM grid, shared cursor, linked X-axis)
      ├── RosPlotManager                 (manages subscriptions → plot series)
      ├── ExpressionEngine               (math expressions with $topic.field variables)
      ├── BagPlayer / BagReader / BagRecorder  (rosbag2 playback/recording)
      ├── RosSession                     (save/load ROS2 workspace state)
      ├── ServiceCaller                  (call ROS2 services interactively)
      ├── RosLogViewer                   (rosout log capture and display)
      └── UI Panels (src/adapters/ros2/ui/)
           ├── TopicListPanel            (browse and subscribe to topics)
           ├── TopicEchoPanel            (raw message echo)
           ├── TopicStatsOverlay         (Hz, bandwidth, latency)
           ├── DiagnosticsPanel          (diagnostic_msgs aggregated view)
           ├── TfTreePanel               (TF2 frame tree visualization)
           ├── NodeGraphPanel            (ROS2 computation graph)
           ├── BagPlaybackPanel          (bag player transport controls)
           ├── BagInfoPanel              (bag metadata inspector)
           ├── ParamEditorPanel          (live parameter editing)
           ├── ServiceCallerPanel        (service call UI)
           ├── ExpressionEditorPanel     (math expression builder)
           ├── FieldDragDrop             (drag fields from topic to plot)
           └── LogViewerPanel            (rosout log table)
```

### 18.2 Data Flow
```
ROS2 Topic → GenericSubscriber → MessageIntrospector → field extraction
  → RosPlotManager → Series::set_data() → GPU upload
  → ExpressionEngine (optional) → computed series
```
