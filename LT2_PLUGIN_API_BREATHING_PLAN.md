# LT-2: Expand Plugin API — Breathing Plan

> **Objective**: Expand Spectra's plugin API from command-only to cover data transforms, overlays, export formats, data sources, and custom series types — enabling domain-specific extensions without rebuilding Spectra.

---

## Assumptions

1. The existing `PluginManager` + C ABI in `src/ui/workspace/plugin_api.hpp/.cpp` is the foundation. We extend it, not replace it.
2. Plugin API version will bump from 1.0 → 2.0 when the first breaking context change lands.
3. All new extension points use the same shared-library plugin model (`spectra_plugin_init` entry point).
4. `TransformRegistry` already supports runtime custom transforms — Phase A is mostly C ABI wrappers.
5. The `Renderer` currently has hard-coded pipelines per series type. Custom series types (Phase D) require a pipeline registration mechanism, which depends on renderer decoupling (QW-2/QW-3/QW-4 from ARCHITECTURE_REVIEW.md). Phase D may be deferred if that prerequisite isn't met.
6. ROS2 and PX4 adapters are currently compile-time linked. Converting them to runtime plugins (Phase C2) is a stretch goal — the first step is defining the adapter interface.
7. We do NOT change the IPC protocol in this plan.
8. All phases are sequential (no parallel agents needed — each phase touches overlapping files).

---

## Constraints

- **ABI stability**: All plugin-facing APIs must be C ABI (`extern "C"`). No STL types cross the boundary.
- **Thread safety**: All registries accessed via plugin API must be mutex-protected (they already are).
- **No per-frame allocations**: Overlay callbacks must not allocate. Plugin authors document this; host enforces via debug-mode frame profiler.
- **Vulkan safety**: Custom series render callbacks must NOT create/destroy Vulkan resources per frame. Pipeline registration happens at load time only.
- **Version gating**: Every new `SpectraPluginContext` field must be gated by `api_version_minor` so older plugins still load.
- **No global state expansion**: New registries are owned by `PluginManager` or existing singletons (`TransformRegistry`), not new globals.

---

## Do Not Touch (This Plan)

- IPC protocol (codec, transport, messages)
- Daemon / multiproc architecture
- Core `Figure` / `Axes` / `Series` data model (read-only access for plugins)
- ImGui internals (plugins call through stable overlay API, not raw ImGui)
- Vulkan backend internals (plugins don't touch VkDevice, VkQueue, etc.)
- Python bindings (separate plan if plugin API needs Python exposure)

---

## Mission Board

### Phase A: Transform Plugins (Simplest — Foundation)

- [x] **A1** [impl] [risk:low] — C ABI wrappers for TransformRegistry
  - `depends_on`: none
  - **Scope**: `src/ui/workspace/plugin_api.hpp`, `src/ui/workspace/plugin_api.cpp`
  - **Acceptance criteria**:
    - Add `SpectraTransformRegistry` opaque handle to `SpectraPluginContext`
    - Add C ABI functions: `spectra_register_transform(registry, name, callback, user_data, description)`
    - Add C ABI function: `spectra_register_xy_transform(registry, name, callback, user_data, description)`
    - Transform callback signature: `float (*)(float value, void* user_data)` for scalar, `void (*)(const float* x_in, const float* y_in, size_t count, float* x_out, float* y_out, size_t* out_count, void* user_data)` for XY
    - Plugin-registered transforms appear in Transforms menu and `TransformRegistry::available_transforms()`
    - Bump `SPECTRA_PLUGIN_API_VERSION_MINOR` to 1

- [x] **A2** [test] [risk:low] — Transform plugin test + example plugin
  - `depends_on`: A1
  - **Scope**: `tests/unit/test_plugin_transforms.cpp`, `examples/plugins/` (new dir)
  - **Acceptance criteria**:
    - Unit test: load a mock `.so` that registers a custom transform, verify it appears in registry, verify `apply_y` produces correct output
    - Example plugin source: `examples/plugins/transform_smooth.cpp` — registers a moving-average smoothing transform
    - CMake target to build example plugin as a shared library
    - Test passes in CI

- [x] **A3** [impl] [risk:low] — Plugin context version negotiation
  - `depends_on`: A1
  - **Scope**: `src/ui/workspace/plugin_api.hpp`, `src/ui/workspace/plugin_api.cpp`
  - **Acceptance criteria**:
    - `PluginManager::load_plugin()` checks `info.api_version_minor` and only exposes context fields the plugin was built against
    - A v1.0 plugin (command-only) still loads successfully even though context now has transform registry pointer
    - Log warning if plugin requests unsupported minor version
    - Unit test: v1.0 plugin ignores new fields, v1.1 plugin uses transform registry

---

### Phase B: Overlay Plugins (ImGui Draw Callbacks)

- [x] **B1** [impl] [risk:med] — OverlayRegistry + C ABI
  - `depends_on`: A1
  - **Scope**: `src/ui/overlay/overlay_registry.hpp` (new), `src/ui/overlay/overlay_registry.cpp` (new), `src/ui/workspace/plugin_api.hpp`, `src/ui/workspace/plugin_api.cpp`
  - **Acceptance criteria**:
    - `OverlayRegistry` class: `register_overlay(name, draw_callback, user_data)`, `unregister_overlay(name)`, `draw_all(ctx)` where ctx provides: figure pointer (opaque), axes index, viewport rect, current mouse pos, is_hovered
    - C ABI: `spectra_register_overlay(registry, name, callback, user_data)` with callback signature `void (*)(const SpectraOverlayContext* ctx, void* user_data)`
    - `SpectraOverlayContext` struct: viewport x/y/w/h, mouse x/y, is_hovered, figure_id, axes_index, series_count
    - C ABI ImGui drawing helpers (so plugins don't link ImGui directly): `spectra_overlay_draw_line(ctx, x1, y1, x2, y2, color, thickness)`, `spectra_overlay_draw_rect(ctx, ...)`, `spectra_overlay_draw_text(ctx, x, y, text, color)`, `spectra_overlay_draw_circle(ctx, ...)`
    - Bump `SPECTRA_PLUGIN_API_VERSION_MINOR` to 2

- [x] **B2** [impl] [risk:med] — Wire OverlayRegistry into render loop
  - `depends_on`: B1
  - **Scope**: `src/ui/imgui/imgui_integration.cpp`, `src/ui/app/window_ui_context.hpp`
  - **Acceptance criteria**:
    - `WindowUIContext` owns an `OverlayRegistry*` (shared across windows, owned by `PluginManager`)
    - After each axes canvas is drawn in `ImGuiIntegration::build_ui()`, call `overlay_registry->draw_all(ctx)` for that axes viewport
    - Overlay draw calls translate to `ImDrawList` commands (lines, rects, text, circles)
    - No per-frame allocations in the draw path
    - Overlays render correctly in split-view (multiple panes)

- [x] **B3** [test] [risk:low] — Overlay plugin test + example
  - `depends_on`: B2
  - **Scope**: `tests/unit/test_plugin_overlays.cpp`, `examples/plugins/overlay_crosshair.cpp`
  - **Acceptance criteria**:
    - Unit test: register overlay callback, verify `draw_all` invokes it with correct viewport
    - Example plugin: custom crosshair overlay that draws coordinate readout at mouse position
    - Visual verification: overlay renders on top of plot area, disappears when plugin unloaded
    - No validation layer errors

---

### Phase C: Export Format Plugins + Data Source Interface

- [x] **C1** [impl] [risk:med] — ExportFormatRegistry + C ABI
  - `depends_on`: A1
  - **Scope**: `src/io/export_registry.hpp` (new), `src/io/export_registry.cpp` (new), `src/ui/workspace/plugin_api.hpp`, `src/ui/workspace/plugin_api.cpp`
  - **Acceptance criteria**:
    - `ExportFormatRegistry` class: `register_format(name, extension, export_callback, user_data)`, `available_formats()`, `export_figure(format_name, figure, path)`
    - Export callback receives: serialized figure data (JSON string describing axes/series/labels/limits), raw RGBA pixel buffer + width/height, output file path
    - C ABI: `spectra_register_export_format(registry, name, extension, callback, user_data)`
    - Plugin-registered formats appear in File → Export As submenu
    - Bump `SPECTRA_PLUGIN_API_VERSION_MINOR` to 3

- [x] **C2** [impl] [risk:med] — DataSourceAdapter interface definition
  - `depends_on`: A1
  - **Scope**: `src/adapters/adapter_interface.hpp` (new), `src/ui/workspace/plugin_api.hpp`
  - **Acceptance criteria**:
    - `DataSourceAdapter` abstract C++ interface: `name()`, `start()`, `stop()`, `is_running()`, `poll(frame)` → optional vector of `DataPoint{series_label, timestamp, value}`, `build_ui()` (optional ImGui panel)
    - C ABI: `SpectraDataSourceDesc` struct with function pointers for each method
    - `spectra_register_data_source(registry, desc)` — registers an adapter that appears in a "Data Sources" panel
    - Document the interface — actual ROS2/PX4 migration to this interface is a separate plan
    - No existing adapter code is modified in this mission

- [x] **C3** [test] [risk:low] — Export + data source plugin tests
  - `depends_on`: C1, C2
  - **Scope**: `tests/unit/test_plugin_export.cpp`, `tests/unit/test_plugin_data_source.cpp`, `examples/plugins/export_csv.cpp`
  - **Acceptance criteria**:
    - Unit test: register CSV export format, call export, verify output file contains expected data
    - Unit test: register mock data source, call `poll()`, verify data points returned
    - Example plugin: CSV exporter that writes series data as comma-separated values
    - Tests pass without GPU (headless-safe)

---

### Phase D: Custom Series Type Plugins (Hardest)

> **Prerequisites**: This phase depends on renderer decoupling progress (QW-2/QW-3/QW-4). If `record_commands()` still lives on `Series`, defer this phase.

- [x] **D1** [research] [risk:high] — Design custom series render interface
  - `depends_on`: C1 (and QW-2/QW-3 progress)
  - **Scope**: Design doc only — `docs/plans/CUSTOM_SERIES_DESIGN.md`
  - **Acceptance criteria**:
    - Document the minimal interface a custom series plugin must implement: `upload_data(backend_handle, data, count)`, `record_draw_commands(backend_handle, viewport, push_constants)`, `get_bounds() → Rect`
    - Document how custom pipelines are registered (SPIR-V blob + vertex layout + topology)
    - Document lifecycle: when upload happens, when draw happens, how dirty tracking works
    - Identify which `Backend` / `Renderer` methods must become public/stable API
    - Risk assessment: ABI stability of Vulkan pipeline creation params
    - Two design options compared (thin wrapper vs full abstraction)

- [ ] **D2** [impl] [risk:high] — SeriesTypeRegistry + pipeline registration
  - `depends_on`: D1
  - **Scope**: `src/render/series_type_registry.hpp` (new), `src/render/series_type_registry.cpp` (new), `src/render/renderer.hpp`, `src/render/renderer.cpp`, `src/ui/workspace/plugin_api.hpp`
  - **Acceptance criteria**:
    - `SeriesTypeRegistry` class: `register_series_type(name, create_fn, upload_fn, draw_fn, bounds_fn, pipeline_desc)`
    - `Renderer::init()` creates pipelines for all registered series types (built-in + plugin)
    - `Renderer::render_series()` dispatches to the correct draw function based on series type tag
    - C ABI: `spectra_register_series_type(registry, desc)` with `SpectraSeriesTypeDesc` struct
    - Built-in series types (Line, Scatter, Bar, etc.) are NOT refactored to use this registry in this mission — they keep their existing paths
    - Bump `SPECTRA_PLUGIN_API_VERSION_MAJOR` to 2 (breaking: context layout change)

- [x] **D3** [test] [risk:high] — Custom series type smoke test
  - `depends_on`: D2
  - **Scope**: `tests/unit/test_plugin_series_type.cpp`, `examples/plugins/series_heatmap.cpp`
  - **Acceptance criteria**:
    - Unit test: register a custom "dot grid" series type, create figure, add series, render one frame, verify no validation errors
    - Example plugin: heatmap series type with custom SPIR-V shaders
    - Resize torture test passes
    - No GPU hangs, no validation errors

---

### Phase E: Integration + Documentation

- [ ] **E1** [impl] [risk:low] — Plugin manager UI panel
  - `depends_on`: B2, C1
  - **Scope**: `src/ui/imgui/imgui_integration.cpp` (or new `plugin_panel.cpp`), `src/ui/app/register_commands.cpp`
  - **Acceptance criteria**:
    - View → Plugins menu item opens a panel listing all loaded plugins
    - Each plugin shows: name, version, author, description, enabled toggle, unload button
    - "Load Plugin..." button opens file dialog to select `.so`/`.dll`/`.dylib`
    - Plugin directory scan button (scans default dir + custom dirs)
    - Plugin state persisted in workspace save/load

- [x] **E2** [docs] [risk:low] — Plugin developer guide
  - `depends_on`: D2 (or C3 if Phase D is deferred)
  - **Scope**: `docs/plugin_developer_guide.md` (new)
  - **Acceptance criteria**:
    - Covers: plugin structure, entry point, API version negotiation, all extension point types
    - Includes complete example plugin source for each type (transform, overlay, export, data source, series type if done)
    - Build instructions (CMake snippet for building a plugin `.so`)
    - ABI rules: what can/cannot cross the boundary, lifetime rules, thread safety guarantees
    - Troubleshooting section: common errors, debug logging, version mismatch

- [x] **E3** [test] [risk:low] — End-to-end plugin integration test
  - `depends_on`: E1
  - **Scope**: `tests/unit/test_plugin_integration.cpp`
  - **Acceptance criteria**:
    - Test: load plugin that registers a transform + overlay + export format simultaneously
    - Verify all three appear in their respective registries
    - Unload plugin, verify all three are removed
    - Reload plugin, verify clean re-registration
    - No leaks (ASAN clean)

---

## Current Focus

**Phase A** — Transform plugin C ABI wrappers. This is the lowest-risk starting point and establishes the version negotiation pattern used by all subsequent phases.

---

## Pre-Flight Checklist

- [ ] Read `src/ui/workspace/plugin_api.hpp` — understand current C ABI surface
- [ ] Read `src/math/data_transform.hpp` — understand `TransformRegistry` API
- [ ] Read `src/render/renderer.hpp` — understand pipeline handle model
- [ ] Read `src/ui/imgui/imgui_integration.cpp` — understand where overlays would hook in
- [ ] Verify existing plugin tests pass: `ctest -R plugin`
- [ ] Verify `TransformRegistry::instance()` is accessible from `PluginManager` context

---

## Session Log

| Session | Date | Agent | Missions | Notes |
|---------|------|-------|----------|-------|
| — | — | — | — | Plan created. No implementation yet. |

---

## Decision Log

| ID | Decision | Rationale | Date |
|----|----------|-----------|------|
| — | — | — | — |

---

## Deferred Improvements

- **Python plugin API**: Expose the C ABI via Python ctypes so Python plugins can register transforms/overlays without compiling C/C++.
- **Hot-reload**: Watch plugin directory for changes and auto-reload modified `.so` files (requires careful Vulkan resource cleanup).
- **Plugin sandboxing**: Run plugin code in a separate process with IPC to prevent crashes from taking down the host.
- **Plugin marketplace / registry**: Online directory of community plugins with install-from-URL support.
- **Migrate ROS2 adapter to DataSourceAdapter**: Convert `RosAppShell` to load as a runtime plugin instead of compile-time linked library.
- **Migrate PX4 adapter to DataSourceAdapter**: Same for `Px4AppShell`.

---

## Known Risks / Open Questions

| Risk | Severity | Mitigation |
|------|----------|------------|
| ABI breakage between plugin API versions | High | Strict version gating in `load_plugin()`, never reorder existing struct fields, only append |
| Custom series pipelines may cause GPU hangs if shader is buggy | High | Validate SPIR-V at load time, timeout fence waits, debug-mode validation layer enforcement |
| Plugin draw callbacks blocking the frame loop | Med | Document no-allocation rule, add frame profiler warning if overlay callback > 1ms |
| `TransformRegistry` is a singleton — thread-safe but rigid | Low | Acceptable for now; can wrap in non-singleton if needed later |
| Phase D depends on renderer decoupling (QW-2/3/4) not yet complete | High | Phase D is explicitly gated; Phases A-C deliver value independently |
| ImGui version coupling for overlay plugins | Med | Plugins use stable C ABI drawing helpers, never raw ImGui — isolates from ImGui version changes |
| Cross-platform shared library loading edge cases | Low | Already handled in existing `plugin_api.cpp` (dlopen/LoadLibrary); test on CI for all platforms |

---

## Definition of Done

LT-2 is complete when:

1. A shared-library plugin can register custom transforms, overlays, export formats, and data sources via C ABI
2. All registered extensions appear in the appropriate UI menus/panels
3. Plugin load/unload is clean (no leaks, no dangling callbacks, no validation errors)
4. Version negotiation works (v1.0 plugins load in v2.0 host; v2.0 plugins gracefully fail in v1.0 host)
5. At least one example plugin per extension type compiles and runs
6. Plugin developer guide exists with build instructions and complete examples
7. All tests pass (unit + integration), ASAN clean, no GPU validation errors
8. Custom series type plugins work if renderer decoupling prerequisite is met (otherwise documented as deferred)
