# Spectra Architecture Review — V3

> **Date**: 2026-04-04
> **Baseline**: V2 review (2026-04-03)
> **Scope**: Full codebase audit reflecting all completed refactors, new subsystems, and remaining debt

---

## What the project is

Spectra is a GPU-accelerated scientific/engineering plotting and visualization application written in C++20. It targets serious engineering use cases: interactive 2D/3D plotting, real-time streaming data (ROS2, PX4 flight logs, MAVLink), animated visualizations, and offscreen export (PNG, SVG, video). It supports two runtime modes — **inproc** and **multiproc** (daemon + agent via Unix IPC). Python bindings provide a MATLAB-style `plot()`/`show()` API. The UI is built on Dear ImGui with docking/tab system, command palette, undo/redo, theming, event bus, automation/MCP server, plugin API with crash isolation, and nascent accessibility support.

Two rendering backends: **Vulkan** (primary, production) and **WebGPU** (experimental, browser/wasm via Dawn or Emscripten).

**Scale**: ~147K lines of C++ (`src/`), ~88K lines of tests, ~16K lines of Python bindings, ~52 examples, ~157 unit test files, 9 benchmarks, golden image regression suite, plus ROS2 and PX4 adapter libraries.

---

## Main subsystems

| Subsystem | Location | Responsibility |
|---|---|---|
| **Core data model** | `src/core/`, `include/spectra/` | Figure, Axes (2D/3D), Series hierarchy (line, scatter, bar, histogram, violin, box plot, surface, mesh, shapes 2D/3D, custom plugin series, **chunked line series**), layout, color, style |
| **Vulkan rendering** | `src/render/`, `src/gpu/shaders/` | `Backend` abstraction, `VulkanBackend` (split: `vk_backend`, `vk_frame`, `vk_capture`, `vk_multi_window`, `vk_texture`), `Renderer` (split: orchestration, `render_2d`, `render_3d`, `render_geometry`, `render_upload`), `TextRenderer`, `SeriesTypeRegistry`, GLSL shaders |
| **WebGPU rendering** | `src/render/webgpu/`, `src/gpu/shaders/wgsl/` | `WebGPUBackend` (Dawn + Emscripten/wasm), 5 WGSL shaders. Gated behind `SPECTRA_USE_WEBGPU`. |
| **UI framework** | `src/ui/` | ImGui integration, window management, input handling (split: orchestration + `input_pan_zoom`, `input_select`, `input_measure`, `input_annotate`), docking, overlays, animation, theme, command/shortcut/undo, `OverlayRegistry` |
| **ViewModel layer** | `src/ui/viewmodel/` | `FigureViewModel` — per-figure UI state with accessors, change notifications, undo integration |
| **App lifecycle** | `src/ui/app/` | `App`, `SessionRuntime` + `RedrawTracker`, `WindowRuntime`, `WindowUIContext`, `register_commands.cpp`, `ResourceMonitor`, `AnimationTickGate` |
| **IPC / multiproc** | `src/ipc/`, `src/daemon/`, `src/agent/` | 40-byte header + **FlatBuffers** payloads (`codec_fb`), legacy TLV (`codec`), schema (`spectra_ipc.fbs`), transport, `FigureModel`, `SessionGraph`, `ProcessManager`. Daemon: `DaemonServer` + 3 handler classes. |
| **Platform** | `src/platform/`, `src/ui/window/` | `SurfaceHost`, `GlfwSurfaceHost`, `GlfwAdapter`, `WindowManager` (split: orchestration + `window_lifecycle` + `window_figure_ops`) |
| **Adapters** | `src/adapters/` | ROS2 (~40 files), PX4 (ULog, MAVLink), Qt |
| **Data processing** | `src/data/`, `src/math/` | Decimation, filters, transforms, **ChunkedArray**, **MappedFile** (mmap), **LodCache** |
| **I/O & export** | `src/io/` | PNG, SVG, video export, **ExportRegistry** (plugin formats) |
| **Embedding** | `src/embed/` | `EmbedSurface`, C FFI |
| **Python bindings** | `python/` | Session management, figure/axes/series proxies, codec, transport, easy API, CLI |
| **Automation / MCP** | `src/ui/automation/` | `AutomationServer` (22+ tools), C++ MCP server (HTTP) |
| **Event system** | `include/spectra/event_bus.hpp` | `EventBus<T>` with re-entrancy guard, priority tiers, `ScopedSubscription` RAII, deferred emission. 9 event types. |
| **Plugin system** | `src/ui/workspace/` | C ABI v2.0 (6 extension points), `PluginManager`, `SeriesTypeRegistry`, **`PluginGuard`** (crash isolation) |
| **Accessibility** | `src/ui/accessibility/`, `src/ui/data/` | Sonification (y→pitch WAV), HTML table export, keyboard navigation commands |

---

## Architectural style

**Modular monolith with multi-backend rendering + event-driven frame loop + ViewModel separation.**

- **Split library**: `libspectra-core` (zero GPU deps) + `libspectra` (rendering, UI, IPC, platform)
- **Multi-backend**: `VulkanBackend` (production, 9 files) and `WebGPUBackend` (experimental). Shader programs in GLSL/SPIR-V and WGSL.
- **Event-driven rendering**: `RedrawTracker` with grace frames, ~5x idle CPU/GPU reduction
- **ViewModel pattern**: `FigureViewModel` separates UI state from data model
- **Event bus**: Typed pub/sub with re-entrancy guards, priority tiers, RAII subscriptions, deferred cross-thread emission
- **Plugin extensibility**: C ABI v2.0 with crash isolation via `PluginGuard`
- **IPC v2**: 40-byte binary header + FlatBuffers schema-defined payloads
- **Data virtualization**: `ChunkedLineSeries` + `MappedFile` + `LodCache` for out-of-core datasets
- **Adapter pattern**: ROS2 and PX4 as separate linked libraries

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
    │    ├── RedrawTracker: dirty→glfwPollEvents() / idle→glfwWaitEventsTimeout(0.1)
    │    ├── EventSystem::drain_all_deferred()
    │    ├── commit_thread_safe_series()
    │    ├── CommandQueue drain + Animator::evaluate()
    │    ├── For each WindowContext:
    │    │    ├── WindowRuntime::update()
    │    │    │    ├── FigureManager + FigureViewModel
    │    │    │    ├── Figure::on_frame, compute_layout()
    │    │    │    ├── ImGuiIntegration::build_ui()
    │    │    │    ├── DockSystem::update_layout()
    │    │    │    └── InputHandler (pan, zoom, select, measure, annotate)
    │    │    └── WindowRuntime::render()
    │    │         ├── Backend::begin_frame()
    │    │         ├── Renderer: upload → 2D → 3D → custom → text → geometry → ImGui
    │    │         └── Backend::end_frame()
    │    ├── Process deferred detaches/moves
    │    └── FrameScheduler::end_frame()
    │
    └── Multiproc: Python → socket → DaemonServer → handlers
         → FigureModel → STATE_DIFF (FlatBuffers) → Agent → WindowRuntime
```

Data flows:
- **Standard**: `Series::set_x/y()` → `dirty_=true` → `upload_series_data()` → GPU SSBO → draw
- **Thread-safe**: Producer → `Series::append()` → `PendingSeriesData` (SpinLock) → `commit_pending()` → GPU
- **Chunked**: `ChunkedLineSeries::load_mmap()` → `ChunkedArray` + `LodCache` → visible decimated subset → GPU

---

# Changes Since V2

## Completed refactors

### QW-5: Split `window_manager.cpp` ✅ DONE

| File | Lines | Responsibility |
|---|---|---|
| `window_manager.cpp` | 2,406 | Orchestration |
| `window_lifecycle.cpp` | 1,028 | Window creation/destruction |
| `window_figure_ops.cpp` | 705 | Figure detach/move/preview |

### QW-7: Split `input.cpp` ✅ DONE

| File | Lines | Responsibility |
|---|---|---|
| `input.cpp` | 926 | Orchestration, event dispatch |
| `input_pan_zoom.cpp` | 952 | Pan, zoom, scroll |
| `input_select.cpp` | 113 | Box/lasso selection |
| `input_measure.cpp` | 125 | Measurement tool |
| `input_annotate.cpp` | 90 | Annotation tool |

### MR-3: ThemeManager singleton ⚠️ PARTIAL

Now uses `set_current()`/`instance()` pattern. `WindowUIContext` holds injected `ThemeManager*`. ~9 call sites still use `instance()`. Per-window theming architecturally possible but not wired.

### MR-7: Plugin crash isolation ✅ DONE

`PluginGuard` (231 lines): `plugin_guard_invoke()` wraps plugin callbacks, catches C++ exceptions and fatal signals (`SIGSEGV`, `SIGBUS`, `SIGFPE`) via thread-local `sigsetjmp`/`siglongjmp`. Returns `PluginCallResult` enum. Test: `test_plugin_guard.cpp` (148 lines).

### LT-4: Event system hardening ✅ DONE

`EventBus<T>` (437 lines) now includes: re-entrancy guard (nested emit queues events), priority tiers (`High`/`Normal`/`Low`), `ScopedSubscription` RAII, deferred cross-thread emission. Test: `test_event_bus.cpp` (689 lines).

### LT-8: Data virtualization ✅ DONE

| Component | File | Lines |
|---|---|---|
| `ChunkedArray` | `src/data/chunked_array.hpp` | 204 |
| `MappedFile` | `src/data/mapped_file.hpp/cpp` | 249 |
| `LodCache` | `src/data/lod_cache.hpp` | 204 |
| `ChunkedLineSeries` | `include/spectra/chunked_series.hpp` + `src/core/chunked_series.cpp` | 520 |

Features: chunked storage (1M elements/chunk), mmap for zero-copy file access, multi-resolution decimation pyramid, rolling-window memory budgets, renderer uploads only visible decimated subset. Tests: 607 lines.

### LT-9: IPC protocol v2 (FlatBuffers) ✅ DONE

| Component | File | Lines |
|---|---|---|
| Schema | `src/ipc/schemas/spectra_ipc.fbs` | 325 |
| Codec | `src/ipc/codec_fb.cpp/hpp` | 1,321 |
| Legacy | `src/ipc/codec.cpp` | 1,799 (retained) |

40-byte header unchanged. Only payloads use FlatBuffers. Schema evolution rules enforced. Test: `test_ipc_flatbuffers.cpp` (388 lines).

### LT-10: WebGPU / WebAssembly ✅ DONE

| Component | File | Lines |
|---|---|---|
| Backend | `src/render/webgpu/wgpu_backend.hpp/cpp` | 1,651 |
| WGSL shaders | `src/gpu/shaders/wgsl/` (5 files) | 595 |
| Example | `examples/webgpu_demo.cpp` | 156 |

Implements `Backend` interface for Dawn (native) and Emscripten (browser). Push constants emulated via uniform buffer. Three pipeline layout families. Gated behind `SPECTRA_USE_WEBGPU`. Test: `test_webgpu_backend.cpp` (258 lines).

### LT-11: Accessibility ⚠️ PARTIAL

| Feature | Status |
|---|---|
| Sonification (y→pitch WAV) | ✅ Done (200 lines) |
| HTML table export | ✅ Done (261 lines) |
| Keyboard navigation commands | ✅ Partial |
| Screen reader integration | ❌ Not started |

Test: `test_accessibility.cpp` (259 lines).

---

## What is working well

1. **Multi-backend rendering** — `VulkanBackend` (9 files) + `WebGPUBackend` (1,651 lines) both implement the same opaque-handle `Backend` API. Validates the original abstraction design.
2. **Decomposed rendering** — Renderer in 5 files (3,671 lines total), Vulkan in 9 files. Each path independently navigable.
3. **Multi-window** — `WindowUIContext` per window, independent ImGui contexts. `WindowManager` decomposed into 3 files.
4. **Event-driven rendering** — `RedrawTracker` + grace frames, ~5x idle reduction.
5. **ViewModel separation** — `FigureViewModel` with accessors, change callbacks, undo integration.
6. **Hardened event system** — Re-entrancy guards, priority tiers, `ScopedSubscription` RAII, deferred emission.
7. **Plugin API + crash isolation** — v2.0 C ABI (6 extension points) + `PluginGuard` signal handler.
8. **Daemon decomposition** — 359-line main + 4 handler classes (1,586 lines total).
9. **Schema-defined IPC** — FlatBuffers with evolution rules, zero-copy deserialization.
10. **Data virtualization** — `ChunkedLineSeries` + `MappedFile` + `LodCache` for out-of-core datasets.
11. **Comprehensive tests** — 157 unit test files, 9 benchmarks, golden image regression, QA agents.
12. **Ergonomic public API** — `easy.hpp` with 7 complexity levels, fluent API on all series types.

---

## Remaining concerns

1. **`Figure` still has 7 friend classes** — `AnimationBuilder`, `App`, `FigureManager`, `WindowRuntime`, `SessionRuntime`, `FigureSerializer`, `EmbedSurface`. 31 public methods coexist with 8 private members accessed via friends. Figure mixes data model, export container, animation state, and scroll state responsibilities.
2. **`ThemeManager` still uses static accessor** — 3 remaining `instance()` sites in `imgui_integration.cpp` (lines ~407, ~885, ~1227). `set_current()` injection exists in `window_lifecycle.cpp` and `register_commands.cpp` but is not fully adopted. Blocks per-window theming.
3. **`window_manager.cpp` still 2,406 lines** — Despite extraction of lifecycle (1,028) and figure ops (705). GLFW callback implementations (~400 lines) and preview window pooling logic (~320 lines) are candidates for further extraction.
4. **`register_commands.cpp` at 1,878 lines** — Single `register_standard_commands()` with ~80 command registrations across 10 categories (view, edit, figures, layout, export, animation, inspection, data, ROS2, tools). Heavy lambda capture duplication: `[&ui_ctx, &registry, active_figure_ptr, ...]` repeated ~50 times.
5. **`#ifdef` soup** — 17 ifdef blocks in `window_lifecycle.cpp`, 14 in `window_manager.cpp`, 11 in `tab_bar.cpp` (many redundant — entire file requires `SPECTRA_USE_IMGUI` but still guards internally). WebGPU adds another dimension.
6. **No `WindowUIContext` lifecycle protocol** — Flat struct with 25 members (6 raw pointers, 4 unique_ptrs, ~15 value-type objects). Non-copyable, non-movable. No explicit init/shutdown ordering — initialization scattered across `window_lifecycle.cpp` and `app_step.cpp`. Adding a new overlay requires touching 3 files with undocumented ordering constraints.
7. **WebGPU missing 3D shaders** — Only 5 of 11 shader pairs ported to WGSL.
8. **Dual codec maintenance** — `codec.cpp` (1,799 lines, TLV) + `codec_fb.cpp` (1,177 lines, FlatBuffers). Both handle Handshake, Figure, Axis, Property, and Snapshot message types. Series streaming operations remain TLV-only. Any new message type must be added to both codecs plus the format detector.
9. **Accessibility nascent** — Sonification + HTML export only. No screen reader integration.
10. **`inspector.cpp` at 1,350 lines** — `draw_series_browser()` alone is ~400 lines with header/paste, multi-select bulk bar, and per-row rendering interleaved. Statistics and properties sections (~350 lines combined) have natural extraction points.
11. **`render_geometry.cpp` at 1,800 lines** — `render_plot_text()` is ~500 lines mixing 2D tick labels, 3D billboard labels, and shape annotations. `render_plot_geometry()` is ~600 lines mixing grid generation with border/tick rendering. Each 2D/3D path is independently extractable.
12. **`imgui_integration.cpp` at 1,200 lines** — `build_ui()` is a ~400-line mega-dispatcher handling frame setup, panel animations, zone drawing, overlays, and deferred dialogs in one function. ~20+ `PushStyleColor`/`PopStyleColor` pairs with repeated theme patterns.
13. **`automation_server.cpp` execute() dispatch** — 668-line if-chain dispatching 21 tool methods via hardcoded string comparisons. No dispatch table or registration mechanism.
14. **`FigureManager` mixes 9 responsibilities** — 25+ public methods spanning lifecycle (5), cross-window transfer (2), navigation (4), state queries (7), title/metadata (4), callbacks (3), and batch operations (4). Init-time circular dependency with `TabDragController` (`WindowManager` → `TabDragController` → `WindowManager`).

---

## Architectural risks

1. **`WindowUIContext` sprawl** — 25 members, organic growth, no lifecycle protocol. Implicit init ordering makes it easy to introduce use-before-init bugs when adding new subsystems.
2. **Plugin ABI stability** — v2.0 with 6 extension points. `PluginGuard` catches CPU faults but cannot recover GPU hangs from faulting plugins. No per-plugin GPU resource tracking.
3. **Renderer↔layout coupling** — `render_figure_content()` directly reads `compute_layout()` results. Tightly binds rendering to layout pass output format.
4. **WebGPU shader gap** — 6 of 11 shader pairs missing WGSL equivalents. 3D rendering unusable on WebGPU.
5. **Legacy codec deprecation** — No timeline for TLV removal. Dual codecs create ongoing overhead for every new IPC message type.
6. **Circular initialization dependencies** — `TabDragController` ↔ `WindowManager` callbacks must be wired in specific order in `init_window_ui()`. Undocumented and fragile.
7. **God object persistence** — `WindowManager` (~40 public methods), `FigureManager` (~25), `ImGuiIntegration` (~30), `Inspector` (~20). Despite prior splits, core orchestration classes still accumulate responsibilities.

---

## Remaining recommendations

### Quick wins

- **QW-8**: Extract GLFW callbacks from `window_manager.cpp` → `window_glfw_callbacks.cpp` (~400 lines). Six callback implementations (`cursor_pos`, `mouse_button`, `scroll`, `key`, `char`, `drop`) plus `install_input_callbacks()` have no dependency on window lifecycle state.
- **QW-9**: Split `register_commands.cpp` into per-category registration functions in separate translation units. Identified categories: view (7 cmds), edit (6), figures (4), layout (4), export (4), animation (5), inspection (3), data (3), ROS2 (3, ifdef), tools (3). Introduce a `CommandContext` struct to eliminate the repeated ~8-variable lambda capture block duplicated ~50 times.
- **QW-10**: Deprecate legacy TLV codec — set timeline, migrate Python bindings to FlatBuffers. Document the message-to-codec assignment in a table: TLV-only (series streaming), FB-only (figure state diff), both (handshake, property).
- **QW-11**: Replace `automation_server.cpp` if-chain (668 lines, 21 branches) with a `std::unordered_map<std::string, ToolHandler>` dispatch table populated at init. Eliminates string-comparison cascade and provides a single source of truth for tool enumeration.
- **QW-12**: Remove redundant `#ifdef` guards — `tab_bar.cpp` has 11 `SPECTRA_USE_IMGUI` guards but the file is only compiled when that flag is ON. Same pattern in `embed_surface.cpp` (10 guards). Replace with compilation-unit gating in CMakeLists.txt.

### Medium refactors

- **MR-3**: Complete `ThemeManager` injection — replace 3 remaining `instance()` sites in `imgui_integration.cpp` (lines ~407, ~885, ~1227). Store `ThemeManager*` in `ImGuiIntegration` and pass during init. Unblocks per-window theming.
- **MR-6**: `WindowUIContext` lifecycle protocol — group 25 members into sub-structs (`InputBundle`, `CommandBundle`, `AnimationBundle`, `LayoutBundle`), add explicit `init_phase_N()`/`shutdown()` methods with documented ordering. Eliminate `fig_mgr`/`fig_mgr_owned` redundancy.
- **MR-8**: WebGPU 3D shader parity — port 6 remaining GLSL→WGSL shader pairs.
- **MR-9**: GPU resource cleanup for faulting plugins — track per-plugin GPU resources in `SeriesTypeRegistry` and release on `PluginGuard` fault.
- **MR-10**: Split `inspector.cpp` (1,350 lines). Extract `draw_series_browser()` (400 lines) into `inspector_series_browser.cpp` with sub-functions: `draw_series_browser_header()`, `draw_bulk_action_bar()` (~150 lines), `draw_series_rows()` (~220 lines). Extract statistics helpers (200 lines) into `inspector_stats.cpp`.
- **MR-11**: Split `render_geometry.cpp` (1,800 lines). Extract `render_plot_text()` (500 lines) → `render_text_labels.cpp` with separate 2D/3D label functions. Extract `render_grid()` (300 lines, 3D) → `render_3d_grid.cpp`. Leaves `render_plot_geometry()` as 2D border/tick orchestration (~600 lines).
- **MR-12**: Decompose `build_ui()` in `imgui_integration.cpp` (~400 lines) into: `update_frame_state()`, `update_panel_animations()`, `draw_zones()`, `draw_interactive_overlays()`, `draw_deferred_dialogs()`. Extract repeated `PushStyleColor` patterns into `ImGuiStyleHelpers`.
- **MR-13**: Narrow `Figure` friend list from 7 to ≤3. Add public API: `queue_export_request()` (removes `FigureSerializer` friend), move scroll state (`scroll_offset_y_`, `content_height_`) to `FigureViewModel` (removes `WindowRuntime` friend), extract animation state to `AnimationStateProxy` (removes `AnimationBuilder` friend). Target: keep only `App`, `FigureManager`, `SessionRuntime` as friends.
- **MR-14**: Decompose `FigureManager` (25 public methods, 9 responsibilities). Extract `FigureNavigator` (switch_to, move_tab, active_index — 4 methods), `FigureFactory` (create, close, duplicate — 5 methods), and `FigureStateManager` (state, save/restore, metadata — 7 methods). Keep `FigureManager` as coordinating facade.
- **MR-15**: Document and formalize circular init dependencies — `TabDragController` ↔ `WindowManager` callback wiring. Option A: introduce a `WindowInitializationContext` that resolves all cross-dependencies at once. Option B: use two-phase init (construct, then wire).

### Long-term

- **LT-5**: ViewModel for Axes and Series (carried from V2)
- **LT-6**: GPU compute pipeline for decimation/transforms/FFT (carried from V2)
- **LT-7**: Render graph abstraction (carried from V2, only if LT-6 justifies)
- **LT-12**: Collaborative/remote rendering (carried from V2)
- **LT-13**: Complete accessibility — keyboard nav, screen readers, high-contrast improvements
- **LT-14**: Unified shader toolchain — SPIR-V→WGSL transpiler (Tint/Naga) for single source-of-truth
- **LT-15**: Decompose `WindowManager` (~40 public methods) into focused managers: `WindowLifecycleManager` (create, destroy, focus, close), `PreviewWindowManager` (warmup, request, process, destroy), `WindowInputDispatcher` (install callbacks, routing). Keep `WindowManager` as thin facade.
- **LT-16**: Flatten `#ifdef` architecture — replace interior preprocessor guards with compilation-unit separation. Each feature flag selects which `.cpp` files are compiled rather than guarding code within shared files. Target files: `window_lifecycle.cpp` (17 guards), `window_manager.cpp` (14 guards).

---

# Progress scorecard (V1 → V2 → V3)

| Item | V2 | V3 | Notes |
|---|---|---|---|
| QW-1: Daemon handlers | ✅ | ✅ | 359-line main + 4 classes |
| QW-2: Remove `record_commands()` | ✅ | ✅ | Type-dispatched |
| QW-3: Fix `app.hpp` include | ✅ | ✅ | Public header |
| QW-4: Figure export/anim state | ✅ | ✅ | Sub-structs |
| QW-5: Split `window_manager` | ❌ | ✅ | 3 files |
| QW-6: Split `register_commands` | ✅ | ✅ | Category functions |
| QW-7: Split `input.cpp` | ❌ | ✅ | 5 files |
| MR-1: Split `renderer.cpp` | ✅ | ✅ | 5 files |
| MR-2: Split `vk_backend.cpp` | ✅ | ✅ | 9 files |
| MR-3: ThemeManager singleton | ❌ | ⚠️ | `set_current()` partial |
| MR-4: Library split | ✅ | ✅ | `spectra-core` real lib |
| MR-5: Event system | ✅ | ✅ | Hardened |
| MR-7: Plugin crash isolation | ❌ | ✅ | `PluginGuard` |
| LT-1: ViewModel separation | ✅ | ✅ | Phases 1-3 |
| LT-2: Plugin API v2.0 | ✅ | ✅ | 6 extension points |
| LT-3: Thread-safe Series | ✅ | ✅ | PendingSeriesData |
| LT-4: Event hardening | ❌ | ✅ | Re-entrancy, priority, RAII |
| LT-8: Data virtualization | ❌ | ✅ | ChunkedLineSeries + mmap |
| LT-9: FlatBuffers IPC | ❌ | ✅ | Schema-defined |
| LT-10: WebGPU backend | ❌ | ✅ | Dawn + Emscripten |
| LT-11: Accessibility | ❌ | ⚠️ | Sonification + HTML export |

**18 of 21** items done (2 partial, 1 remaining from V1).

---

# Final verdict

## Current maturity level

**Production-ready v1.0 candidate.** Every major V1 concern addressed: god objects decomposed, data model decoupled from renderer, library split, event system hardened, ViewModel complete, plugin API comprehensive with crash isolation, IPC schema-defined, thread-safe streaming, data virtualization, second rendering backend validating the abstraction.

Growth: ~136K → ~147K lines of C++ while decomposing monoliths and expanding tests to ~88K lines (157 unit test files).

## Top 3 priorities for V3→V4

1. **Decompose remaining god objects** (QW-8, QW-9, MR-10, MR-11, MR-14) — `window_manager.cpp` (2,406), `register_commands.cpp` (1,878), `inspector.cpp` (1,350), `render_geometry.cpp` (1,800), and `FigureManager` (25 methods) are the largest remaining monoliths. Each has identified extraction targets.
2. **`WindowUIContext` lifecycle protocol** (MR-6) — 25 unordered members with implicit init dependencies block safe extension of the UI framework. Most other refactors depend on a formalized lifecycle.
3. **Retire legacy TLV codec + complete WebGPU parity** (QW-10, MR-8) — Dual codec maintenance and partial WGSL shader coverage are the main ongoing overhead taxes.

---

# Appendix: Key file sizes (V1 → V2 → V3)

| File | V1 | V2 | V3 | Change |
|---|---|---|---|---|
| `renderer.cpp` | 3,338 | 761 | 768 | Split into 5 files |
| `vk_backend.cpp` | 2,932 | 1,384 | 1,384 | Split into 9 files |
| `window_manager.cpp` | 2,374 | 2,406 | 2,406 | +split: lifecycle(1,028)+fig_ops(705) |
| `input.cpp` | 1,982 | 1,982 | 926 | Split into 5 files |
| `daemon/main.cpp` | 1,916 | 359 | 359 | Split into 4 handler classes |
| `register_commands.cpp` | 1,745 | 1,753 | 1,878 | +125 (a11y commands) |
| `render_geometry.cpp` | — | — | 1,800 | Text+grid+border rendering |
| `inspector.cpp` | — | — | 1,350 | Property inspector, series browser |
| `imgui_integration.cpp` | — | — | 1,200 | ImGui backend, build_ui() dispatcher |
| `automation_server.cpp` | — | — | 1,200 | 21 tools, 668-line dispatch chain |
| `event_bus.hpp` | — | 214 | 437 | +223 (hardening) |
| `plugin_api.hpp/cpp` | ~500 | 1,711 | 1,713 | v2.0 |
| **New V3**: `wgpu_backend` | — | — | 1,651 | WebGPU backend |
| **New V3**: `wgsl/` shaders | — | — | 595 | 5 WGSL shaders |
| **New V3**: chunked data | — | — | 1,177 | ChunkedArray+MappedFile+LodCache |
| **New V3**: `chunked_series` | — | — | 520 | Out-of-core series |
| **New V3**: `codec_fb` | — | — | 1,321 | FlatBuffers codec |
| **New V3**: `spectra_ipc.fbs` | — | — | 325 | IPC schema |
| **New V3**: `plugin_guard` | — | — | 231 | Crash isolation |
| **New V3**: accessibility | — | — | 461 | Sonification + HTML export |

# Appendix: Refactoring candidate inventory

Files over 800 lines in `src/` with identified extraction targets:

| File | Lines | Extraction targets | Est. residual |
|---|---|---|---|
| `window_manager.cpp` | 2,406 | GLFW callbacks (~400), preview pool (~320) | ~1,700 |
| `register_commands.cpp` | 1,878 | 10 category TUs (~150-200 each) | ~200 (scaffolding) |
| `render_geometry.cpp` | 1,800 | `render_text_labels.cpp` (~500), `render_3d_grid.cpp` (~300) | ~1,000 |
| `codec.cpp` | 1,799 | Deprecate in favor of `codec_fb.cpp` | 0 (retired) |
| `inspector.cpp` | 1,350 | `inspector_series_browser.cpp` (~400), `inspector_stats.cpp` (~200) | ~750 |
| `imgui_integration.cpp` | 1,200 | `build_ui()` decomposition into 5 sub-methods | ~1,000 (methods stay in file) |
| `automation_server.cpp` | 1,200 | Dispatch table replaces 668-line if-chain | ~800 |
| `codec_fb.cpp` | 1,177 | Absorb migrated TLV message types | ~1,400 (grows) |
| `window_lifecycle.cpp` | 1,028 | Remove redundant `#ifdef` guards (17 blocks) | ~950 |
