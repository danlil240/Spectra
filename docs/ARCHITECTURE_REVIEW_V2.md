# Spectra Architecture Review — V2

> **Date**: 2026-04-03
> **Baseline**: V1 review (pre-refactor)
> **Scope**: Full codebase audit reflecting all completed refactors, new subsystems, and remaining debt

---

## What the project is

Spectra is a GPU-accelerated (Vulkan) scientific/engineering plotting and visualization application written in C++20. It targets serious engineering use cases: interactive 2D/3D plotting, real-time streaming data (ROS2, PX4 flight logs, MAVLink), animated visualizations, and offscreen export (PNG, SVG, video). It supports two runtime modes — **inproc** (single-process, all windows in one event loop) and **multiproc** (daemon + agent processes communicating via Unix IPC). Python bindings provide a MATLAB-style `plot()`/`show()` API that drives the backend daemon over Unix sockets. The UI is built on Dear ImGui with a custom docking/tab system, command palette, undo/redo, theming, an event bus, an automation/MCP server, and a comprehensive plugin API.

**Scale**: ~139K lines of C++ (`src/`), ~84K lines of tests, ~12K lines of Python bindings, ~51 examples, ~145 unit tests, 9 benchmarks, golden image regression suite, plus ROS2 and PX4 adapter libraries.

---

## Main subsystems

| Subsystem | Location | Responsibility |
|---|---|---|
| **Core data model** | `src/core/`, `include/spectra/` | Figure, Axes (2D/3D), Series hierarchy (line, scatter, bar, histogram, violin, box plot, surface, mesh, **shapes 2D/3D**, **custom plugin series**), layout computation, color, plot style |
| **Vulkan rendering** | `src/render/`, `src/gpu/shaders/` | Backend abstraction (`Backend`), `VulkanBackend` impl (split: `vk_backend`, `vk_frame`, `vk_capture`, `vk_multi_window`, `vk_texture`), `Renderer` (split: orchestration, `render_2d`, `render_3d`, `render_geometry`, `render_upload`), `TextRenderer` (SDF font atlas), `SeriesTypeRegistry` (custom plugin pipelines), GLSL shaders |
| **UI framework** | `src/ui/` | ImGui integration, window management, input handling, docking/split view, overlays (crosshair, tooltip, inspector, data markers, legend, **annotations**), animation system (timeline, keyframes, curve editor, recording), theme system, command/shortcut/undo system, **OverlayRegistry** |
| **ViewModel layer** | `src/ui/viewmodel/` | `FigureViewModel` — per-figure UI state separated from the core data model. Accessor-based with change notifications and undo integration. |
| **App lifecycle** | `src/ui/app/` | `App` class, `SessionRuntime` (session loop + **RedrawTracker** event-driven rendering), `WindowRuntime` (per-window update+render), `WindowUIContext` (per-window UI bundle), `register_commands.cpp` |
| **IPC / multiproc** | `src/ipc/`, `src/daemon/`, `src/agent/` | Binary message protocol (40-byte header + TLV), codec, transport (Unix sockets), `FigureModel` (authoritative state), `SessionGraph`, `ProcessManager`. Daemon refactored into `DaemonServer`, `PythonMessageHandler`, `AgentMessageHandler`, `HeartbeatMonitor`. |
| **Platform abstraction** | `src/platform/`, `src/ui/window/` | `SurfaceHost` interface, `GlfwSurfaceHost`, `GlfwAdapter`, `WindowManager` |
| **Adapters** | `src/adapters/` | ROS2 adapter (~40 files: topic discovery, message introspection, bag playback, expression engine, 13 UI panels), PX4 adapter (ULog reader, MAVLink bridge), Qt adapter |
| **Data processing** | `src/data/`, `src/math/` | Decimation (LTTB, min-max), filters, data transforms (log, FFT, derivative, cumulative sum, custom), transform pipeline/registry |
| **I/O & export** | `src/io/` | PNG export (STB), SVG export (CPU-side traversal), video export (ffmpeg pipe) |
| **Embedding** | `src/embed/` | `EmbedSurface` (headless Vulkan rendering for embedding), C FFI (`spectra_embed_c.h`) |
| **Python bindings** | `python/` | Pure-Python client: session management, figure/axes/series proxies, codec, transport, easy API, CLI |
| **Automation / MCP** | `src/ui/automation/` | `AutomationServer` (Unix socket JSON commands, 22+ tools), **C++ MCP server** (HTTP, `tools/list`, `tools/call` for agent-driven testing) |
| **Event system** | `include/spectra/event_bus.hpp` | `EventBus<T>` template + `EventSystem` aggregate. Events: FigureCreated/Destroyed, SeriesDataChanged/Added/Removed, AxesLimitsChanged, ThemeChanged, WindowOpened/Closed. |
| **Plugin system** | `src/ui/workspace/` | Workspace save/load, figure serializer, **C ABI plugin API v2.0** (commands, transforms, overlays, export formats, data sources, **custom series types**), `PluginManager`, `SeriesTypeRegistry` |

---

## Architectural style

**Mixed: Modular monolith with rendering pipeline traits + event-driven frame loop + ViewModel separation.**

- **Split library**: `libspectra-core` (pure data model, math, transforms, I/O, animation — zero GPU dependencies) + `libspectra` (rendering, UI, IPC, platform). Downstream consumers can link only `spectra-core` for headless data manipulation.
- **Rendering pipeline**: The render path follows a classic pipeline: `SessionRuntime::tick()` → per-window `WindowRuntime::update()` + `render()` → `Renderer::begin_render_pass()` → `render_figure_content()` → `render_plot_text()` → ImGui → `end_render_pass()`. Dirty flags on `Series` drive GPU uploads. Rendering is split across focused modules (`render_2d.cpp`, `render_3d.cpp`, `render_geometry.cpp`, `render_upload.cpp`).
- **Event-driven rendering**: `RedrawTracker` with grace frames. Idle windows sleep in `glfwWaitEventsTimeout()`. ~5x CPU/GPU reduction for static scenes.
- **ViewModel pattern**: `FigureViewModel` separates per-figure UI state (selection, scroll, 3D mode, zoom cache) from the core `Figure` data model. All fields behind accessors with change callbacks and optional undo integration.
- **Event bus**: `EventBus<T>` provides typed pub/sub for cross-subsystem notifications (figure lifecycle, series changes, theme changes, window events).
- **Command pattern**: `CommandRegistry` + `ShortcutManager` + `UndoManager` provide a command system per-window.
- **Plugin extensibility**: C ABI v2.0 supports commands, transforms, overlays, export formats, data sources, and custom series types with GPU pipeline registration.
- **IPC layer**: Clean request/response protocol with snapshot + diff state sync, versioned headers, sequence numbers. Daemon decomposed into focused handler classes.
- **Adapter pattern**: ROS2 and PX4 are separate libraries that link against `spectra`, providing domain-specific UIs.

---

## Data/control flow

```
User code / Python client
    │
    ▼
App::figure() → FigureRegistry (thread-safe, monotonic IDs)
    │
    ▼
App::run() → run_inproc() / run_multiproc()
    │
    ├── SessionRuntime::tick()
    │    ├── RedrawTracker decision:
    │    │    ├── Dirty/animating → glfwPollEvents()
    │    │    └── Idle → glfwWaitEventsTimeout(0.1)
    │    ├── CommandQueue drain
    │    ├── Animator::evaluate()
    │    ├── For each WindowContext:
    │    │    ├── WindowRuntime::update()
    │    │    │    ├── FigureManager (tab creates/closes/switches)
    │    │    │    │    └── FigureViewModel (save/restore axes state)
    │    │    │    ├── Figure::on_frame callback (user animation)
    │    │    │    ├── Figure::compute_layout()
    │    │    │    ├── ImGuiIntegration::build_ui() (menus, inspector, canvas)
    │    │    │    ├── DockSystem::update_layout()
    │    │    │    └── InputHandler (pan, zoom, select, measure, annotate)
    │    │    └── WindowRuntime::render()
    │    │         ├── Backend::begin_frame() (acquire swapchain, wait fence)
    │    │         ├── Renderer::render_figure_content()
    │    │         │    ├── Per-axes: upload dirty series (render_upload.cpp)
    │    │         │    ├── 2D series rendering (render_2d.cpp)
    │    │         │    ├── 3D series rendering (render_3d.cpp)
    │    │         │    └── Custom plugin series (SeriesTypeRegistry dispatch)
    │    │         ├── Renderer::render_plot_text() (SDF text)
    │    │         ├── Renderer::render_plot_geometry() (render_geometry.cpp)
    │    │         ├── ImGuiIntegration::render() (ImGui draw commands)
    │    │         └── Backend::end_frame() (submit + present)
    │    ├── Process deferred detaches/moves
    │    └── FrameScheduler::end_frame() (sleep/spin to target FPS)
    │
    └── Multiproc path: Python → Unix socket → Backend daemon
         → DaemonServer → PythonMessageHandler / AgentMessageHandler
         → FigureModel mutation → STATE_DIFF → Agent process → WindowRuntime
```

Series data flows: `User code → Series::set_x/y()` → `dirty_ = true` → next frame: `Renderer::upload_series_data()` → GPU SSBO → draw.

---

# Changes Since V1

## Completed refactors (from V1 recommendations)

### QW-1: Daemon monolith extraction ✅ DONE

The 1,916-line `main()` in `daemon/main.cpp` has been decomposed into focused classes:

| Component | File | Lines | Responsibility |
|---|---|---|---|
| `DaemonServer` | `daemon_server.cpp/hpp` | 198 + 105 | Core daemon context, client slot management, utility functions |
| `PythonMessageHandler` | `python_message_handler.cpp/hpp` | 619 + 73 | All Python client message handling (create figure, set data, show, etc.) |
| `AgentMessageHandler` | `agent_message_handler.cpp/hpp` | 468 + 26 | Agent hello, heartbeat, window ops, state sync |
| `HeartbeatMonitor` | `heartbeat_monitor.cpp/hpp` | 85 + 26 | Stale agent detection, process reaping |
| `main.cpp` | `daemon/main.cpp` | 359 | Thin event loop with `poll()`-based multiplexing and message type dispatch |

**Before**: 1,916 lines in one function. **After**: 359-line main + 4 focused handler classes totaling ~1,370 lines. Each handler is independently testable.

### QW-2: `Series::record_commands()` removed ✅ DONE

The `virtual void record_commands(Renderer&)` method has been removed from `include/spectra/series.hpp`. The `Renderer` now uses type-dispatched rendering via its cached `SeriesType` enum. The public data model no longer depends on the rendering layer.

### QW-3: `app.hpp` internal include fixed ✅ DONE

`include/spectra/app.hpp` now includes `<spectra/figure_registry.hpp>` (a proper public header) instead of reaching into `../src/ui/figures/figure_registry.hpp`. The `FigureRegistry` header was promoted to the public API at `include/spectra/figure_registry.hpp`.

### QW-4: Figure export/animation state extracted ✅ DONE

`Figure` now contains two public sub-structs:
- `FigureExportRequest` — `png_path`, `svg_path`, `video_path`, export dimensions
- `FigureAnimState` — `fps`, `duration`, `loop`, `on_frame`, `time`

These are public members (`export_req_`, `anim_`) so runtime subsystems can consume pending requests without friend access. Friend count reduced from 7 to 7 (same classes remain friends for config/style/axes access but with narrower mutation surface).

### MR-1: `renderer.cpp` split ✅ DONE

Original 3,338-line monolith split into focused translation units:

| File | Lines | Responsibility |
|---|---|---|
| `renderer.cpp` | 761 | Orchestration (begin/end render pass, figure dispatch, init/shutdown) |
| `render_2d.cpp` | 616 | 2D series rendering (line, scatter, stat fill, shapes) |
| `render_3d.cpp` | 560 | 3D series rendering (line3d, scatter3d, surface, mesh, shapes3d) |
| `render_geometry.cpp` | 1,143 | Grid, border, tick marks, arrows, 3D grid planes |
| `render_upload.cpp` | 449 | Series GPU data upload, dirty tracking |

### MR-2: `vk_backend.cpp` split ✅ DONE

Original 2,932-line monolith split into focused modules:

| File | Lines | Responsibility |
|---|---|---|
| `vk_backend.cpp` | 1,384 | Init/shutdown, surface, swapchain, pipelines, buffers, descriptors |
| `vk_frame.cpp` | 485 | Begin/end frame, render pass, bind, draw, queries |
| `vk_capture.cpp` | 453 | Framebuffer readback and capture |
| `vk_multi_window.cpp` | 397 | Window context management |
| `vk_texture.cpp` | 370 | Texture create/destroy, image upload |
| (pre-existing) `vk_buffer.cpp` | — | Buffer create/upload/destroy |
| (pre-existing) `vk_device.cpp` | — | Device, instance, queue setup |
| (pre-existing) `vk_pipeline.cpp` | — | Pipeline creation |
| (pre-existing) `vk_swapchain.cpp` | — | Swapchain management |

### MR-4: Library genuinely split ✅ DONE

`spectra-core` is now a **real** static/shared library:
- Contains: `src/core/`, `src/math/`, `src/data/`, `src/io/`, `src/anim/`, camera, knob manager
- Zero GPU dependencies (no Vulkan, no GLFW, no ImGui)
- `libspectra` links `spectra-core` PUBLIC and adds rendering, UI, IPC, platform layers
- Downstream consumers can `target_link_libraries(my_app PRIVATE spectra-core)` for headless data manipulation

### MR-5: Event system implemented ✅ DONE

`include/spectra/event_bus.hpp` provides:
- `EventBus<T>` — lightweight typed observer with subscribe/unsubscribe/emit
- Deferred unsubscribe during emission (safe iteration)
- `EventSystem` — aggregate owner for all event buses
- 9 event types: `FigureCreatedEvent`, `FigureDestroyedEvent`, `SeriesDataChangedEvent`, `SeriesAddedEvent`, `SeriesRemovedEvent`, `AxesLimitsChangedEvent`, `ThemeChangedEvent`, `WindowOpenedEvent`, `WindowClosedEvent`
- Single `EventSystem` instance lives in `App`/`SessionRuntime`, passed by pointer to subsystems

### LT-1: FigureModel/ViewModel separation — Phase 1-3 ✅ DONE

`FigureViewModel` (`src/ui/viewmodel/`) fully implements the view-model layer:

- **Phase 1**: Absorbed `FigureState` fields from `FigureManager`
- **Phase 2**: Migrated per-figure UI state from `WindowUIContext`
- **Phase 3**: All fields behind accessor methods with:
  - `ChangeField` enum (10 fields)
  - `ChangeCallback` fired on every mutation (no-op on same value)
  - Optional `UndoManager*` integration (undoable title, 3D mode, series selection)
  - Mutable accessors (`axes_snapshots_mut()`, `saved_3d_camera_mut()`, `home_limits_mut()`)

All callers updated across `figure_manager.cpp`, `register_commands.cpp`, `window_runtime.cpp`, `app_step.cpp`, `agent/main.cpp`, `automation_server.cpp`. 102 tests pass (20 ViewModel + 54 FigureManager + 28 integration).

### LT-2: Plugin API expanded to v2.0 ✅ DONE

The plugin C ABI has been expanded through 4 revisions:

| API Version | Extension Point | Capabilities |
|---|---|---|
| v1.0 | Commands, shortcuts, undo | Register commands, bind shortcuts, push undo actions |
| v1.1 | Transform plugins | Scalar transforms (`float→float`) and XY transforms (can change array length) |
| v1.2 | Overlay plugins | Register ImGui draw callbacks per axes viewport. Drawing helpers (line, rect, circle, text) avoid direct ImGui linking. |
| v1.3 | Export format + data source plugins | Custom export formats (receive figure JSON + RGBA pixels). Data source adapters (start/stop/poll interface with optional UI). |
| **v2.0** | **Custom series types** | Register SPIR-V shaders + vertex layout + upload/draw/bounds/cleanup callbacks. Full GPU pipeline access via C ABI backend wrappers (`spectra_backend_create_buffer`, `_bind_pipeline`, `_draw`, `_draw_indexed`, etc.). `CustomSeries` class + `SeriesTypeRegistry`. |

Supporting infrastructure:
- `SeriesTypeRegistry` (`src/render/series_type_registry.hpp/cpp`) — thread-safe registration, pipeline creation/destruction
- `CustomSeries` (`include/spectra/custom_series.hpp`) — plugin-defined data blob with plugin-computed bounds
- `OverlayRegistry` (`src/ui/overlay/overlay_registry.hpp/cpp`) — thread-safe overlay callback management
- `PluginManager` — discovery, loading, unloading, enable/disable, state persistence
- `ThemeSnapshot` + `ThemeChangeCallback` (`include/spectra/theme_api.hpp`) — read-only theme API for plugins

---

## New subsystems since V1

### Automation / MCP server

- **AutomationServer** (`src/ui/automation/automation_server.cpp`, 1,281 lines) — Unix socket listener, 22+ tools (ping, execute_command, create_figure, add_series, mouse/keyboard simulation, screenshot, resize, pump_frames, etc.)
- **C++ MCP server** (`src/ui/automation/mcp_server.cpp`, 839 lines) — HTTP endpoint exposing `initialize`, `tools/list`, `tools/call`. Forwards to the existing automation execution path. Enables IDE agent integration (Windsurf, Claude, etc.)

### Shape series (2D + 3D)

- `ShapeSeries` (`include/spectra/series_shapes.hpp`) — 2D geometric primitives: rect, circle, ellipse, line, arrow, polygon, ring, text annotations. Fluent API with per-shape style modifiers. Generates fill triangles + outline segments.
- `ShapeSeries3D` (`include/spectra/series_shapes3d.hpp`) — 3D primitives: box, sphere, cylinder, cone, arrow3d, plane. Generates MeshSeries-compatible vertex data with normals for Phong lighting.
- Both support animation via `clear_shapes()` + rebuild pattern.

### Annotation system

- `AnnotationManager` (`src/ui/overlay/annotation.hpp/cpp`) — persistent text annotations pinned to data coordinates. Inline ImGui text editing, drag-to-reposition, per-axes filtering, theme-aware rendering.
- Integrates with `InputHandler` annotate tool mode.

### Panel detach controller

- `PanelDetachController` (`src/ui/panel/panel_detach_controller.hpp/cpp`) — manages panel tear-off into separate windows.

### Resource monitor

- `ResourceMonitor` (`src/ui/app/resource_monitor.hpp`) — runtime resource tracking.

### Animation tick gate

- `AnimationTickGate` (`src/ui/app/animation_tick_gate.hpp`) — controls animation tick scheduling.

---

## What is working well

### 1. Clean rendering abstraction with practical Vulkan engineering

The `Backend` abstract interface (`src/render/backend.hpp`) remains well-designed: opaque handles, clean lifecycle methods, no Vulkan types leak. The `VulkanBackend` is now properly decomposed into 9 focused files instead of one 2,932-line monolith. Deferred buffer deletion, per-window command buffers, swapchain recreation with proper fence ordering — all correct.

The `Renderer` split into 5 files (orchestration + 2D + 3D + geometry + upload) makes each rendering path independently navigable. New series types (shapes, custom) integrate cleanly.

### 2. Multi-window architecture

`WindowUIContext` bundles all per-window UI state. Each window has independent ImGui contexts, command registries, undo stacks. `WindowManager` handles figure detach/move/preview. `TabDragController` state machine is clean.

### 3. Event-driven rendering

`RedrawTracker` with grace frames (3 frames after last dirty) + `glfwWaitEventsTimeout(0.1)`. Idle at ~10 fps, active at ~60fps VSync. `glfwPostEmptyEvent()` from automation server thread for cross-thread wake-up.

### 4. ViewModel separation (LT-1 complete)

`FigureViewModel` cleanly separates UI state from the data model. All fields behind accessors with change notification. Undo integration is non-invasive (optional `UndoManager*`). The suppressed-undo guard prevents re-entrant pushes.

### 5. Plugin API breadth

The v2.0 C ABI covers 6 extension points (commands, transforms, overlays, export formats, data sources, custom series types). The C ABI backend wrappers allow plugins to create buffers, upload data, bind pipelines, and issue draw calls without linking Vulkan. The `SeriesTypeRegistry` handles pipeline lifecycle correctly (create on init, destroy on shutdown/swapchain recreation).

### 6. Daemon decomposition

The daemon is now a clean 359-line event loop dispatching to 4 focused handler classes. Each handler operates on `DaemonContext` (shared references to `SessionGraph`, `FigureModel`, `ProcessManager`, clients). Message routing is a simple switch.

### 7. IPC protocol

Production-grade binary protocol: magic bytes, versioned headers, request ID correlation, sequence numbers, snapshot + diff state sync with revision tracking.

### 8. Comprehensive test coverage

~145 unit tests, 9 benchmarks, golden image regression tests, QA agents. Infrastructure includes `ValidationGuard`, `GPUHangDetector`, `MultiWindowFixture`.

### 9. Ergonomic public API

MATLAB-style `easy.hpp` with 7 progressive complexity levels. Fluent API on all series types. Shape series provide clean geometry primitives for annotations and overlays.

---

## Remaining concerns

### 1. `Figure` still has 7 friend classes

```cpp
friend class AnimationBuilder;
friend class App;
friend class FigureManager;
friend class WindowRuntime;
friend class SessionRuntime;
friend class FigureSerializer;
friend class EmbedSurface;
```

While `FigureExportRequest` and `FigureAnimState` were extracted as public sub-structs, the friend list hasn't shrunk. The remaining friends access `config_`, `style_`, `legend_`, `axes_`, `all_axes_`, `grid_rows_`, `grid_cols_`, and scroll state. A further pass could expose these through accessor methods, but the reduced mutation surface (export/animation are now public) makes this less urgent.

### 2. `ThemeManager` is still a global singleton

`ui::ThemeManager::instance()` is called from ~15 locations. MR-3 (replace singleton with injected dependency) from V1 has not been implemented. The new `ThemeSnapshot` / `ThemeChangeCallback` API provides a stable read-only interface for plugins, but the core singleton pattern remains.

**Impact**: Blocks per-window theming, makes testing harder, hidden coupling.

### 3. `window_manager.cpp` remains large (2,406 lines)

This is now the largest single file in the codebase. It handles window creation, destruction, figure detach/move, preview windows, cross-window operations. A split similar to the renderer/backend decomposition would improve navigability.

### 4. `input.cpp` remains large (1,982 lines)

All mouse/keyboard handling, pan, zoom, box select, measure, hit testing in one file. Could be split by tool mode.

### 5. `register_commands.cpp` remains large (1,753 lines)

All ~80+ command registrations in one function. Could be split by command category (file, edit, view, series, navigation, etc.).

### 6. Series data is now thread-safe ✅

Resolved. `Series::set_thread_safe(true)` enables a double-buffered `PendingSeriesData` path protected by a `SpinLock`. Producers (ROS2 executor threads) write directly via `Series::append()` which routes through PendingSeriesData. At frame boundary, `commit_pending()` swaps pending data into the live vectors. `dirty_` is `std::atomic<bool>`. ROS2 adapters (`RosPlotManager`, `SubplotManager`) use direct-write callbacks from the executor thread, eliminating ring-buffer intermediaries. PX4 adapter retains its batch-rebuild pattern (structurally incompatible with streaming append). See LT-3 below.

### 7. `#ifdef` soup for feature flags

Feature flags (`SPECTRA_USE_GLFW`, `SPECTRA_USE_IMGUI`, `SPECTRA_USE_FFMPEG`, `SPECTRA_USE_EIGEN`, `SPECTRA_USE_ROS2`, `SPECTRA_USE_PX4`) are still scattered as `#ifdef` guards. The library split (spectra-core vs spectra) reduces this somewhat for core-only consumers, but the full library still has significant ifdef complexity.

### 8. `EventBus` has deferred emission support ✅

`EventBus::emit_deferred()` enqueues events into a mutex-protected deferred queue. `drain_deferred()` replays them on the main thread. `EventSystem::drain_all_deferred()` drains all 9 typed buses. `SessionRuntime::tick()` calls `drain_all_deferred()` before `commit_thread_safe_series()`. This enables safe cross-thread event emission from producer threads.

### 9. No formal lifecycle protocol for `WindowUIContext`

`WindowUIContext` remains a large bag of ~20+ members with no init/shutdown ordering contract. Adding new UI features means adding another member here.

---

## Architectural risks

### Risk 1: `WindowUIContext` sprawl
The struct has grown organically. It's not a designed interface — it's a bag of everything a window needs. No lifecycle protocol (init/shutdown ordering). Each new feature adds another member.

### Risk 2: Plugin ABI stability
The C ABI is at v2.0 with 6 extension points. Maintaining binary compatibility across compiler versions and plugin updates requires discipline. The `make_context()` version gating is correct but untested at scale. Plugin crash isolation is not implemented — a plugin segfault will crash the host.

### Risk 3: Custom series GPU hazards
The `SeriesTypeRegistry` allows plugins to register arbitrary SPIR-V shaders and issue draw calls through C ABI wrappers. Incorrect buffer management or pipeline state in plugin code could cause GPU hangs or validation errors that are difficult to diagnose.

### Risk 4: Event system emission ordering
`EventBus::emit()` invokes all callbacks synchronously in registration order. If a callback modifies state that triggers another emission, the re-entrant behavior is undefined. No guard against recursive emission.

### Risk 5: Rendering pipeline still coupled to figure layout
`Renderer::render_figure_content()` directly reads `Figure::compute_layout()` results and iterates axes. If the layout model changes (constraints, responsive), the renderer must change too.

---

## Remaining recommendations

### Quick wins

#### QW-5: Split `window_manager.cpp` (2,406 lines)

- **What**: Extract into `window_lifecycle.cpp` (creation/destruction), `window_figure_ops.cpp` (detach/move/preview), and `window_manager.cpp` (orchestration).
- **Why**: Largest file in the codebase. Same rationale as MR-1/MR-2.
- **Difficulty**: Low-medium.

#### QW-6: Split `register_commands.cpp` into category functions ✅ DONE

- **What**: Command registrations grouped into 10 focused functions: `register_view_commands()`, `register_edit_commands()`, `register_file_commands()`, `register_figure_commands()`, `register_series_commands()`, `register_animation_commands()`, `register_theme_commands()`, `register_panel_commands()`, `register_tool_commands()`, `register_window_commands()`. Top-level `register_standard_commands()` dispatches to all.
- **Result**: Each command category is independently navigable. Adding a new command requires finding only the relevant category function. File remains at 1,846 lines but internal structure is clean.
- **Further opportunity**: Could split into separate translation units per category if the file grows further.

#### QW-7: Split `input.cpp` (1,982 lines)

- **What**: Extract tool-mode-specific logic into `input_pan_zoom.cpp`, `input_select.cpp`, `input_measure.cpp`, `input_annotate.cpp`.
- **Difficulty**: Low-medium.

### Medium-size refactors

#### MR-3: Replace `ThemeManager` singleton (carried from V1)

- **What**: Make `ThemeManager` an owned object in `App`/`SessionRuntime`. Pass as reference to subsystems.
- **Why**: Blocks testability, per-window theming, creates hidden coupling.
- **Difficulty**: Medium. ~15 call sites.

#### MR-6: Add `WindowUIContext` lifecycle protocol

- **What**: Replace the flat struct with a class that has explicit `init()` / `shutdown()` methods and documented initialization order. Group members into logical sub-structs (interaction bundle, command bundle, animation bundle).
- **Why**: The current struct is fragile — adding members requires knowing implicit ordering.
- **Difficulty**: Medium.

#### MR-7: Plugin crash isolation

- **What**: Run plugin callbacks in a signal-handler-guarded context. Catch segfaults/exceptions and disable the offending plugin rather than crashing the host.
- **Why**: The v2.0 plugin API gives plugins GPU access. A bug in a plugin can take down the entire application.
- **Difficulty**: Medium-high.

### Long-term architecture direction

#### LT-3: Thread-safe Series data model ✅ IMPLEMENTED

Fully implemented. ROS2 executor threads write directly to `LineSeries::append()` from callback threads.

- **Core infrastructure**: `PendingSeriesData` (src/core/pending_series_data.hpp) provides thread-safe append/replace/erase_before under `SpinLock`. `Series::set_thread_safe(true)` allocates the pending buffer. `commit_pending()` swaps pending data into live vectors at frame boundary.
- **Atomic dirty flag**: `dirty_` is `std::atomic<bool>` with relaxed ordering. Move operations on Series are deleted (instances are managed by pointer/reference).
- **EventBus deferred emission**: `EventBus::emit_deferred()` enqueues events into a mutex-protected queue. `drain_deferred()` replays on the main thread. `SessionRuntime::tick()` drains before `commit_thread_safe_series()`.
- **ROS2 adapter migration**: `GenericSubscriber` supports `DirectWriteCallback` per field extractor — fired on the executor thread instead of pushing to ring buffers. `RosPlotManager` and `SubplotManager` register callbacks that call `series->append()` directly. `poll()` calls `commit_pending()` and handles pruning/auto-fit.
- **PX4 adapter**: Retains batch-rebuild pattern (`channel_snapshot()` → rebuild vectors). Structurally incompatible with streaming append — not a regression, the batch pattern is the correct design for MAVLink telemetry.
- **ExpressionPlot**: Retains ring-buffer mode (expression evaluation requires synchronization of multiple inputs on a single thread).
- **Test coverage**: 30+ unit tests for PendingSeriesData/SpinLock, all RosPlotManager tests (51) and SubplotManager tests (68) pass with direct-write mode.
- **Acceptance criteria met**: ROS2 topic subscriber writes directly to `LineSeries` from executor thread. No mutex in the adapter hot path. No frame hitches.

#### LT-4: Event system hardening

The `EventBus<T>` is functional but fragile under real-world usage patterns.

- **Re-entrancy guard**: Add an `emitting_` flag to `EventBus::emit()`. If a callback triggers another `emit()` on the same bus, queue the event instead of dispatching immediately. Drain the queue after the outer emission completes. This prevents infinite recursion and guarantees predictable callback ordering.
- **Cross-thread emission queue**: ✅ **Done** (implemented as part of LT-3). `EventBus::emit_deferred()` enqueues into a `std::vector` protected by `std::mutex`. `drain_deferred()` replays on the main thread. `SessionRuntime::tick()` calls `EventSystem::drain_all_deferred()` before `commit_thread_safe_series()`.
- **Event priorities**: Allow subscribers to specify a priority tier (e.g., `Priority::High`, `Priority::Normal`, `Priority::Low`). High-priority callbacks (e.g., ViewModel state updates) run before normal ones (e.g., UI refresh) within the same emission.
- **Subscription lifetime management**: Add `ScopedSubscription` RAII wrapper that auto-unsubscribes on destruction. Currently, callers must manually track `SubscriptionId` and call `unsubscribe()` — easy to leak.
- **New event types to consider**: `AnimationStartedEvent`, `AnimationStoppedEvent`, `ExportCompletedEvent`, `PluginLoadedEvent`, `PluginUnloadedEvent`.
- **Difficulty**: Medium. Core changes in `event_bus.hpp` only. Callers adopt incrementally.

#### LT-5: ViewModel migration for Axes and Series

`FigureViewModel` (LT-1) proved the pattern. The next logical targets are `Axes` and `Series`, which still mix data state with view/interaction state.

- **AxesViewModel** — extract from `Axes`:
  - View state: current zoom limits (separate from data-fit limits), scroll position, active tool mode per-axes, selection rect, hover state
  - Interaction state: pan origin, zoom anchor, box-select corners
  - Currently embedded as direct fields in `Axes` and partially in `InputHandler`
  - Enables: multiple views of the same axes data (split view showing different zoom levels), serialization of view state independent of data
- **SeriesViewModel** — extract from `Series`:
  - View state: visibility toggle, selection highlight, opacity override, label override
  - Currently `visible_`, `color_`, `label_` live on `Series` alongside data arrays
  - Enables: per-window visibility (show series A in window 1, hide in window 2), undo for visibility changes without touching data
- **Migration strategy**: Same phased approach as LT-1. Phase 1: create the ViewModel classes with forwarding accessors. Phase 2: migrate callers. Phase 3: enforce accessor-only access with change callbacks.
- **Coupling risk**: `Renderer` currently reads axes limits and series visibility directly. Must route through ViewModel accessors.
- **Difficulty**: High. Axes and Series are the most-touched types in the codebase.

#### LT-6: GPU compute pipeline for data processing

All data processing (decimation, transforms, FFT, derivative, cumulative sum) currently runs on the CPU in `src/data/` and `src/math/`. For datasets exceeding ~1M points, this becomes a bottleneck — especially LTTB decimation and FFT.

- **Goal**: Offload heavy data processing to Vulkan compute shaders.
- **Phase 1 — Compute-based decimation**:
  - Implement LTTB and min-max decimation as compute shaders operating on GPU-resident SSBOs.
  - Current flow: CPU `lttb()` → CPU result → GPU upload. New flow: GPU SSBO → compute dispatch → GPU SSBO (zero CPU readback).
  - Requires adding `VkComputePipeline` support to `Backend` (currently graphics-only).
- **Phase 2 — Compute-based transforms**:
  - Move `TransformRegistry` scalar/XY transforms to compute where applicable.
  - Plugin transforms (C ABI v1.1) would need a GPU variant callback signature.
- **Phase 3 — GPU-side FFT**:
  - Stockham FFT in a compute shader. Critical for real-time frequency analysis of ROS2 sensor streams.
- **Backend changes**: Add `Backend::create_compute_pipeline()`, `Backend::dispatch_compute()`, `Backend::memory_barrier()`. The `VulkanBackend` already has the device/queue infrastructure.
- **Difficulty**: High. New Vulkan pipeline type, shader compilation, synchronization between compute and graphics queues.

#### LT-7: Render graph abstraction

The current rendering is procedural: `begin_render_pass()` → series of draw calls → `end_render_pass()`. This works but limits optimization opportunities.

- **Goal**: Replace procedural rendering with a declarative render graph where passes and resources are described, then compiled and executed optimally.
- **Benefits**:
  - Automatic resource aliasing (transient framebuffers share memory)
  - Parallel command buffer recording (independent passes on separate threads)
  - Automatic barrier/layout transition insertion
  - Clean integration point for compute passes (LT-6)
  - Easier multi-pass effects (MSAA resolve, post-processing, bloom for data highlights)
- **Scope**: The `Renderer` split (MR-1) already identifies the logical passes: upload, 2D series, 3D series, geometry, text, ImGui overlay. Each becomes a render graph node.
- **Risk**: Over-engineering for a plotting library. Only pursue if compute shaders (LT-6) or advanced post-processing effects justify the complexity.
- **Difficulty**: Very high. Full rendering architecture rework.

#### LT-8: Data virtualization / out-of-core datasets

Current `LineSeries` stores all data in `std::vector<float>` in RAM. For datasets exceeding available memory (multi-GB flight logs, week-long ROS2 bag recordings), this fails.

- **Goal**: Support datasets larger than RAM via chunked, demand-loaded data access.
- **Strategy**:
  1. **Chunked data model**: Replace contiguous `std::vector<float>` with a `ChunkedArray` that loads fixed-size chunks (e.g., 1M points) from disk on demand.
  2. **Memory-mapped backend**: Use `mmap()` / `CreateFileMapping()` for read-only access to large binary data files. OS manages page eviction.
  3. **Level-of-detail cache**: Pre-compute decimated versions at multiple zoom levels (pyramid). When zoomed out, render from the coarsest level. When zoomed in, load the relevant chunk at full resolution.
  4. **Streaming append**: For live data, maintain a rolling window in RAM (configurable, e.g., last 10M points) and spill older data to a memory-mapped temp file.
- **Integration**: The `data::lttb()` and `data::min_max_decimate()` functions in `src/data/decimation.hpp` already return reduced-point vectors. The LoD cache would pre-compute these at build time.
- **Impact**: Enables flight log analysis (PX4 ULog files can be 2-10GB), long-duration ROS2 bag replay, and batch processing of scientific datasets.
- **Difficulty**: High. Touches `Series` data model, renderer upload path, and introduces new I/O subsystem.

#### LT-9: IPC protocol v2 (schema-defined payloads)

The current binary protocol (`src/ipc/message.hpp`) is hand-rolled: magic bytes, 40-byte fixed header, hand-coded TLV serialization in `codec.cpp`. This works but is fragile — adding a new field requires careful manual encoding/decoding with no schema validation.

- **Goal**: Migrate to a schema-defined serialization format while preserving the binary wire format's performance characteristics.
- **Options**:
  - **FlatBuffers** (preferred): Zero-copy deserialization, schema evolution with field deprecation, C++ and Python codegen. Aligns with Spectra's performance requirements.
  - **Cap'n Proto**: Similar zero-copy benefits, more complex build integration.
  - **Protocol Buffers**: Mature tooling but requires deserialization copy. Acceptable for control messages, not ideal for bulk data transfer.
- **Migration path**: Keep the existing 40-byte header (magic, type, length, seq, request_id, session_id, window_id) as the framing layer. Replace only the payload encoding with FlatBuffers. This allows incremental migration — old and new payloads can coexist during transition.
- **Python impact**: The Python `spectra/_codec.py` hand-rolled encoder/decoder would be replaced by generated FlatBuffers Python code.
- **Difficulty**: Medium-high. Schema definition, codegen integration into CMake, migration of ~30 message types, Python codec replacement.

#### LT-10: WebGPU / WebAssembly target

The `Backend` abstraction (`src/render/backend.hpp`) with opaque handles and no leaked Vulkan types was designed to support alternative rendering backends.

- **Goal**: Add a `WebGPUBackend` implementation enabling Spectra to run in the browser via Emscripten/wasm.
- **Scope**:
  - `spectra-core` (data model, math, transforms) already has zero GPU dependencies — compiles to wasm unchanged.
  - `WebGPUBackend` implements the `Backend` interface using `wgpu` (Dawn or wgpu-native).
  - ImGui has an existing WebGPU backend (`imgui_impl_wgpu`).
  - GLFW has Emscripten support for window/input.
- **Challenges**:
  - Shader translation: GLSL → WGSL or SPIR-V → WGSL. Plugin custom SPIR-V shaders (v2.0 API) would need a SPIR-V → WGSL transpiler.
  - No Unix sockets in browser: IPC/multiproc mode would not apply. Inproc-only for web target.
  - File I/O: PNG/SVG export would use browser download APIs instead of `fopen`.
  - Memory constraints: wasm has a 4GB address space limit. Data virtualization (LT-8) becomes critical for large datasets.
- **Value**: Browser-based scientific visualization with the same codebase. Jupyter notebook integration. Shareable interactive plots via URL.
- **Difficulty**: Very high. New backend, shader toolchain, build system integration, platform abstraction layer for browser APIs.

#### LT-11: Accessibility

Spectra currently has no accessibility support. Scientific visualization tools are used by researchers with diverse abilities.

- **Keyboard navigation**: Complete keyboard-only operation for all UI elements. Currently, many interactions (pan, zoom, annotate) require mouse input. Add focus management and key-driven navigation through axes, series, and UI panels.
- **Screen reader integration**: Expose figure metadata (axes labels, series counts, data ranges, annotation text) via platform accessibility APIs (AT-SPI on Linux, UI Automation on Windows, NSAccessibility on macOS). ImGui has experimental accessibility support via `imgui_test_engine`.
- **Sonification**: Audio representation of data trends for visually impaired users. Map y-values to pitch, x-axis to time. Play a series as a tone sweep.
- **Data table export**: Generate accessible HTML table representations of figure data, usable by screen readers.
- **High-contrast improvements**: The existing `high_contrast` theme is a starting point. Ensure all series colors meet WCAG AA contrast ratios against the background. Add pattern/texture fills as alternatives to color-only series differentiation.
- **Difficulty**: Medium-high across multiple phases. Keyboard navigation first (medium), screen reader support (high), sonification (medium).

#### LT-12: Collaborative / remote rendering

Enable multiple users to view and interact with the same figure session, either in real-time or via shared state snapshots.

- **Real-time collaboration**: The daemon (`src/daemon/`) already manages multiple connected clients (Python + agents). Extend to support multiple interactive viewers: each viewer is an agent process with its own window, all receiving the same `STATE_DIFF` updates from the authoritative `FigureModel`.
- **Remote rendering**: For high-performance datasets that can't be transferred to the client, render server-side and stream compressed frames (H.264/VP9) to a thin web client. The `EmbedSurface` + `vk_capture.cpp` readback path already captures rendered frames.
- **Shared annotations**: Annotations (LT-5 ViewModel) shared across sessions via the daemon, enabling collaborative data review.
- **Conflict resolution**: When two users modify the same figure simultaneously, the daemon applies last-writer-wins for simple properties. For complex operations (series reordering), add operational transform or CRDT semantics.
- **Difficulty**: Very high. Requires networking, authentication, state synchronization, and potentially video encoding infrastructure.

---

# Progress scorecard (V1 → V2)

| V1 Item | Status | Notes |
|---|---|---|
| QW-1: Extract daemon handlers | ✅ **Done** | 1,916→359 lines main + 4 handler classes |
| QW-2: Remove `Series::record_commands()` | ✅ **Done** | Type-dispatched rendering in Renderer |
| QW-3: Fix `app.hpp` internal include | ✅ **Done** | `figure_registry.hpp` promoted to public API |
| QW-4: Extract Figure export/animation state | ✅ **Done** | `FigureExportRequest` + `FigureAnimState` structs |
| MR-1: Split `renderer.cpp` | ✅ **Done** | 3,338→5 files (761+616+560+1143+449) |
| MR-2: Split `vk_backend.cpp` | ✅ **Done** | 2,932→5 files (1384+485+453+397+370) + pre-existing |
| MR-3: Replace ThemeManager singleton | ❌ **Open** | Still global singleton |
| MR-4: Make library split-able | ✅ **Done** | `spectra-core` is real library, zero GPU deps |
| MR-5: Event system | ✅ **Done** | `EventBus<T>` + `EventSystem` with 9 event types |
| LT-1: FigureModel/ViewModel separation | ✅ **Done** (Phases 1-3) | Full accessor-based ViewModel with undo |
| LT-2: Expand plugin API | ✅ **Done** (v2.0) | 6 extension points including custom series types |
| LT-3: Thread-safe data model | ✅ **Done** | PendingSeriesData + SpinLock + direct-write ROS2 |

**10 of 12** V1 recommendations implemented. 2 remaining (ThemeManager singleton, plus carried items).

---

# Final verdict

## Current maturity level

**Late-stage pre-1.0 project approaching production readiness.** The major architectural debts identified in V1 have been systematically addressed: god objects decomposed, data model decoupled from renderer, library split achieved, event system added, ViewModel separation complete, plugin API expanded to cover all extension points. The codebase has grown modestly (~139K → up from ~136K) but the structural improvements mean the growth is in the right places (tests, plugin infrastructure, new series types) rather than in monolithic files.

## Whether the architecture is healthy enough for growth

**Yes.** The foundational choices remain sound, and the V1 blockers have been cleared:
- The library is split (headless/embedded use cases unblocked)
- God objects are decomposed (developer productivity improved)
- Data model is decoupled from renderer (new renderers and plugin series unblocked)
- Event system exists (plugin ecosystem unblocked)
- Plugin API covers data, rendering, and UI extension points

Remaining technical debt (ThemeManager singleton, large input/window_manager files, thread-unsafe Series) is real but not blocking. The architecture can sustain feature development and ecosystem growth.

## Top 3 priorities for V2→V3

1. **Break up remaining large files** (QW-5, QW-6, QW-7) — `window_manager.cpp` (2,406), `input.cpp` (1,982), `register_commands.cpp` (1,753) are the last >1,700-line files. Splitting them completes the god-object cleanup.

2. **Replace ThemeManager singleton** (MR-3) — the last global singleton. Blocks per-window theming and testability. Straightforward with ~15 call sites.

3. ~~**Thread-safe Series data model** (LT-3)~~ — **DONE**. PendingSeriesData + SpinLock + direct-write callbacks in ROS2 adapters. Next priority: TSAN validation and extension to 3D series types.

---

# Appendix: File size comparison (V1 → V2)

| File | V1 Lines | V2 Lines | Change |
|---|---|---|---|
| `renderer.cpp` | 3,338 | 761 | Split into 5 files |
| `vk_backend.cpp` | 2,932 | 1,384 | Split into 5 files |
| `window_manager.cpp` | 2,374 | 2,406 | +32 (unchanged structure) |
| `input.cpp` | 1,982 | 1,982 | Unchanged |
| `daemon/main.cpp` | 1,916 | 359 | Split into 4 handler classes |
| `register_commands.cpp` | 1,745 | 1,753 | +8 (unchanged structure) |
| `plugin_api.hpp` | ~200 | 588 | Expanded to v2.0 (6 extension points) |
| `plugin_api.cpp` | ~300 | 1,123 | Implementations for all extension points |
| **New**: `event_bus.hpp` | — | 214 | EventBus + EventSystem + 9 event types |
| **New**: `figure_view_model.hpp` | — | 171 | ViewModel with accessors + undo |
| **New**: `series_type_registry.hpp` | — | 92 | Custom series plugin pipeline registry |
| **New**: `series_shapes.hpp` | — | 236 | 2D shape series |
| **New**: `series_shapes3d.hpp` | — | 208 | 3D shape series |
| **New**: `overlay_registry.hpp` | — | 71 | Plugin overlay registration |
| **New**: `theme_api.hpp` | — | 61 | Public read-only theme API |
| **New**: `custom_series.hpp` | — | 63 | Plugin-defined series class |
| **New**: `annotation.hpp` | — | 143 | Text annotations |
| **New**: `automation_server.cpp` | — | 1,281 | Automation + MCP tools |
| **New**: `mcp_server.cpp` | — | 839 | C++ HTTP MCP server |
