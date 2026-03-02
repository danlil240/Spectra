
## 0) Living Plan Protocol (Required Every Session)

This is a breathing file. Every agent session must update it before ending.

Rules:
- Never remove prior session entries; append new updates at the top of the session log.
- Keep entries factual and short (what was done, what remains, exact next steps).
- If no code changed, still add a session entry with findings/decisions.
- Include blockers explicitly so the next session can continue immediately.

Required update checklist per session:
- `Done this session`: concrete completed items.
- `Files touched`: exact paths (or `none` for planning-only sessions).
- `Validation run`: tests/checks executed, or `not run`.
- `Open issues`: unresolved risks/blockers.
- `Next session`: numbered first actions.

Session entry template:

```md
### Session YYYY-MM-DD HH:MM (Agent: <name>)
Done this session:
- ...

Files touched:
- ...

Validation run:
- ...

Open issues:
- ...

Next session:
1. ...
2. ...
```

## Session Log

### Session 2026-03-02 10:13 (Agent: Codex)
Done this session:
- Added a dedicated Qt multi-canvas binary target: `qt_multi_canvas_demo` (built from `qt_embed_demo.cpp` with compile-time default multi mode).
- Added compile-time default mode wiring in `qt_embed_demo.cpp`:
  - `SPECTRA_QT_FORCE_MULTI_CANVAS` sets default startup mode to two canvases.
  - Added `--single` / `-s` override alongside existing `--multi` / `-m`.
- Added a multi-canvas lifecycle test hook in demo runtime:
  - `Ctrl+D` toggles detach/reattach for the right canvas (`QtRuntime::detach_window` / `attach_window`) without restarting the app.
- Added explicit Phase 2 multi-canvas smoke checklist section (§20), including independent interaction and detach/reattach validation steps.

Files touched:
- `examples/qt_embed_demo.cpp`
- `examples/CMakeLists.txt`
- `plans/QT_embed_plan.md`

Validation run:
- `cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON -DSPECTRA_BUILD_EXAMPLES=ON` (pass)
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo qt_multi_canvas_demo -j6` (pass)
- `cmake --build build --target spectra -j6` (pass)

Open issues:
- Runtime smoke validation on a real display server is still pending for both `qt_embed_demo --multi` and `qt_multi_canvas_demo`.
- Validation-layer verification is still needed for the new `Ctrl+D` detach/reattach flow (no GUI runtime available in current environment).

Next session:
1. Run `./build-qt/examples/qt_multi_canvas_demo` with validation layers and execute §20 end-to-end.
2. Record checklist results (especially independent interaction + `Ctrl+D` detach/reattach behavior) in this session log.
3. If any flicker/crosstalk appears during detach/reattach, isolate shared renderer resource hazards and patch `QtRuntime::begin_frame`/window fencing as needed.

### Session 2026-03-02 00:08 (Agent: Codex)
Done this session:
- Added minimal multi-canvas exercise path to `qt_embed_demo` to validate the new window-addressable `QtRuntime` API.
- Added `--multi` / `-m` runtime flag:
  - default mode remains single-canvas behavior.
  - multi mode creates two `SpectraVulkanWindow` canvases in a horizontal `QSplitter`, both sharing the same `QtRuntime`.
- Refactored demo setup with small helpers:
  - `populate_demo_figure(...)` to generate demo data (phase-shifted per canvas in multi mode).
  - `setup_input_handler(...)` to bind pan/zoom/select per figure.
- Updated startup/help text in the non-Qt fallback main to mention `--multi`.

Files touched:
- `examples/qt_embed_demo.cpp`
- `plans/QT_embed_plan.md`

Validation run:
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo -j6` (pass)
- `cmake --build build --target spectra -j6` (pass)
- `ctest --test-dir build --output-on-failure` (83/85 pass; failing suites remain `golden_image_tests` and `golden_image_tests_3d`)

Open issues:
- Runtime smoke validation on an actual display server is still pending for both single and `--multi` modes (including detach/reattach, hide/show, and DPR monitor moves).
- Multi-canvas demo currently lives behind `--multi`; no dedicated separate example target yet.

Next session:
1. Run `./build-qt/examples/qt_embed_demo --multi` with validation layers and execute Phase 2-focused manual checks.
2. Add explicit multi-canvas smoke checklist items to this plan (attach/detach one canvas, independent interaction, resize torture with two canvases).
3. Decide whether to split multi-canvas into a dedicated example binary.

### Session 2026-03-02 00:05 (Agent: Codex)
Done this session:
- Started Phase 2 runtime API expansion in `QtRuntime`:
  - Added per-window APIs: `has_window`, `detach_window`, `resize(window,...)`, `mark_swapchain_dirty(window)`, `begin_frame(window)`, `render_figure(window, ...)`, `end_frame(window)`, and `window_context(window)`.
  - Kept compatibility wrappers (`begin_frame()`, `render_figure(fig)`, etc.) by routing to a tracked primary window.
  - Reworked internal state from single `window_ctx_` to per-window state map with per-window resize debounce and instrumentation counters.
  - Updated shutdown to destroy all attached window contexts safely.
- Added Qt platform surface lifecycle wiring in `qt_embed_demo`:
  - Handles `QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed` by calling `QtRuntime::detach_window(this)`.
  - Handles `SurfaceCreated` by re-attaching when exposed and size is valid.
  - Updated render and resize paths to call the new window-specific runtime APIs.

Files touched:
- `src/adapters/qt/qt_runtime.hpp`
- `src/adapters/qt/qt_runtime.cpp`
- `examples/qt_embed_demo.cpp`
- `plans/QT_embed_plan.md`

Validation run:
- `cmake --build build --target spectra -j6` (pass)
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo -j6` (pass)
- `ctest --test-dir build --output-on-failure` (83/85 pass; failing suites remain `golden_image_tests` and `golden_image_tests_3d`)

Open issues:
- Runtime validation on an actual display server is still pending for the new detach/reattach path.
- `QtRuntime` is now window-addressable but still renderer-shared and primary-window-centric for compatibility wrappers; no dedicated multi-canvas example exists yet.

Next session:
1. Run `qt_embed_demo` with validation layers and explicitly test hide/show, minimize/restore, and platform-surface detach/reattach behavior.
2. Add a minimal two-canvas Qt example (or mode in `qt_embed_demo`) to exercise `begin_frame(window)` across multiple windows.
3. Decide whether to remove or keep primary-window compatibility wrappers once multi-canvas call sites are migrated.

### Session 2026-03-02 00:01 (Agent: Codex)
Done this session:
- Added `SurfaceHost` lifecycle hooks and adapter-owned surface destruction seam:
  - New optional hooks: `on_surface_created`, `on_surface_about_to_destroy`.
  - New `destroy_surface(VkInstance, VkSurfaceKHR)` virtual with Vulkan default behavior.
- Implemented Qt-specific surface destruction override (`QtSurfaceHost::destroy_surface`) as a no-op so Qt-owned `VkSurfaceKHR` is not double-destroyed.
- Updated `VulkanBackend` cleanup paths to destroy window surfaces through the `SurfaceHost` seam in both:
  - global backend shutdown
  - per-window `destroy_window_context`.
- Added backend helper `destroy_surface_for(WindowContext&)` and wired surface lifecycle callbacks on creation.
- Hardened `init_window_context()` failure handling to release a partially-created surface on exceptions.
- Removed Qt runtime shutdown hack that manually nulled `window_ctx_->surface`; cleanup now relies on adapter-owned surface semantics.
- Fixed potential dangling pointer risk in `QtRuntime::shutdown()` by clearing backend active window after window context destruction.
- Added attach rebind path in `QtRuntime::attach_window()` to destroy any prior context before attaching a new `QWindow`.

Files touched:
- `src/platform/window_system/surface_host.hpp`
- `src/adapters/qt/qt_surface_host.hpp`
- `src/adapters/qt/qt_surface_host.cpp`
- `src/render/vulkan/vk_backend.hpp`
- `src/render/vulkan/vk_backend.cpp`
- `src/adapters/qt/qt_runtime.cpp`
- `plans/QT_embed_plan.md`

Validation run:
- `cmake --build build --target spectra -j6` (pass)
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo -j6` (pass)
- `ctest --test-dir build --output-on-failure` (83/85 pass; 2 failing suites remain `golden_image_tests` and `golden_image_tests_3d`, consistent with pre-existing golden diffs)

Open issues:
- Qt runtime/manual smoke validation on a display server is still pending (cannot be verified from CLI-only environment).
- Phase 2 multi-canvas runtime APIs are not started yet (current `QtRuntime` remains single-active-window oriented despite safer rebind support).

Next session:
1. Run `qt_embed_demo` under a display server with validation layers and execute checklist §19.
2. Start Phase 2 runtime API expansion in `QtRuntime` (`attach_window`/`detach_window`/`begin_frame(window)` semantics for multi-canvas).
3. Add Qt surface lifecycle event wiring (`QPlatformSurfaceEvent`) for detach/reattach generation handling.

### Session 2026-03-02 00:00 (Agent: Cascade)
Done this session:
- Added visibility guard to `SpectraVulkanWindow::renderFrame()`: skips rendering when `!isExposed()` or pixel size is zero. Prevents swapchain storms and wasted GPU work on hidden/minimized windows.
- Changed `resizeEvent()` to set a dirty flag via `QtRuntime::mark_swapchain_dirty()` instead of immediately calling `resize()`. Actual swapchain recreation is deferred to `begin_frame()` at the next frame boundary.
- Added 50ms resize debounce in `QtRuntime::begin_frame()`: coalesces rapid resize events into a single swapchain recreation. `OUT_OF_DATE` from present bypasses debounce (must recreate immediately).
- Added resize/swapchain instrumentation logging (DEBUG level) in `QtRuntime`: logs debounce skips, swapchain recreation count, surface-not-ready skips. All behind `SPECTRA_LOG_DEBUG` so zero overhead at default log level.
- Enhanced `exposeEvent()` to handle hide→show transitions: on re-expose, immediately renders a frame so the canvas isn't blank after restore-from-minimize.
- Added DPR change detection in `exposeEvent()`: tracks `last_dpr_` and marks swapchain dirty when DPR changes (moving window between monitors with different scaling).
- Added smoke test checklist for Qt single-canvas validation (Section 19 of this plan).
- Updated P1-08 and P1-09 backlog items to done. All Phase 1 backlog items are now complete.

Files touched:
- `examples/qt_embed_demo.cpp` (visibility guard, debounced resize)
- `src/adapters/qt/qt_runtime.hpp` (mark_swapchain_dirty, debounce members, counters)
- `src/adapters/qt/qt_runtime.cpp` (mark_swapchain_dirty impl, debounced begin_frame, instrumentation)
- `plans/QT_embed_plan.md` (session log, backlog updates, smoke test checklist)

Validation run:
- `cmake --build build --target spectra -j6` (pass — no recompilation, Qt files not in standard build)
- `ctest` in standard build (83/85 pass, 2 pre-existing golden image failures)
- `cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON -DSPECTRA_BUILD_EXAMPLES=ON` (pass)
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo -j6` (pass)

Open issues:
- Runtime validation still pending: need to run `qt_embed_demo` on a display server to confirm all changes work correctly.
- Phase 2 planning not yet started.

Next session:
1. Run `qt_embed_demo` on display server with validation layers enabled. Execute smoke test checklist (Section 19).
2. Begin Phase 2 planning: multi-canvas, DPR monitor change, dock detach/reattach.
3. Consider adding `exposeEvent` handling for hide→show transitions (re-trigger frame).

### Session 2026-03-02 (Agent: Cascade)
Done this session:
- Expanded `QtRuntime` from lightweight VkInstance bridge to full Vulkan bootstrap runtime:
  - Owns `VulkanBackend` + `Renderer` + `WindowContext` lifecycle.
  - `init()` creates VkInstance/VkDevice/Renderer, adopts instance into `QVulkanInstance`.
  - `attach_window()` creates VkSurfaceKHR + swapchain + sync objects for a QWindow.
  - `resize()` recreates swapchain.
  - `begin_frame()` / `render_figure()` / `end_frame()` drive the per-frame render loop.
  - `shutdown()` performs clean Vulkan teardown in correct order.
- Migrated `qt_embed_demo.cpp` from CPU-blit `EmbedSurface` path to real Vulkan canvas:
  - New `SpectraVulkanWindow` (QWindow subclass, VulkanSurface type) drives render loop.
  - DPR-aware input forwarding via `InputHandler` (double-precision coordinates).
  - Resize handled via swapchain recreation through `SurfaceHost`.
  - Embedded in `QMainWindow` via `QWidget::createWindowContainer`.
  - Timer-driven 60 FPS frame loop.
- Added `Figure::set_size(uint32_t, uint32_t)` public API to avoid friend-access coupling.
- Added `VulkanBackend::create_window_context()` factory method to avoid incomplete-type issues with `WindowUIContext` in adapter translation units.
- Verified all framebuffer size queries in active render paths route through `SurfaceHost` (`glfwGetFramebufferSize` only remains in `glfw_adapter.cpp`, which is correct).
- Added `src/` and `generated/` include directories to `qt_embed_demo` CMake target.

Files touched:
- `src/adapters/qt/qt_runtime.hpp` (expanded API)
- `src/adapters/qt/qt_runtime.cpp` (full implementation)
- `include/spectra/figure.hpp` (added `set_size()`)
- `src/render/vulkan/vk_backend.hpp` (added `create_window_context()` factory)
- `src/render/vulkan/vk_backend.cpp` (implemented factory)
- `examples/qt_embed_demo.cpp` (rewritten for Vulkan canvas)
- `examples/CMakeLists.txt` (added include dirs for Qt demo)

Validation run:
- `cmake --build build --target spectra -j6` (pass — standard non-Qt build)
- `ctest` in standard build (83/85 pass, 2 pre-existing golden image failures)
- `cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON -DSPECTRA_BUILD_EXAMPLES=ON` (pass)
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo -j6` (pass)

Open issues:
- Runtime validation pending: need to run `qt_embed_demo` on a display server to confirm Vulkan canvas renders correctly (resize torture, input interaction, clean shutdown).
- `QVulkanInstance` ownership: currently `QtRuntime` always creates its own; injected-instance path (`adopt_vulkan_instance` before `init`) is coded but untested.
- No ImGui overlay integration in Qt path yet (Phase 2/3 scope).

Next session:
1. Run `qt_embed_demo` on display server and validate: canvas renders, resize for 2 min, pan/zoom/select, clean shutdown with validation layers.
2. Add visibility guard to frame loop (skip rendering when `!isExposed()` or minimized).
3. Add swapchain recreation debounce (dirty flag + frame-boundary only).
4. Begin Phase 2 planning: multi-canvas, DPR monitor change, dock detach/reattach.

### Session 2026-03-01 23:23 (Agent: Codex)
Done this session:
- Implemented Qt Vulkan surface host implementation (`QtSurfaceHost`) with:
  - Vulkan instance extension discovery/filtering for common Qt platform backends.
  - `QWindow` -> `VkSurfaceKHR` creation via `QVulkanInstance::surfaceForWindow()`.
  - DPR-aware framebuffer size query for adapter-neutral swapchain sizing.
- Added `spectra_qt_adapter` CMake target behind `SPECTRA_USE_QT` and wired Qt adapter sources into it.
- Updated Qt example build gating to `SPECTRA_BUILD_QT_EXAMPLE` + `SPECTRA_USE_QT` and linked `qt_embed_demo` through `spectra_qt_adapter`.
- Updated Qt demo build instructions to reflect new CMake options.

Files touched:
- /home/daniel/projects/Spectra/src/adapters/qt/qt_surface_host.cpp
- /home/daniel/projects/Spectra/CMakeLists.txt
- /home/daniel/projects/Spectra/examples/CMakeLists.txt
- /home/daniel/projects/Spectra/examples/qt_embed_demo.cpp
- /home/daniel/projects/Spectra/plans/QT_embed_plan.md

Validation run:
- `cmake --build build --target spectra -j6` (pass)
- `cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON -DSPECTRA_BUILD_EXAMPLES=ON` (pass)
- `cmake --build build-qt --target spectra_qt_adapter qt_embed_demo -j6` (pass)

Open issues:
- `qt_embed_demo` still exercises CPU blit `EmbedSurface` path; it is not yet migrated to the Qt Vulkan adapter runtime (`qt_runtime` + `QtSurfaceHost`) for a first-class Vulkan canvas bootstrap.
- Qt runtime ownership policy for `QVulkanInstance` (fully owned vs injected/shared from host app lifecycle) remains unresolved in plan notes.

Next session:
1. Migrate `examples/qt_embed_demo.cpp` to a real Vulkan Qt canvas path using `QtSurfaceHost` + backend surface creation.
2. Wire `qt_runtime` into the example/bootstrap path so `QWindow` Vulkan instance binding is explicit and validated at runtime.
3. Replace/confirm any remaining active-path framebuffer size queries that bypass adapter-neutral `SurfaceHost`.

### Session 2026-03-01 22:05 (Agent: Codex)
Done this session:
- Continued the plan with an execution backlog for Phase 1 (task IDs, owners/files, dependencies, done criteria).
- Added build/run command checklist and explicit go/no-go decision gates for merge readiness.
- Added unresolved design questions and fallback strategy to reduce next-session ambiguity.

Files touched:
- /home/daniel/projects/Spectra/plans/QT_embed_plan.md

Validation run:
- not run (documentation update only)

Open issues:
- Need confirmation whether Qt adapter should own `QVulkanInstance` lifecycle fully or accept injected/shared instance from host app.
- Need to confirm expected behavior when canvas is hidden/minimized for long periods (render loop suspension policy).

Next session:
1. Implement `SurfaceHost` header/source and compile all current Vulkan call sites against it.
2. Create `src/adapters/qt/qt_surface_host.*` with minimal single-canvas path.
3. Add Qt CMake toggles/targets and perform dual build matrix (`Qt OFF` and `Qt ON + example`).

### Session 2026-03-01 22:02 (Agent: Codex)
Done this session:
- Continued this plan with execution-level detail for Phase 1 implementation.
- Added concrete file-level work items, interface header draft, CMake target plan, and first-slice task order.
- Recorded explicit Definition-of-Done checkpoints for each remaining Phase 1 task.

Files touched:
- /home/daniel/projects/Spectra/plans/QT_embed_plan.md

Validation run:
- not run (documentation update only)

Open issues:
- Qt adapter runtime bootstrap order (Qt instance/device/surface ownership boundaries) must still be validated against current Vulkan init assumptions.
- ImGui backend decoupling scope may expand if hidden GLFW transitive calls are found outside current inventory.

Next session:
1. Add `surface_host.hpp` + `surface_host.cpp` interface scaffolding and wire it through current Vulkan paths.
2. Implement Qt `SurfaceHost` minimal path (`required_instance_extensions`, `create_surface`, `framebuffer_size`) and a single-canvas bootstrap example target.
3. Replace remaining direct framebuffer-size GLFW queries with the `SurfaceHost` call path and run a full build.

### Session 2026-03-01 21:55 (Agent: Codex)
Done this session:
- Started Phase 1 implementation by wiring the `SurfaceHost` seam into Vulkan instance/surface paths.
- Updated Vulkan instance creation to consume adapter-provided instance extensions (`SurfaceHost`) instead of direct GLFW calls in `vk_device`.
- Updated Vulkan surface creation (`create_surface`, `init_window_context`) to use configured `SurfaceHost` and fail cleanly when missing.
- Added `native_window` to `WindowContext` and populated it from GLFW paths while keeping legacy `glfw_window` for ImGui/GLFW code.
- Set default `VulkanBackend` surface host to `GlfwSurfaceHost` under `SPECTRA_USE_GLFW`.

Files touched:
- /home/daniel/projects/Spectra/src/render/vulkan/window_context.hpp
- /home/daniel/projects/Spectra/src/render/vulkan/vk_device.hpp
- /home/daniel/projects/Spectra/src/render/vulkan/vk_device.cpp
- /home/daniel/projects/Spectra/src/render/vulkan/vk_backend.hpp
- /home/daniel/projects/Spectra/src/render/vulkan/vk_backend.cpp
- /home/daniel/projects/Spectra/src/platform/window_system/glfw_surface_host.cpp
- /home/daniel/projects/Spectra/src/ui/window/window_manager.cpp

Validation run:
- `cmake --build build --target spectra -j6` (pass)

Open issues:
- GLFW-specific types/callbacks still dominate window/input/ImGui paths; only Vulkan surface/extension seam is adapter-neutral so far.
- No Qt adapter implementation yet (`spectra_qt_adapter` not started).

Next session:
1. Introduce Qt `SurfaceHost` implementation (QWindow/QVulkanInstance path) and adapter bootstrap target.
2. Replace direct framebuffer-size GLFW queries in runtime paths with `SurfaceHost::framebuffer_size`.
3. Draft CI/CMake toggle for a Qt bootstrap example build (`SPECTRA_BUILD_QT_EXAMPLE` path).

### Session 2026-03-01 (Agent: Codex)
Done this session:
- Completed architecture plan for first-class Qt6 embedding (planning-only).
- Chose Qt surface/event-loop/backend abstraction directions (A/B/C decisions).
- Documented phased milestones, tests, and risks.
- Added this living-plan protocol and mandatory handoff template.

Files touched:
- /home/daniel/projects/Spectra/plans/QT_embed_plan.md

Validation run:
- not run (documentation update only)

Open issues:
- No implementation has started yet; milestones are not yet converted into tasks/issues.

Next session:
1. Convert Phase 1 scope into concrete task tickets (module-by-module).
2. Define exact `WindowSystem/SurfaceHost` C++ interface header draft.
3. Confirm Qt adapter bootstrap example target and CI build toggle.

**A/B/C Decisions**
A) Qt surface type: use `QWindow` (`QSurface::VulkanSurface`) + `QWidget::createWindowContainer`, with `QVulkanInstance::surfaceForWindow()` for `VkSurfaceKHR`.
- Why: keeps Spectra’s Vulkan swapchain ownership, avoids platform-specific `winId` surface code, supports embedding in docks/tabs/layouts, and avoids `QVulkanWindow` taking over render lifecycle.

B) Event loop: hybrid where Qt owns the event loop, Spectra owns frame policy.
- Qt drives wakeups via `requestUpdate` + single-shot timers.
- Spectra `FrameScheduler` decides whether to render now, delay, or idle.
- VSync: present throttles frame pace.
- Fixed timestep: scheduler accumulates simulation ticks, render once per update.

C) Backend abstraction: introduce a stable `WindowSystem/SurfaceHost` interface at render boundary.
- Core/render use opaque platform handles and Spectra event types only.
- `spectra_glfw_adapter` and `spectra_qt_adapter` implement the interface.
- No Qt/GLFW types in `libspectra_core` or `libspectra_render` public headers.

## 1) Current-State Analysis (from code inspection)

GLFW assumptions and coupling points:
- Vulkan instance extension discovery is GLFW-bound in [vk_device.cpp](/home/daniel/projects/Spectra/src/render/vulkan/vk_device.cpp).
- Surface creation is GLFW-only in [vk_backend.cpp](/home/daniel/projects/Spectra/src/render/vulkan/vk_backend.cpp).
- Window context stores `glfw_window` directly in [window_context.hpp](/home/daniel/projects/Spectra/src/render/vulkan/window_context.hpp).
- Input callbacks are GLFW-centric in [window_manager.cpp](/home/daniel/projects/Spectra/src/ui/window/window_manager.cpp).
- ImGui platform backend is GLFW-specific in [imgui_integration.cpp](/home/daniel/projects/Spectra/src/ui/imgui/imgui_integration.cpp).
- Clipboard command path uses GLFW in [register_commands.cpp](/home/daniel/projects/Spectra/src/ui/app/register_commands.cpp).

What already works for Qt:
- Basic Qt embedding exists as CPU/blit path in [qt_embed_demo.cpp](/home/daniel/projects/Spectra/examples/qt_embed_demo.cpp).
- Python Qt backend has a Matplotlib-like API pattern in [backend_qtagg.py](/home/daniel/projects/Spectra/python/spectra/backends/backend_qtagg.py), including DPR handling/timer-driven redraw.

What is missing for first-class Qt6 embedding:
- No native Vulkan Qt adapter module.
- `embed_surface` Vulkan/image path is incomplete (`render_to_image` TODO) in [embed_surface.cpp](/home/daniel/projects/Spectra/src/embed/embed_surface.cpp).
- Some embed API stubs are placeholders in [spectra_embed_c.cpp](/home/daniel/projects/Spectra/src/embed/spectra_embed_c.cpp).
- Build graph is still effectively monolithic in [CMakeLists.txt](/home/daniel/projects/Spectra/CMakeLists.txt).
- ImGui enablement is coupled to GLFW (`SPECTRA_USE_IMGUI AND SPECTRA_USE_GLFW`).

Architectural blockers to modularity:
- GLFW type assumptions in render-layer structs.
- Input/key/mod enums implicitly GLFW-shaped.
- ImGui platform backend hardcoded to GLFW.
- No platform abstraction seam for surface lifecycle, clipboard, cursors, timing.

## 2) Target Architecture (modular layout)

```text
apps/examples
  |-- spectra_glfw_adapter  ----\
  |-- spectra_qt_adapter    -----+--> libspectra_render --> libspectra_core
  |-- headless/export path  ----/          |
  |                                         +--> libspectra_ui_imgui (optional overlay pass)
```

Dependency rules:
- `libspectra_core`: no Qt/GLFW/Vulkan includes.
- `libspectra_render`: Vulkan + core only; no Qt/GLFW includes.
- `libspectra_ui_imgui`: depends on core/render + ImGui; no direct Qt/GLFW backend calls.
- `spectra_glfw_adapter`: depends on core/render/ui_imgui(optional) + GLFW.
- `spectra_qt_adapter`: depends on core/render/ui_imgui(optional) + Qt6.
- Headless/export must link core/render only.

Minimal must-refactor items:
- Replace `glfw_window` field with opaque surface host handle.
- Move Vulkan extension/surface hooks behind adapter interface.
- Split ImGui platform integration from GLFW-specific backend.
- Normalize input key/mod/pointer types to Spectra enums (adapter converts).
- Remove `ImGui<->GLFW` build-time coupling.

## 3) Public Qt API Proposal

Classes:
- `SpectraCanvas : QWidget`
- `SpectraController : QObject`
- `SpectraNavigationToolbar : QToolBar`

Minimal API surface:
- `setFigure(SpectraFigureHandle)`
- `setTheme(const SpectraTheme&)`
- `setInteractionMode(SpectraInteractionMode)`
- `setUiMode(SpectraUiMode)` where `ImGuiOverlay | QtChromeOnly`
- `saveImage(...)`, `saveSvg(...)`, `savePdf(...)` (route to existing export path)

Signals/slots:
- Signals: `cursorMoved(x, y)`, `selectionChanged(...)`, `viewChanged(...)`, `frameRendered(ms)`, `deviceLost(reason)`.
- Slots/commands (controller): `home()`, `pan()`, `zoom()`, `select()`, `back()`, `forward()`, `setAutoScale(bool)`.

Mode support (minimal branching):
- Render pipeline always runs scene pass.
- Overlay pass is plugin-like and optional (`libspectra_ui_imgui` on/off).
- Qt toolbar/menu actions call same controller commands in both modes.

## 4) Rendering Integration Plan

Ownership model:
- One shared `VkInstance + VkPhysicalDevice + VkDevice` per process (per adapter runtime).
- Per-canvas: `VkSurfaceKHR`, swapchain, frame sync objects, command buffers.
- No primary-window special case; each canvas is symmetric.

Surface/swapchain lifecycle rules:
- `resize`, `show/hide`, DPR change, reparent events only set dirty flags.
- Recreate swapchain only at frame boundary and only when `visible && extent_px > 0`.
- Debounce recreation to prevent storms during rapid resize.
- On `VK_ERROR_OUT_OF_DATE_KHR`/`SUBOPTIMAL_KHR`: mark dirty, retry once next frame.
- On detach/reattach, detect surface generation change (`SurfaceAboutToBeDestroyed/SurfaceCreated`) and rebuild `VkSurfaceKHR` + swapchain.

Device lost handling:
- Detect `VK_ERROR_DEVICE_LOST`, publish adapter-level `deviceLost`.
- Tear down per-canvas swapchain objects first, then shared device/runtime.
- Attempt controlled runtime reinit; if fail, keep app alive and surface error to UI.

## 5) Input Mapping Plan

Qt-to-Spectra mapping:
- `QMouseEvent` -> `PointerEvent` (press/release/move, buttons/mods, logical coords + DPR).
- `QWheelEvent` -> normalized zoom/scroll delta (`angleDelta` primary, `pixelDelta` fallback).
- `QKeyEvent` -> `KeyEvent` via Spectra key enum (no GLFW codes in core).
- `QEnterEvent/QEvent::Leave` -> hover state.
- `QDragEnter/Move/Drop` -> drag interactions.

Focus and shortcuts:
- `SpectraCanvas` tracks focus in/out and becomes active command target.
- `SpectraController` registers `QAction/QShortcut` with focused-canvas routing.
- Shortcuts execute only for active canvas (no global leakage across canvases).

Text/IME:
- Enable input methods on canvas.
- Handle `QInputMethodEvent` preedit/commit for annotations/text tools.
- Pass committed UTF-8 text to Spectra text input path; keep preedit candidate state for inline composition.

## 6) Threading Model

Chosen model (v1): single GUI thread owns Qt objects and Vulkan submission.
- Rationale: lowest risk with Qt object affinity, simplest lifecycle, fewer deadlocks.
- Keep command queue abstraction internal so render-thread migration stays possible later.

Command flow:
- Qt events -> `SpectraController` -> core commands -> render invalidate.
- Render executes on `UpdateRequest`/timer tick on GUI thread.
- Background worker threads remain for non-GPU tasks (I/O, data prep).

Deadlock avoidance:
- No `wait_idle` in Qt event handlers.
- Resource destruction deferred to safe frame points.
- No blocking queued connections between canvas callbacks and render path.

## 7) Milestones (3 phases)

| Phase | Scope | Files/Modules Touched | Acceptance Criteria | Verification Steps |
|---|---|---|---|---|
| Phase 1 | Introduce platform surface abstraction and deliver stable `SpectraCanvas` (single canvas, Vulkan render, resize, core input) | `src/render/vulkan/*`, new `src/platform/window_system/*`, new `src/adapters/qt/*`, `src/adapters/glfw/*` shim updates, CMake target wiring | Canvas embeds in `QMainWindow`/dock/tab; pan/zoom/select work; resize does not flicker/storm; headless/export unchanged | Run Qt example with one canvas; interact + resize continuously; run existing headless export tests |
| Phase 2 | Multi-canvas, docking detach/reattach, high-DPI/per-monitor correctness, OUT_OF_DATE storm suppression | `spectra_qt_adapter` lifecycle code, swapchain manager in render, DPR/input coordinate conversion | 3+ canvases independent; move between docks/tabs/monitors without teardown artifacts; no recreate storms | Multi-canvas demo scenario; monitor move at mixed scaling; resize torture loop |
| Phase 3 | Qt-native controller/toolbar, ImGui overlay optionality, polish and docs | new `SpectraController`, `SpectraNavigationToolbar`, `libspectra_ui_imgui` gating, docs/examples | `ImGuiOverlay` and `QtChromeOnly` both functional with same canvas API; shortcuts focus-gated; export hooks from toolbar/actions | Validate both UI modes in one example app; run cross-platform smoke + validation layers |

## 8) Test Strategy

Manual verification matrix:
- Linux X11 (NVIDIA/AMD), Linux Wayland (KWin/GNOME), Windows 11.
- macOS optional smoke if MoltenVK path exists.
- Single monitor and dual monitor mixed DPI (100%/150%/200%).

Resize torture:
- Programmatic rapid resize (small<->large) for 60s.
- Pass criteria: no hang, no unbounded swapchain recreates, stable interaction latency.

Multi-canvas:
- 1/2/4 canvas layouts in docks/tabs/splitters.
- Reparent/detach/reattach cycles; close/reopen canvas repeatedly.
- Independent camera/selection state per canvas.

Validation layers checklist:
- `VK_LAYER_KHRONOS_validation` enabled.
- Check for sync errors, image layout errors, surface/swapchain lifetime errors.
- Confirm clean shutdown after canvas close/reopen loops.

Headless regression:
- Golden image/export tests run unchanged without Qt libs.
- CI matrix keeps headless path as baseline gate.

## 9) Risks + Mitigations

- Qt surface recreation quirks on reparent.
  - Mitigation: explicit surface generation tracking and recreate only on generation change.
- `OUT_OF_DATE/SUBOPTIMAL` storms during resize/minimize.
  - Mitigation: dirty-flag + frame-boundary debounce + visibility/extent guards.
- DPI drift and coordinate mismatch.
  - Mitigation: unified logical/pixel conversion utility and DPR change event handling.
- Multi-window resource leakage/crosstalk.
  - Mitigation: strict per-canvas swapchain/frame-resource ownership with shared immutable device resources.
- ImGui in embedded context (focus/input conflicts).
  - Mitigation: backend-neutral ImGui bridge and explicit focus arbitration with `SpectraController`.

This plan keeps GLFW intact, adds Qt as a first-class adapter, and limits mandatory refactors to the platform seam and input/backend decoupling.

## 10) Phase 1 Detailed Execution (remaining work)

Status note:
- Vulkan seam bootstrap has started (instance/surface hooks + `native_window` path). Items below focus on finishing a shippable Phase 1 slice.

### 10.1 SurfaceHost contract completion

Tasks:
- Introduce canonical interface header for adapter-facing surface operations:
  - `required_instance_extensions()`
  - `create_surface(VkInstance, NativeWindowHandle)`
  - `framebuffer_size(NativeWindowHandle)`
  - lifecycle hooks (`on_surface_about_to_destroy`, `on_surface_created`) as no-op defaults if needed.
- Ensure all Vulkan code paths that currently query GLFW extensions/size/surface route through this interface.

Primary files:
- `src/platform/window_system/surface_host.hpp` (new)
- `src/platform/window_system/surface_host.cpp` (new, optional)
- `src/render/vulkan/vk_device.cpp`
- `src/render/vulkan/vk_backend.cpp`

Definition of done:
- No direct GLFW extension/surface/size calls remain in Vulkan backend core paths.
- Both GLFW adapter and Qt adapter can provide a `SurfaceHost` implementation.

### 10.2 Qt adapter bootstrap (single-canvas)

Tasks:
- Create `src/adapters/qt/` module with:
  - `qt_surface_host.hpp/.cpp`
  - lightweight runtime bootstrap (`qt_runtime.hpp/.cpp`) that binds `QWindow`/`QVulkanInstance` to Spectra backend config.
- Start with one embedded canvas path (`QWidget::createWindowContainer`) and no multi-canvas policy yet.

Primary files:
- `src/adapters/qt/*` (new)
- `examples/qt_embed_demo.cpp` (migrate to adapter path)
- `CMakeLists.txt` and/or adapter-specific CMake files

Definition of done:
- Example app creates a canvas, renders, resizes, and shuts down cleanly.
- Surface creation uses `QVulkanInstance::surfaceForWindow()` (no platform-specific `winId` Vulkan surface branches).

### 10.3 Framebuffer size + DPR normalization

Tasks:
- Replace all runtime framebuffer size reads that still call GLFW directly.
- Centralize logical-to-pixel conversion utility for Qt DPR and keep GLFW behavior unchanged.

Primary files:
- `src/render/vulkan/*`
- `src/ui/window/*`
- `src/adapters/glfw/*`
- `src/adapters/qt/*`

Definition of done:
- All resize/recreate triggers consume adapter-neutral size inputs.
- Qt DPR changes correctly reflow viewport/swapchain extent.

### 10.4 Build graph and toggles

Tasks:
- Add `spectra_qt_adapter` target.
- Add toggle: `SPECTRA_BUILD_QT_EXAMPLE` (default `OFF` in CI unless Qt runner exists).
- Keep headless/export targets independent of Qt/GLFW.

Primary files:
- `CMakeLists.txt`
- `cmake/*.cmake` (if split)

Definition of done:
- `cmake --build` succeeds with:
  - Qt disabled
  - Qt adapter enabled + example enabled
  - GLFW-only path unchanged

### 10.5 Minimal validation for Phase 1 exit

Required checks:
- Build: `cmake --build build --target spectra -j6`
- Build: Qt example target when enabled.
- Runtime: single-canvas embed in `QMainWindow`, continuous resize for 2 minutes.
- Runtime: pan/zoom/select interaction sanity.
- Validation layers: no new surface/swapchain lifetime errors.

Phase 1 exit criteria:
- Adapter seam works for both GLFW and Qt.
- Single Qt canvas is operational and stable under resize.
- Headless/export path still builds without Qt.

## 11) Interface Draft (first header cut)

```cpp
// src/platform/window_system/surface_host.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

namespace spectra::platform {

struct NativeWindowHandle {
  void* ptr = nullptr; // adapter-owned native handle (GLFWwindow*, QWindow*, etc.)
};

struct FramebufferSize {
  uint32_t width = 0;
  uint32_t height = 0;
};

class SurfaceHost {
 public:
  virtual ~SurfaceHost() = default;

  virtual std::vector<const char*> required_instance_extensions() const = 0;
  virtual VkSurfaceKHR create_surface(VkInstance instance, NativeWindowHandle window) const = 0;
  virtual std::optional<FramebufferSize> framebuffer_size(NativeWindowHandle window) const = 0;

  virtual void on_surface_about_to_destroy(NativeWindowHandle /*window*/) {}
  virtual void on_surface_created(NativeWindowHandle /*window*/) {}
};

}  // namespace spectra::platform
```

Notes:
- Keep the interface intentionally small in Phase 1.
- Additional hooks (clipboard/cursor/timing) should be separate interfaces, not overloaded into `SurfaceHost`.

## 12) CMake Wiring Plan (minimal)

Target additions:
- `spectra_qt_adapter` (STATIC or OBJECT, matching existing adapter pattern).
- `spectra_glfw_adapter` remains current default for existing examples.

Options:
- `SPECTRA_USE_QT` (enables Qt adapter source/defs).
- `SPECTRA_BUILD_QT_EXAMPLE` (builds Qt embed sample app).

Rules:
- `libspectra_core` and `libspectra_render` never link Qt.
- Qt example links: `spectra_qt_adapter` + core/render (+ optional `libspectra_ui_imgui`).
- Headless/export targets must not inherit Qt transitive dependencies.

## 13) Next Session First 90 Minutes (concrete order)

1. Add/compile `surface_host.hpp` and ensure all existing call sites build with the interface.
2. Implement `qt_surface_host` with extension query + surface creation + framebuffer size.
3. Add `spectra_qt_adapter` target and wire `examples/qt_embed_demo.cpp` through it.
4. Remove/replace any remaining direct `glfwGetFramebufferSize` calls in active render paths.
5. Build both default target and Qt example target; log errors directly into this plan if blockers remain.

## 14) Phase 1 Backlog (ticketized)

Legend:
- Priority: `P0` critical for Phase 1 exit, `P1` strongly recommended before merge.
- Status: `todo | in_progress | blocked | done`.

| ID | Priority | Status | Task | Primary Paths | Depends On | Done When |
|---|---|---|---|---|---|---|
| P1-01 | P0 | done | Add canonical `SurfaceHost` interface and update includes/types | `src/platform/window_system/surface_host.hpp`, `src/render/vulkan/window_context.hpp` | none | Builds with interface present and no duplicate type definitions |
| P1-02 | P0 | done | Route Vulkan instance extension discovery through `SurfaceHost` only | `src/render/vulkan/vk_device.cpp` | P1-01 | No direct GLFW extension query in Vulkan core path |
| P1-03 | P0 | done | Route Vulkan surface creation and framebuffer size through `SurfaceHost` only | `src/render/vulkan/vk_backend.cpp`, `src/render/vulkan/*` | P1-01 | No direct GLFW surface/size calls in Vulkan core path |
| P1-04 | P0 | done | Implement minimal Qt surface host (`QWindow` + `QVulkanInstance`) | `src/adapters/qt/qt_surface_host.hpp/.cpp` | P1-01 | Can create surface and report framebuffer size for one canvas |
| P1-05 | P0 | done | Add Qt adapter bootstrap runtime and example wiring | `src/adapters/qt/qt_runtime.*`, `examples/qt_embed_demo.cpp` | P1-04 | Example renders, resizes, and exits cleanly |
| P1-06 | P0 | done | Add CMake toggles and adapter targets | `CMakeLists.txt`, `cmake/*` | P1-04 | Build passes for both Qt OFF and Qt ON+example configs |
| P1-07 | P1 | done | Remove remaining direct `glfwGetFramebufferSize` usage in active runtime paths | `src/ui/window/*`, `src/adapters/glfw/*`, `src/render/vulkan/*` | P1-03 | Search confirms adapter-neutral size retrieval in active render paths |
| P1-08 | P1 | done | Add resize/debounce instrumentation logs guarded by debug flag | `src/adapters/qt/qt_runtime.cpp` | P1-03 | Resize storm behavior can be inspected without intrusive debug tools |
| P1-09 | P1 | done | Add smoke test script/checklist for Qt single-canvas | `plans/QT_embed_plan.md` §19 | P1-05, P1-06 | Reproducible manual validation steps checked and recorded |

## 15) Build + Run Matrix (exact commands)

Assumed build dir:
- `build/`

Configure/build (GLFW/headless baseline):
```bash
cmake -S . -B build -DSPECTRA_USE_QT=OFF
cmake --build build --target spectra -j6
```

Configure/build (Qt adapter + example):
```bash
cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON
cmake --build build-qt --target spectra qt_embed_demo -j6
```

Runtime checks:
```bash
# baseline binary/app target name may vary by current tree
cmake --build build-qt --target qt_embed_demo -j6
```

Manual checks to record in session log:
- App opens with embedded canvas in `QMainWindow`.
- 2-minute continuous resize has no crash/hang/flicker storm.
- Pan/zoom/select interactions remain responsive.
- Close/reopen app has clean shutdown (no validation fatal errors).

## 16) Decision Gates Before Merge

Gate A: Surface abstraction correctness
- Pass if Vulkan core has no direct GLFW surface/extension/framebuffer calls.
- Fail if any such calls remain outside adapter implementations.

Gate B: Qt bootstrap stability
- Pass if `qt_embed_demo` runs with one canvas and survives resize torture.
- Fail if frequent `OUT_OF_DATE` loops or unrecoverable surface loss occur.

Gate C: Build graph hygiene
- Pass if Qt OFF build does not link Qt transitively and headless/export still build.
- Fail if Qt libraries appear as required deps in non-Qt targets.

Gate D: Regression safety
- Pass if existing GLFW path remains functional (basic interaction + startup/shutdown).
- Fail if GLFW behavior regresses while enabling Qt adapter.

## 17) Open Design Questions (must resolve in Phase 1/2 boundary)

1. `QVulkanInstance` ownership model:
- Option A: Adapter creates/owns internal `QVulkanInstance`.
- Option B: Host app injects a preconfigured `QVulkanInstance`.
- Current recommendation: support A first, design runtime API so B can be added without ABI break.

2. Hidden/minimized canvas frame policy:
- Option A: stop rendering entirely until visible.
- Option B: render at low rate for internal animations.
- Current recommendation: A in Phase 1 for simplicity and predictable resource usage.

3. Device-loss policy in Qt mode:
- Option A: auto-attempt single runtime reinit and emit signal.
- Option B: fail-fast and require host restart.
- Current recommendation: A with bounded retries and clear `deviceLost(reason)` signal.

## 18) Fallback Strategy If Qt Vulkan Path Blocks

Trigger conditions:
- Reproducible platform-specific crash in `surfaceForWindow()` path.
- Incompatible Qt/Vulkan loader behavior unresolved within one session.

Fallback:
- Keep `spectra_qt_adapter` behind compile flag and preserve GLFW as default shipping path.
- Land abstraction seams (`SurfaceHost`, CMake separation, size/input normalization) independently.
- Continue Qt delivery via feature branch while avoiding regressions in mainline render path.

Exit from fallback:
- Re-enable Qt default-off target once single-canvas smoke passes on at least one Linux + one Windows environment.

## 19) Smoke Test Checklist — Qt Single-Canvas (P1-09)

Prerequisites:
- Build: `cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON -DSPECTRA_BUILD_EXAMPLES=ON && cmake --build build-qt --target qt_embed_demo -j6`
- Enable validation layers: `export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
- Enable debug logging: set Spectra log level to Debug before running.

### 19.1 Startup
- [ ] `./build-qt/examples/qt_embed_demo` launches without crash.
- [ ] QMainWindow appears with embedded Vulkan canvas.
- [ ] Two line plots (sin/cos) are visible with correct layout.
- [ ] No validation layer errors in stderr/log on startup.

### 19.2 Interaction
- [ ] Left-click drag: pans the plot.
- [ ] Scroll wheel: zooms in/out around cursor.
- [ ] Right-click drag: directional zoom (horizontal/vertical bias).
- [ ] Keyboard: R resets view, G toggles grid (if wired).
- [ ] Interaction remains responsive after 30+ seconds of use.

### 19.3 Resize Torture (2 minutes)
- [ ] Drag window edges rapidly for 60 seconds — no crash, no hang.
- [ ] Minimize → restore: canvas reappears correctly.
- [ ] Maximize → restore: canvas reappears correctly.
- [ ] Resize to very small (~100x75 px) — no crash, no validation errors.
- [ ] Resize to very large (fullscreen) — no crash, no validation errors.
- [ ] During rapid resize, no swapchain recreation storm (check debug log: recreate count should be << resize event count due to debounce).

### 19.4 Visibility
- [ ] Minimize window: frame timer fires but `renderFrame()` skips (no GPU work).
- [ ] Restore from minimize: rendering resumes without artifacts.
- [ ] Hide window behind another fullscreen window: no crash (may still render — not occluded on all compositors).

### 19.5 Shutdown
- [ ] Close window via X button: clean exit, no crash.
- [ ] No validation layer errors on shutdown.
- [ ] No leaked Vulkan resources (check debug log for "Shutdown complete").
- [ ] Exit code 0.

### 19.6 DPR / Monitor (if applicable)
- [ ] On HiDPI display: text and lines are crisp (DPR-aware rendering).
- [ ] Move window between monitors with different DPR: canvas rescales correctly.

### 19.7 Build Regression
- [ ] `cmake --build build --target spectra -j6` (GLFW-only) still passes.
- [ ] `ctest` in standard build: no new failures beyond pre-existing golden image tests.

Record results in the session log after running.

## 20) Smoke Test Checklist — Qt Multi-Canvas (Phase 2)

Prerequisites:
- Build: `cmake -S . -B build-qt -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON -DSPECTRA_BUILD_EXAMPLES=ON && cmake --build build-qt --target qt_multi_canvas_demo -j6`
- Enable validation layers: `export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
- Enable debug logging: set Spectra log level to Debug before running.

### 20.1 Startup
- [ ] `./build-qt/examples/qt_multi_canvas_demo` launches without crash.
- [ ] Main window shows two Vulkan canvases side-by-side.
- [ ] Left/right plots are both visible (phase-shifted data, not identical overlays).
- [ ] No validation-layer errors on startup.

### 20.2 Independent Interaction
- [ ] Pan/zoom left canvas; right canvas view does not move.
- [ ] Pan/zoom right canvas; left canvas view does not move.
- [ ] Repeated alternating interaction (left then right) remains responsive for 60+ seconds.

### 20.3 Canvas Detach/Reattach (`Ctrl+D`)
- [ ] Press `Ctrl+D`: right canvas detaches (no crash, left canvas keeps rendering).
- [ ] Press `Ctrl+D` again: right canvas re-attaches and resumes rendering.
- [ ] Repeat detach/reattach cycle at least 5 times with no validation-layer surface/swapchain lifetime errors.

### 20.4 Dual-Canvas Resize Torture
- [ ] Drag splitter divider rapidly for 60 seconds: no crash/hang.
- [ ] Resize main window rapidly for 60 seconds: no swapchain recreate storm beyond expected debounced behavior.
- [ ] Minimize/restore while in multi-canvas mode: both canvases recover correctly.

### 20.5 Shutdown + Regression
- [ ] Close window via X button: clean exit with code 0.
- [ ] No validation-layer errors on shutdown.
- [ ] `cmake --build build --target spectra -j6` still passes after Qt multi-canvas changes.
