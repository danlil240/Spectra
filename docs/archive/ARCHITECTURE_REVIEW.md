# Spectra Macro Overview

## What the project is

Spectra is a GPU-accelerated (Vulkan) scientific/engineering plotting and visualization application written in C++20. It targets serious engineering use cases: interactive 2D/3D plotting, real-time streaming data (ROS2, PX4 flight logs, MAVLink), animated visualizations, and offscreen export (PNG, SVG, video). It supports two runtime modes — **inproc** (single-process, all windows in one event loop) and **multiproc** (daemon + agent processes communicating via Unix IPC). Python bindings provide a MATLAB-style `plot()`/`show()` API that drives the backend daemon over Unix sockets. The UI is built on Dear ImGui with a custom docking/tab system, command palette, undo/redo, theming, and a nascent plugin API.

**Scale**: ~136K lines of C++ (`src/`), ~81K lines of tests, ~12K lines of Python bindings, ~47 examples, ~130 unit tests, 8 benchmarks, golden image regression suite, plus ROS2 and PX4 adapter libraries.

## Main subsystems

| Subsystem | Location | Responsibility |
|---|---|---|
| **Core data model** | `src/core/`, `include/spectra/` | Figure, Axes (2D/3D), Series hierarchy (line, scatter, bar, histogram, violin, box plot, surface, mesh), layout computation, color, plot style |
| **Vulkan rendering** | `src/render/`, `src/gpu/shaders/` | Backend abstraction (`Backend`), `VulkanBackend` impl, `Renderer` (pipeline orchestration, series GPU upload, grid/border/text rendering), `TextRenderer` (SDF font atlas), GLSL shaders |
| **UI framework** | `src/ui/` | ImGui integration, window management, input handling, docking/split view, overlays (crosshair, tooltip, inspector, data markers, legend), animation system (timeline, keyframes, curve editor, recording), theme system, command/shortcut/undo system |
| **App lifecycle** | `src/ui/app/` | `App` class, `SessionRuntime` (session loop), `WindowRuntime` (per-window update+render), `WindowUIContext` (per-window UI bundle), `register_commands.cpp` |
| **IPC / multiproc** | `src/ipc/`, `src/daemon/`, `src/agent/` | Binary message protocol (40-byte header + TLV), codec, transport (Unix sockets), `FigureModel` (authoritative state), `SessionGraph`, `ProcessManager`, agent entry point |
| **Platform abstraction** | `src/platform/`, `src/ui/window/` | `SurfaceHost` interface, `GlfwSurfaceHost`, `GlfwAdapter`, `WindowManager` |
| **Adapters** | `src/adapters/` | ROS2 adapter (~40 files: topic discovery, message introspection, bag playback, expression engine, 13 UI panels), PX4 adapter (ULog reader, MAVLink bridge), Qt adapter |
| **Data processing** | `src/data/`, `src/math/` | Decimation (LTTB, min-max), filters, data transforms (log, FFT, derivative, cumulative sum, custom), transform pipeline/registry |
| **I/O & export** | `src/io/` | PNG export (STB), SVG export (CPU-side traversal), video export (ffmpeg pipe) |
| **Embedding** | `src/embed/` | `EmbedSurface` (headless Vulkan rendering for embedding), C FFI (`spectra_embed_c.h`) |
| **Python bindings** | `python/` | Pure-Python client: session management, figure/axes/series proxies, codec, transport, easy API, CLI |
| **Automation** | `src/ui/automation/` | AutomationServer (Unix socket JSON commands), MCP server (tool-based agent interface) |
| **Workspace/plugin** | `src/ui/workspace/` | Workspace save/load, figure serializer, C ABI plugin API, plugin manager |

## Architectural style

**Mixed: Modular monolith with rendering pipeline traits + event-driven frame loop.**

- **Monolithic library**: All subsystems compile into a single `libspectra` shared library. The logical split (`spectra-core`, `spectra-ipc`, `spectra-render`) is INTERFACE-only — they all link the same binary. This means a Python-only consumer still pulls in all of ImGui, Vulkan, GLFW, and the full UI stack.
- **Rendering pipeline**: The render path follows a classic pipeline: `SessionRuntime::tick()` → per-window `WindowRuntime::update()` + `render()` → `Renderer::begin_render_pass()` → `render_figure_content()` → `render_plot_text()` → ImGui → `end_render_pass()`. Dirty flags on `Series` drive GPU uploads.
- **Event-driven rendering**: `RedrawTracker` with grace frames. Idle windows sleep in `glfwWaitEventsTimeout()`.
- **Command pattern**: `CommandRegistry` + `ShortcutManager` + `UndoManager` provide a command system, but it's per-window, not global.
- **IPC layer**: Clean request/response protocol with snapshot + diff state sync, versioned headers, sequence numbers.
- **Adapter pattern**: ROS2 and PX4 are separate libraries that link against `spectra`, providing domain-specific UIs.

The architecture is **not** MVC/MVVM — the `Figure`/`Axes`/`Series` model objects are directly mutated by UI code, the renderer reads them directly, and there is no formal view-model separation. It's closer to a **retained-mode data model with immediate-mode UI overlay**.

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
    │    ├── glfwPollEvents() / glfwWaitEventsTimeout()
    │    ├── CommandQueue drain
    │    ├── Animator::evaluate()
    │    ├── For each WindowContext:
    │    │    ├── WindowRuntime::update()
    │    │    │    ├── FigureManager (tab creates/closes/switches)
    │    │    │    ├── Figure::on_frame callback (user animation)
    │    │    │    ├── Figure::compute_layout()
    │    │    │    ├── ImGuiIntegration::build_ui() (menus, inspector, canvas)
    │    │    │    ├── DockSystem::update_layout()
    │    │    │    └── InputHandler (pan, zoom, select, measure)
    │    │    └── WindowRuntime::render()
    │    │         ├── Backend::begin_frame() (acquire swapchain, wait fence)
    │    │         ├── Renderer::render_figure_content() (per-axes: upload dirty series, draw)
    │    │         ├── Renderer::render_plot_text() (SDF text)
    │    │         ├── Renderer::render_plot_geometry() (ticks, borders)
    │    │         ├── ImGuiIntegration::render() (ImGui draw commands)
    │    │         └── Backend::end_frame() (submit + present)
    │    ├── Process deferred detaches/moves
    │    └── FrameScheduler::end_frame() (sleep/spin to target FPS)
    │
    └── Multiproc path: Python → Unix socket → Backend daemon
         → FigureModel mutation → STATE_DIFF → Agent process → WindowRuntime
```

Series data flows: `User code → Series::set_x/y()` → `dirty_ = true` → next frame: `Renderer::upload_series_data()` → GPU SSBO → draw.

---

# High-Level Architecture Review

## What is working well

### 1. Clean rendering abstraction with practical Vulkan engineering

The `Backend` abstract interface (`src/render/backend.hpp`) is well-designed: opaque handles (`BufferHandle`, `PipelineHandle`, `TextureHandle`), clean lifecycle methods, no Vulkan types leak into the interface. The `VulkanBackend` implementation handles the hard parts correctly: deferred buffer deletion with frame-stamped rings, per-window command buffers and sync objects, swapchain recreation with proper fence ordering, multi-window support with per-window `WindowContext`.

The `Renderer` class properly manages per-series and per-axes GPU data with reusable scratch buffers, avoiding per-frame heap allocations. Camera-relative rendering (double-precision origin subtraction) prevents catastrophic cancellation at deep zoom. The deletion ring for series GPU resources is correct for `MAX_FRAMES_IN_FLIGHT=2`.

**Evidence**: `src/render/backend.hpp` (199 lines, zero Vulkan includes), `src/render/renderer.hpp` (FRAME_BUFFER_SLOTS, deletion_ring_, upload_scratch_), `src/render/vulkan/window_context.hpp` (clean per-window resource isolation).

### 2. Multi-window architecture is genuinely well-structured

`WindowUIContext` bundles all per-window UI state (ImGui context, figure manager, dock system, command registry, undo, shortcuts) into a single struct. Each window is independent: separate ImGui contexts, separate command registries, separate undo stacks. The `WindowManager` handles figure detach, move, preview windows. Tab drag-and-drop uses a clean state machine (`TabDragController`).

**Evidence**: `src/ui/app/window_ui_context.hpp` — every UI subsystem is per-window, not global. `src/ui/figures/tab_drag_controller.hpp` — explicit state machine (`Idle → DragStartCandidate → DraggingDetached`).

### 3. Thoughtful IPC protocol design

The `spectra::ipc` namespace has a proper binary protocol: magic bytes, versioned headers, request ID correlation, sequence numbers, snapshot + diff state sync with revision tracking. Message types cover the full lifecycle (handshake, figure CRUD, property updates, batch operations, input events, window management). The codec handles TLV encoding/decoding. This is production-grade.

**Evidence**: `src/ipc/message.hpp` (503 lines, well-structured enums and payloads), `src/ipc/codec.cpp` (1753 lines — comprehensive serialization).

### 4. Adapter pattern for domain extensions

ROS2 and PX4 adapters are cleanly separated as independent libraries (`spectra_ros2_adapter`, `spectra_px4_adapter`) that link against the core `spectra` library. They have their own `main.cpp`, app shell, UI panels, and domain logic. This is the right approach for domain-specific extensions.

**Evidence**: CMakeLists.txt lines 392-512 (ros2), 528-575 (px4) — separate library targets with proper dependencies.

### 5. Event-driven rendering

The `RedrawTracker` with grace frames and the `glfwWaitEventsTimeout()` integration is a correct and efficient approach. ~5x CPU/GPU reduction for static scenes. The `glfwPostEmptyEvent()` wake-up from the automation server thread is the right pattern for cross-thread wake-up.

### 6. Comprehensive test and example coverage

~130 unit tests, 8 benchmarks, golden image regression tests, QA agents, ~47 examples covering progressive complexity levels. The test infrastructure includes `ValidationGuard` (Vulkan validation layer checking), `GPUHangDetector`, and `MultiWindowFixture`.

### 7. Ergonomic public API

The `easy.hpp` MATLAB-style API (`plot()`, `scatter()`, `show()`, `subplot()`) with 7 progressive complexity levels is a genuine usability win. The fluent API on `Series` subclasses (`line.label("x").color(red).width(2.0)`) is clean.

## Main concerns

### 1. God objects: several files are dangerously large

| File | Lines | Role |
|---|---|---|
| `renderer.cpp` | 3,338 | All 2D/3D render logic, series upload, grid, text, borders, arrows |
| `vk_backend.cpp` | 2,932 | All Vulkan device, swapchain, pipeline, buffer, texture, multi-window ops |
| `window_manager.cpp` | 2,374 | Window creation, destruction, figure detach/move, preview, cross-window ops |
| `input.cpp` | 1,982 | All mouse/keyboard handling, pan, zoom, box select, hit testing |
| `daemon/main.cpp` | 1,916 | Entire backend daemon in one function (all message handling) |
| `register_commands.cpp` | 1,745 | All ~80+ command registrations in one function |

`daemon/main.cpp` is the worst offender: an entire daemon server with all message routing, figure model operations, agent management, and heartbeat logic in a single `main()` function (1,916 lines). This is a maintenance and testability hazard.

**Risk**: Any change to one rendering path (e.g., scatter 3D) requires editing a 3,338-line file. Any change to window management requires editing a 2,374-line file. New developers will struggle to find where logic lives.

### 2. `Figure` has 7 friend classes — it's a public header with private internals exposed

```cpp
friend class AnimationBuilder;
friend class App;
friend class FigureManager;
friend class WindowRuntime;
friend class SessionRuntime;
friend class FigureSerializer;
friend class EmbedSurface;
```

This means 7 different subsystems directly mutate `Figure`'s private state (animation timers, export paths, grid dimensions). There's no proper interface boundary between the data model and the runtime/UI layers. The `Figure` class is simultaneously:
- A user-facing API object
- An internal state container for animation
- An export request holder (png_export_path_, svg_export_path_, video_record_path_)
- A scroll state container

### 3. ThemeManager is a global singleton

`ui::ThemeManager::instance()` is called from at least 15 different locations across `register_commands.cpp`, `embed_surface.cpp`, `spectra_embed_c.cpp`, `qt_runtime.cpp`. This is the only singleton identified in the codebase, but it creates a hard dependency on global state that's difficult to test and incompatible with per-window theming.

### 4. The monolithic library prevents granular dependency control

All subsystems compile into one `libspectra`. The "logical" split targets (`spectra-core`, `spectra-ipc`, `spectra-render`) are INTERFACE targets that still link the full library. A headless Python client pulling in `spectra-core` still transitively depends on Vulkan, GLFW, ImGui, and the entire UI stack.

**Evidence**: CMakeLists.txt lines 582-592 — `add_library(spectra-core INTERFACE)` / `target_link_libraries(spectra-core INTERFACE spectra)`.

### 5. `Series::record_commands(Renderer&)` — data model knows about rendering

The `Series` base class declares `virtual void record_commands(Renderer& renderer) = 0` in the **public header** (`include/spectra/series.hpp`). This means:
- Every user who includes `<spectra/series.hpp>` transitively pulls in `Renderer` (forward-declared via `fwd.hpp`, but still a conceptual coupling).
- The data model has a direct dependency on the rendering layer.
- Series subclasses need to know how to talk to the Renderer.

This violates separation of concerns. The Renderer should know how to draw each Series type; the Series should not need to know about the Renderer.

### 6. `app.hpp` includes internal headers from `src/`

```cpp
#include "../src/ui/figures/figure_registry.hpp"
```

A public API header reaching into `src/` internals is an encapsulation violation. Users of the public API are exposed to internal implementation details.

### 7. No formal event/notification system

State changes flow through direct method calls, friend class access, dirty flags, and deferred queues. There's no pub/sub or observer pattern for cross-subsystem notifications. When a figure's data changes, the renderer discovers it via polling `is_dirty()`. When a tab is dragged, it's queued as a `PendingDetach` and processed later. This works but doesn't scale to plugin or extension scenarios where external code needs to react to state changes.

### 8. Series data is NOT thread-safe

The CODEBASE_MAP explicitly states: *"Series data: NOT thread-safe — must be mutated from main thread or user's on_frame callback."* For a library targeting live sensor streams (ROS2, MAVLink), this is a significant limitation. The ROS2 adapter likely works around this by buffering data in its own thread-safe structures and flushing to Series on the main thread, but this pattern is undocumented and fragile.

## Architectural risks

### Risk 1: Daemon monolith (`daemon/main.cpp`)
The 1,916-line `main()` function contains all message routing, state management, and process lifecycle. Any bug in message handling can crash the entire daemon. Untestable in isolation.

### Risk 2: `WindowUIContext` is non-movable, non-copyable, 20+ members
This struct has grown organically. It's not a designed interface — it's a bag of everything a window needs. Adding a new UI feature means adding another member here. There's no lifecycle protocol (init/shutdown ordering).

### Risk 3: Rendering pipeline is tightly coupled to figure layout
`Renderer::render_figure_content()` directly reads `Figure::compute_layout()` results, iterates axes, checks series types via `dynamic_cast` (cached to avoid per-frame cost). If the layout model changes (e.g., to support constraints or responsive layouts), the renderer must change too.

### Risk 4: `#ifdef` soup for feature flags
Feature flags (`SPECTRA_USE_GLFW`, `SPECTRA_USE_IMGUI`, `SPECTRA_USE_FFMPEG`, `SPECTRA_USE_EIGEN`, `SPECTRA_USE_ROS2`, `SPECTRA_USE_PX4`) are scattered as `#ifdef` guards throughout the codebase, including in headers like `window_ui_context.hpp`, `session_runtime.hpp`, and `window_runtime.hpp`. This makes code hard to read, and any combination of flags creates a different binary with potentially different behavior.

### Risk 5: Plugin API is thin and not yet integrated into the render or data layers
The plugin C ABI only exposes `CommandRegistry`, `ShortcutManager`, and `UndoManager`. Plugins cannot:
- Add new series types
- Add new render pipelines
- Access figure/axes data
- Register overlays
- Hook into the data pipeline (transforms, decimation)
- React to state changes

This limits plugins to "add a menu command" which is insufficient for a serious extensibility story.

---

# Recommended Improvements

## Quick wins

### QW-1: Extract `daemon/main.cpp` message handlers into separate classes

- **What**: Split the 1,916-line `main()` into a `DaemonServer` class with `PythonMessageHandler`, `AgentMessageHandler`, and `HeartbeatMonitor` components. Each handler processes its message types and operates on `FigureModel`/`SessionGraph` via injected references.
- **Why**: The current monolith is untestable, hard to debug, and a merge conflict hotspot.
- **Benefit**: Each handler becomes independently testable. New message types don't require touching a 2K-line function.
- **Difficulty**: Low-medium. Pure refactor, no behavior change.
- **Strategy**: Start by extracting helper functions, then group into classes. Keep the event loop in `main()`.

### QW-2: Move `Series::record_commands()` to a visitor or strategy in the Renderer

- **What**: Remove `virtual void record_commands(Renderer&)` from `Series`. Instead, have `Renderer` use a type switch (already has `SeriesType` cache) to dispatch rendering per series type.
- **Why**: Decouples the public data model from the rendering layer. Users of `<spectra/series.hpp>` no longer transitively depend on `Renderer`.
- **Benefit**: Cleaner public API boundary. Easier to add new renderers (e.g., CPU rasterizer for headless SVG).
- **Difficulty**: Low. The Renderer already caches series types. Replace `series.record_commands(*this)` with `render_series_by_type(series)`.
- **Strategy**: Add the type-dispatched method, delegate from it, then remove the virtual.

### QW-3: Fix `app.hpp` internal include

- **What**: Replace `#include "../src/ui/figures/figure_registry.hpp"` in `include/spectra/app.hpp` with a forward declaration or a public-facing `FigureId` type + minimal interface.
- **Why**: Public headers must not include private headers. This breaks encapsulation and makes the install layout fragile.
- **Benefit**: Clean public/private boundary.
- **Difficulty**: Low.

### QW-4: Extract `Figure` export/animation state into separate structs

- **What**: Move `png_export_path_`, `svg_export_path_`, `video_record_path_`, `anim_fps_`, `anim_duration_`, `anim_loop_`, `anim_on_frame_`, `anim_time_` into a `FigureAnimState` and `FigureExportRequest` struct. Reduce friend class count.
- **Why**: `Figure` currently mixes user-facing data model concerns with runtime animation and export transport. 7 friend classes is a code smell.
- **Benefit**: Clearer ownership boundaries. Fewer friend classes needed.
- **Difficulty**: Low-medium.

## Medium-size refactors

### MR-1: Split `renderer.cpp` (3,338 lines) into focused modules ✅ DONE

- **What**: Extracted into:
  - `render_2d.cpp` (598 lines) — 2D series rendering (line, scatter, stat fill)
  - `render_3d.cpp` (560 lines) — 3D series rendering (line3d, scatter3d, surface, mesh)
  - `render_geometry.cpp` (1,142 lines) — Grid, border, tick marks, arrows
  - `render_upload.cpp` (426 lines) — Series GPU data upload, dirty tracking
  - `renderer.cpp` (735 lines) — Orchestration (begin/end render pass, figure dispatch)
- **Why**: The original file was too large for any single developer to hold in working memory. Changes to 3D rendering risk breaking 2D rendering and vice versa.
- **Benefit**: Faster compile times (partial recompilation), easier code review, clearer ownership.
- **Difficulty**: Medium. Methods remain `Renderer` members via separate translation units.

### MR-2: Split `vk_backend.cpp` (2,932 lines) into focused modules ✅ DONE

- **What**: The Vulkan backend already has `vk_buffer.cpp`, `vk_device.cpp`, `vk_pipeline.cpp`, `vk_swapchain.cpp` as separate files. The remaining 2,932-line `vk_backend.cpp` still contained: multi-window management, framebuffer operations, descriptor pool management, texture management, frame lifecycle.
  - Extracted `vk_texture.cpp` (370 lines — texture create/destroy, image upload)
  - Extracted `vk_frame.cpp` (485 lines — begin/end frame, render pass, bind, draw, queries)
  - Extracted `vk_capture.cpp` (453 lines — framebuffer readback and capture)
  - Extracted `vk_multi_window.cpp` (397 lines — window context management)
  - Remaining `vk_backend.cpp`: 1,299 lines (init/shutdown, surface, swapchain, pipelines, buffers, descriptors)
- **Why**: Same as MR-1. 2,932 lines is a maintenance burden.
- **Difficulty**: Medium. The Vulkan state is intertwined, but the method boundaries are clear.

### MR-3: Replace `ThemeManager` singleton with injected dependency

- **What**: Make `ThemeManager` a regular owned object in `App` (or `SessionRuntime`). Pass it as a reference/pointer to subsystems that need it.
- **Why**: Singletons block testability (can't test two themes in one process), block multi-window theming (different themes per window), and create hidden coupling.
- **Benefit**: Testable, multi-theme capable, explicit dependency graph.
- **Difficulty**: Medium. ~15 call sites need updating. Some are in command lambdas that capture pointers.
- **Strategy**: Add `ThemeManager*` to `WindowUIContext`. Change `ThemeManager::instance()` to return from a thread-local or context variable during transition.

### MR-4: Make the library actually split-able ✅ DONE

- **What**: Factored the CMake build so that `spectra-core` (data model, math, transforms, I/O export, animation, camera, knob manager) is a **real** static/shared library that does not depend on Vulkan, GLFW, or ImGui. `spectra` links `spectra-core` PUBLIC and adds the rendering, UI, IPC, and platform layers.
  - Created `libspectra-core.a` (~9MB): `src/core/`, `src/math/`, `src/data/`, `src/io/`, `src/anim/`, camera, knob manager
  - Remaining `libspectra.a` (~79MB): render, platform, UI, IPC, daemon, embed, third-party
  - Moved `knob_manager.hpp` from `src/ui/overlay/` to `include/spectra/` (was the last public→private include violation)
  - Camera (`src/ui/camera/camera.cpp`) moved to spectra-core (pure math, no render deps)
  - Prerequisite QW-2 (`Series::record_commands` removal) and QW-3 (`app.hpp` internal include fix) were already completed
- **Why**: Enables headless usage (Python bindings, CI/CD, testing) without pulling in GPU dependencies. Enables embedding in Qt or other frameworks without GLFW.
- **Benefit**: Smaller binaries for headless use. Cleaner dependency graph. Easier packaging. Downstream consumers can now `target_link_libraries(my_app PRIVATE spectra-core)` for headless data manipulation without any GPU stack.
- **Difficulty**: Medium-high. Required untangling `#include` chains and moving pure-math code out of the UI layer.

### MR-5: Add an observer/event system for cross-subsystem notifications

- **What**: Introduce a lightweight event bus or signal/slot system. Key events: `FigureCreated`, `FigureDestroyed`, `SeriesDataChanged`, `AxesLimitsChanged`, `ThemeChanged`, `WindowOpened`, `WindowClosed`.
- **Why**: Currently, state changes propagate through polling (`is_dirty()`), friend class access, and ad-hoc callbacks. Plugins and adapters have no way to react to state changes.
- **Benefit**: Enables reactive plugins, decouples subsystems, enables live data synchronization.
- **Difficulty**: Medium.
- **Strategy**: Start with a simple `EventBus<EventType>` template. Wire up `FigureRegistry` and `Series` first.

## Long-term architecture direction

### LT-1: Introduce a FigureModel / ViewModel separation

- **What**: Currently, `Figure` / `Axes` / `Series` are simultaneously the user API, the renderer's data source, and the serialization target. Introduce a `FigureViewModel` that the UI reads from and the renderer draws. The core `Figure` becomes a pure data model. The ViewModel handles layout, visibility, selection state, animation state.
- **Why**: This is the standard approach for complex desktop UIs. It enables:
  - Undo/redo at the model level (not UI level)
  - Serialization of pure data (not UI state)
  - Multiple views of the same data (split view already hints at this need)
  - Proper testing (test the model without the renderer)
- **Benefit**: Clean architecture, easier testing, proper undo/redo, eventual collaborative editing.
- **Difficulty**: High. Fundamental restructuring. 6-12 months of incremental migration.
- **Strategy**: Start with new features using the ViewModel pattern. Gradually migrate existing code.

### LT-2: Expand the plugin API to cover data, rendering, and UI extension points

- **What**: The current plugin API only exposes command registration. Expand to support:
  - Custom series types (register new series + render pipeline)
  - Custom overlays (register ImGui draw callbacks)
  - Data source plugins (register adapters like ROS2/PX4 as plugins instead of compile-time libraries)
  - Transform plugins (register custom data transforms)
  - Export format plugins
- **Why**: The current adapter approach (compile-time library) doesn't scale. Users should be able to add domain-specific visualizations without rebuilding Spectra.
- **Benefit**: Ecosystem growth, reduced core maintenance burden, domain-specific customization.
- **Difficulty**: High. Requires stable internal APIs, versioned plugin interfaces, careful ABI management.
- **Strategy**: Start with transform plugins (simplest). Then overlay plugins. Then series type plugins (hardest).

### LT-3: Thread-safe data model for streaming data

- **What**: Make `Series` data thread-safe. Options:
  1. Copy-on-write with atomic pointer swap (reader sees consistent snapshot)
  2. Double-buffer: writer fills back buffer, renderer reads front buffer, swap at frame boundary
  3. Lock-free ring buffer for append-only streaming data
- **Why**: ROS2 and PX4 adapters receive data on background threads but must flush to `Series` on the main thread. This creates latency and complexity. A thread-safe data model would simplify adapters and enable higher-throughput streaming.
- **Benefit**: Simpler adapter code, lower latency for streaming data, safer concurrent access.
- **Difficulty**: High. Requires careful design to avoid GPU hazards.
- **Strategy**: Start with option 2 (double-buffer) for `LineSeries` only. Measure. Extend.

---

# Final Verdict

## Current maturity level

**Mid-stage production project (v0.x → v1.0 transition)**. The core architecture is functional, the rendering pipeline is solid, the IPC protocol is well-designed, and the adapter model works. The codebase is large enough (~136K lines) that architectural debt is starting to create friction: god objects, tight coupling between data model and renderer, and a monolithic library make changes increasingly costly. The project has outgrown its original structure but hasn't yet undergone the refactoring needed for sustainable v1.0+ growth.

## Whether the architecture is healthy enough for growth

**Yes, with caveats.** The foundational choices (Vulkan abstraction, per-window UI isolation, IPC protocol, adapter pattern) are sound. The main blockers for growth are:
- The monolithic library structure (blocks headless/embedded use cases)
- God objects (blocks developer productivity and code review quality)
- Tight data-model/renderer coupling (blocks new renderers and plugin series types)
- Missing event system (blocks plugin ecosystem)

None of these are fundamental architectural flaws — they're evolutionary debt that can be paid down incrementally.

## Top 3 priorities

1. **Break up god objects** (QW-1, MR-1, MR-2) — highest ROI. The 5 files over 1,900 lines each are the primary source of developer friction and merge conflicts. Splitting them improves every contributor's daily experience.

2. **Decouple data model from renderer** (QW-2, QW-3, QW-4) — unblocks the library split (MR-4), plugin series types (LT-2), and headless usage. Start with removing `record_commands()` from `Series`.

3. **Make the library genuinely split-able** (MR-4) — unblocks Python-only headless usage, Qt embedding without GLFW, and CI without GPU. This is the strategic enabler for the next phase of adoption.

---

# Suggested Next Review Pass

Three focused deep dives that would provide the most additional insight:

1. **Rendering architecture deep dive** — Examine `renderer.cpp` (3,338 lines) and `vk_backend.cpp` (2,932 lines) in detail. Map the actual pipeline stages, identify redundant state management, find opportunities for draw call batching and pipeline state caching. Check for GPU hazards in the multi-window render path. Review the 3D transparency rendering order.

2. **State/command system deep dive** — Trace a complete command lifecycle from `CommandRegistry::register_command()` through `ShortcutManager` dispatch to `UndoManager` integration. Evaluate whether the per-window command registry model scales, whether undo/redo handles cross-window operations correctly, and whether the deferred execution pattern (command queues, pending detaches/moves) has race conditions.

3. **Data model / figure model deep dive** — Examine the full `Figure` → `Axes` → `Series` ownership chain, the dirty flag propagation, the thread safety boundaries, and the serialization path (`FigureSerializer`, `Workspace`). Map all the places where `Figure` private state is accessed via friend classes. Design the ViewModel separation boundary.
