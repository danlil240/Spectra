# QA Results — Program Fixes & Optimizations

> Living document. Updated after each QA session with actionable Spectra fixes.
> Last updated: 2026-02-24 | Session seeds: 42, 12345, 99999, 77777, 1771883518, 1771883726, 1771883913, 1771884136

---

## Session 1 Results (2026-02-23)

### Environment
- **OS:** Linux (X11/Wayland)
- **GPU:** Vulkan backend
- **Build:** Debug, GCC 12, C++20
- **QA runs:** 2 (seed 42, 30s + 60s)

---

## CRITICAL — Crash

### C1: SIGSEGV During Fuzzing — Dangling Figure/Axes Pointers
- **Observed:** SIGSEGV in `LegendInteraction::draw()` (dangling `unique_ptr<Series>`) and `InputHandler::on_mouse_button()` (dangling `AxesBase*` via `dynamic_cast`).
- **Root cause:** `FigureManager::close_figure()` calls `registry_.unregister_figure()` which destroys the `Figure` and its `Axes`/`Series`. But `DataInteraction::last_figure_` and `InputHandler::active_axes_base_` still hold raw pointers to the destroyed objects.
- **Fix applied:**
  1. Added `DataInteraction::clear_figure_cache(Figure*)` — nulls `last_figure_` and `active_axes_` when a figure is destroyed.
  2. Added `InputHandler::clear_figure_cache(Figure*)` — nulls `figure_`, `active_axes_`, `active_axes_base_`, `drag3d_axes_` and resets drag state.
  3. Wired `FigureManager::set_on_figure_closed()` callback in `app_step.cpp` to call both clear methods.
  4. Added defensive validation in `InputHandler::on_mouse_button()` — checks `active_axes_base_` still belongs to `figure_->axes()` before `dynamic_cast`.
- **Files modified:**
  - `src/ui/overlay/data_interaction.hpp` — added `clear_figure_cache()`
  - `src/ui/input/input.hpp` — added `clear_figure_cache()`
  - `src/ui/input/input.cpp` — added axes pointer validation before dynamic_cast
  - `src/ui/app/app_step.cpp` — wired `on_figure_closed` callback
- **Verified:** seed 42 `rapid_figure_lifecycle` scenario passes (was crashing). seed 12345 full session passes.
- **Priority:** **P0**
- **Status:** ✅ Fixed

### C2: Vulkan Device Lost During Multi-Window Fuzzing
- **Observed:** After ~1429 frames of fuzzing with seed 42, `VulkanBackend::begin_frame()` throws `Vulkan device lost`. Reproduced with seed 77777 at frame 2544.
- **Trigger:** Vulkan validation error `vkCmdCopyImageToBuffer(): pRegions[0].imageOffset.y (0) + extent.height (933) exceeds imageSubresource height extent (720)` — `readback_framebuffer()` uses caller-provided dimensions that are stale after a window resize.
- **Root cause:** `readback_framebuffer()` accepted `width`/`height` from the caller without clamping to the actual swapchain image extent. After a resize, the swapchain extent shrinks but the caller still passes the old (larger) dimensions, causing an out-of-bounds image copy that triggers device lost.
- **Fix applied:** Clamp `width` and `height` to `active_window_->swapchain.extent` (windowed) or `offscreen_.extent` (headless) at the start of `readback_framebuffer()`. Also added early return for zero-size dimensions.
- **Files modified:**
  - `src/render/vulkan/vk_backend.cpp` — `readback_framebuffer()` dimension clamping
- **Verified:** seed 77777 now runs 4031 frames (90s) with 0 critical issues. Previously crashed at frame 2544.
- **Priority:** **P1**
- **Status:** ✅ Fixed

### C3: SIGSEGV in Inspector::draw_series_properties() — Dangling Series Pointer
- **Observed (Session 2, seed 77777):** SIGSEGV in `__dynamic_cast` called from `Inspector::draw_series_properties()` at frame 2732 during `fuzz:KeyPress`.
- **Root cause:** `SelectionContext` holds raw `Series*` pointer. When a figure is closed during fuzzing, the `Series` is destroyed but the `Inspector`'s `SelectionContext` still references it. The null check passes (dangling pointer is non-null), then `dynamic_cast` on the destroyed object causes SIGSEGV. Same class of bug as C1.
- **Fix applied:**
  1. Added `ImGuiIntegration::clear_figure_cache(Figure*)` that clears both `selection_ctx_` and `inspector_.context()` when the figure matches.
  2. Wired it into the existing `FigureManager::set_on_figure_closed()` callback in `app_step.cpp`.
- **Files modified:**
  - `src/ui/imgui/imgui_integration.hpp` — added `clear_figure_cache(Figure*)`
  - `src/ui/app/app_step.cpp` — wired `imgui_ui->clear_figure_cache(fig)` into figure-closed callback
- **Verified:** seed 77777 now runs full 90s (4031 frames) with 0 crashes.
- **Priority:** **P0**
- **Status:** ✅ Fixed

---

## HIGH — Performance

### H1: Large Frame Time Spikes (157–178ms) During Data Operations
- **Observed:** Frames 858, 914, 956, 1042 (Run 1) and frames 2461, 2765 (Run 2) show 157–178ms spikes (7–8× average).
- **Cause:** Almost certainly the `LargeDataset` fuzz action creating 100K–500K point `LineSeries` on the main thread. The `line()` call does immediate vertex buffer upload.
- **Fix options:**
  1. **Async data upload:** Move vertex buffer creation to a background thread, present previous frame's data until ready
  2. **Chunked upload:** Split large datasets across multiple frames (e.g., 50K points per frame)
  3. **Lazy GPU upload:** Defer GPU buffer creation until first render, batch multiple series updates
- **Suspected files:**
  - `src/core/series.cpp` — `LineSeries` constructor / `set_y()` / data upload
  - `src/render/renderer.cpp` — vertex buffer allocation path
- **Impact:** User-visible stutter when loading large datasets
- **Priority:** **P1**
- **Status:** 🟡 Open

### H2: VSync Frame Time Bimodal Distribution
- **Observed:** Frame times cluster at ~3ms (non-VSync) and ~16ms (VSync-locked). The profiler shows `vk_acquire` averaging 11.2ms — this is the VSync wait.
- **Analysis:** This is expected behavior for VSync-on rendering. The `FrameScheduler` correctly paces to 60 FPS. However, the QA agent's spike detection interprets VSync frames as "slow" because early frames (before VSync kicks in) are fast.
- **Fix:** Not a Spectra bug. QA agent threshold needs adjustment (see `QA_update.md` item #1).
- **Priority:** N/A (QA agent issue, not Spectra issue)
- **Status:** ✅ Not a bug

### H3: `win_update` Max Spike at 22ms
- **Observed:** Profiler shows `win_update` avg=793µs but max=22.13ms (28× average).
- **Cause:** Likely ImGui layout recalculation when figure count changes or dock layout shifts. The `imgui_build` phase is stable (max 757µs), so the spike is in the non-ImGui update path.
- **Investigation:** Add per-stage timing inside `WindowRuntime::update()` to isolate whether it's layout, data transform, or axes recalculation.
- **Suspected files:**
  - `src/ui/app/window_runtime.cpp` — `update()` method
  - `src/ui/layout/layout_manager.cpp` — layout recalculation
- **Priority:** **P2**
- **Status:** 🟡 Open

---

## HIGH — Memory

### M1: RSS Growth of 80–115MB Over Session
- **Observed:** Run 1: 199→279MB (+80MB). Run 2: 168→284MB (+115MB, flagged at frame 2820).
- **Analysis:** Growth is expected to some degree — the fuzzer creates up to 20 figures with 10–500K point series each. But 115MB for ~20 figures seems high.
- **Breakdown needed:**
  1. CPU-side series data: 20 figures × ~200K points × 8 bytes (x+y float) = ~32MB
  2. GPU vertex buffers: mirrored = ~32MB
  3. ImGui atlas + context: ~5MB
  4. Remaining ~46MB: potential leak or fragmentation
- **Investigation steps:**
  1. Run with `--no-fuzz` to measure scenario-only growth
  2. Run with Valgrind `--tool=massif` for heap profiling
  3. Check if closed figures' GPU resources are freed (VMA allocations)
  4. Check if `FigureRegistry::unregister()` releases all owned memory
- **Suspected files:**
  - `src/ui/figures/figure_registry.cpp` — figure lifecycle
  - `src/render/vulkan/vk_backend.cpp` — VMA allocation tracking
  - `src/core/series.cpp` — data buffer ownership
- **Priority:** **P1**
- **Status:** 🟡 Open

---

## MEDIUM — Vulkan

### V2: Descriptor Image Layout Mismatch in Multi-Window
- **Observed (Session 2, seed 99999):** Vulkan validation error during `vkQueueSubmit` — `vkCmdDraw` expects swapchain image in `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` but actual layout is `VK_IMAGE_LAYOUT_UNDEFINED`.
- **Context:** Occurs after `move_figure` fails with "source window does not have figure". The error appears when rendering to a newly created window whose swapchain images haven't been presented yet.
- **Root cause hypothesis:** The render pass `initialLayout = VK_IMAGE_LAYOUT_UNDEFINED` correctly handles the first render. But a **descriptor** (likely ImGui font atlas or a texture sampler) references a swapchain image from another window that is still in UNDEFINED layout. This could happen if ImGui's internal texture references get confused across window contexts.
- **Fix options:**
  1. Ensure each window's ImGui context only references its own swapchain resources
  2. Add explicit image layout transition barrier before first render to a new window
  3. Validate `move_figure` source window ownership before attempting the move
- **Suspected files:**
  - `src/render/vulkan/vk_backend.cpp` — multi-window begin_frame/end_frame
  - `src/ui/window/window_manager.cpp` — `move_figure()` validation
  - `src/ui/imgui/imgui_integration.cpp` — per-window ImGui context setup
- **Priority:** **P2** — validation error only, no crash observed
- **Status:** 🟡 Open

### V1: Swapchain Recreation Storm
- **Observed:** Logs show rapid cascading swapchain recreations:
  ```
  end_frame: present returned OUT_OF_DATE
  Post-present OUT_OF_DATE, recreating: 1280x338
  ...new swapchain created: 1280x338
  end_frame: present returned OUT_OF_DATE  ← again immediately
  Post-present OUT_OF_DATE, recreating: 1280x301
  ```
- **Cause:** Window manager (compositor) is resizing the window in multiple steps. Each present triggers OUT_OF_DATE because the surface size changed again between recreate and present.
- **Fix options:**
  1. **Debounce:** After recreating swapchain, skip 1 frame before presenting to let the compositor settle
  2. **Coalesce:** On OUT_OF_DATE, query current surface size and only recreate if it differs from last recreate
  3. **Rate limit:** Max 1 swapchain recreation per 2 frames
- **Suspected files:**
  - `src/render/vulkan/vk_backend.cpp` — `end_frame()`, `recreate_swapchain()`
  - `src/ui/app/session_runtime.cpp` — resize handling
- **Impact:** Each recreation takes ~20ms (fence wait + create + destroy). 3 recreations in rapid succession = 60ms stall.
- **Priority:** **P2**
- **Status:** 🟡 Open

---

## LOW — Code Quality

### Q1: `readback_framebuffer()` May Race With Rendering
- **Observed:** QA agent calls `backend->readback_framebuffer()` for screenshots during the frame loop. This may submit a transfer command while the render command buffer is in flight.
- **Fix:** Ensure readback waits for the current frame's fence before reading. Or defer screenshot capture to after `end_frame()`.
- **Priority:** **P3**
- **Status:** 🟡 Open

---

## Optimization Opportunities

### O1: Series Data Upload Batching
- When multiple series are added in the same frame (common in QA fuzzing and real usage), each triggers a separate GPU buffer allocation + upload. Batching these into a single staging buffer + copy would reduce driver overhead.
- **Estimated impact:** 2–5ms reduction on frames with multiple series additions.

### O2: ImGui Render Command Deduplication
- Profiler shows `cmd_record` avg=214µs. For complex UIs with many figures, this could be reduced by caching ImGui draw lists when the UI hasn't changed.
- **Estimated impact:** Minor (~100µs savings).

### O3: Figure Close Deferred Cleanup
- When closing figures during fuzzing, ensure GPU resources are freed on a deferred queue (not in the hot path). This prevents stalls from `vkDeviceWaitIdle` or fence waits during close.
- **Estimated impact:** Prevents 5–20ms stalls on figure close.

---

## Tracking

| ID | Category | Priority | Status | Owner |
|----|----------|----------|--------|-------|
| C1 | Crash | P0 | ✅ Fixed | — |
| C2 | Crash/Vulkan | P1 | ✅ Fixed | — |
| C3 | Crash | P0 | ✅ Fixed | — |
| H1 | Performance | P1 | 🟡 Open | — |
| H2 | Performance | N/A | ✅ Not a bug | — |
| H3 | Performance | P2 | 🟡 Open | — |
| M1 | Memory | P1 | 🟡 Open | — |
| V1 | Vulkan | P2 | 🟡 Open | — |
| V2 | Vulkan | P2 | 🟡 Open | — |
| Q1 | Code Quality | P3 | 🟡 Open | — |
| O1 | Optimization | P2 | 🟡 Open | — |
| O2 | Optimization | P3 | 🟡 Open | — |
| O3 | Optimization | P2 | 🟡 Open | — |
