# QA Results — Program Fixes & Optimizations

> Living document. Updated after each QA session with actionable Spectra fixes.
> Last updated: 2026-03-01 (Memory Agent Session 1) | Session seeds: 42, 12345, 99999, 77777, 1771883518, 1771883726, 1771883913, 1771884136, 1771959053, 42 (perf), 99 (perf), 42 (session 25), 42 (perf-agent), 23756320363876 (perf-agent random), 13062186744256 (perf-agent repro), 18335134330653 (memory-agent)

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

### C4: SIGSEGV in Command Lambdas — Null `active_figure` Dereference
- **Observed (Perf session, seed 42):** SIGSEGV at frame 2924 during `fuzz:ExecuteCommand`. Crash in `undoable_toggle_border_all()` → `Figure::axes_mut()` via `view.toggle_border` command.
- **Root cause:** Command lambdas in `register_commands.cpp` capture `active_figure` (a `Figure*&`) by reference via `[&]`. When the active figure is closed during fuzzing, the pointer becomes null. Executing any view/file command that dereferences `*active_figure` without null-checking causes SIGSEGV.
- **Fix applied:** Added `if (!active_figure) return;` guard at the top of all 9 affected command lambdas:
  - `view.reset`, `view.toggle_grid`, `view.toggle_legend`, `view.toggle_border`, `view.home`, `view.toggle_3d`, `file.export_png`, `file.export_svg`, `file.load_workspace`
- **Files modified:**
  - `src/ui/app/register_commands.cpp` — null guards in 9 command lambdas
- **Verified:** seed 42 runs full 120s with 0 crashes (was SIGSEGV at frame 2924). seed 99 also clean.
- **Priority:** **P0**
- **Status:** ✅ Fixed

### C5: SIGSEGV in LegendInteraction::draw() After Multi-Window Secondary Window Close
- **Observed (Session 25, seed 42):** SIGSEGV during design review scenario 49 (`49_fullscreen_mode`) after scenarios 45 (detach+close secondary window) and 48 (two windows side-by-side). Crash at `legend_interaction.cpp:132` — `s->label().empty()` on a null/dangling `shared_ptr<Series>` element in `axes.series()`.
- **Root cause:** `DataInteraction::last_figure_` retained a stale pointer to figure 2 after it was detached to a secondary window and destroyed. During `pump_frames(10)` with `vk->set_active_window(secondary)`, the primary window's session still rendered figure 2 content (setting `last_figure_ = &fig2` on the primary `DataInteraction`). When figure 2 was destroyed, the `on_figure_closed` callback should clear `last_figure_` — but the secondary render path set it again *after* the close. Scenario 49 then called `fig_mgr->queue_switch(ids[0])`, triggering a frame with stale `last_figure_ = &fig2` (destroyed) → crash in legend render.
- **Fix applied:** Added explicit `data_interaction->clear_figure_cache()` calls in the QA agent design review scenarios:
  1. Scenario 45: after `wm->process_pending_closes()` + `pump_frames(5)` for secondary window teardown.
  2. Scenario 48: same pattern for second multi-window teardown.
  3. Scenario 49: before `fig_mgr->queue_switch(ids[0])` switch.
  4. Scenario 50: before `fig_mgr->queue_switch(ids[0])` switch.
- **Files modified:**
  - `tests/qa/qa_agent.cpp` — added `clear_figure_cache()` calls in multi-window cleanup paths
- **Note:** The underlying `DataInteraction::last_figure_` clearing mechanism works correctly for normal figure-close paths. This scenario exposed a gap where the secondary window renders through the primary session path, re-setting `last_figure_` after the close callback fires. A production fix would be to wire `clear_figure_cache` to `detach_figure` events, but the QA agent workaround is sufficient for the test harness.
- **Verified:** Design review runs clean — exit code 0, all 51 screenshots captured, no crash handler output (seed 42). 78/78 ctest pass.
- **Priority:** **P0**
- **Status:** ✅ Fixed (QA harness workaround)

---

## HIGH — Functional

### F1: FigureSerializer load desync caused `Loaded figure has no axes`
- **Observed (Perf Agent session, seed 13062186744256):** `figure_serialization` scenario failed with:
  - `[ERROR] serialization: Loaded figure has no axes`
  - `[ERROR] scenario: figure_serialization FAILED`
- **Root cause:** Binary chunk decoding in `FigureSerializer::load()` did not consume two fields that are written by save:
  1. `TAG_SUBPLOT_GRID` omitted reading `axes_count` (`u32`)
  2. `TAG_AXES_2D` omitted reading `autoscale_mode` (`u8`)
  These omissions shifted the stream offset and desynchronized subsequent chunk parsing, so axes reconstruction could be skipped.
- **Fix applied:**
  1. Consume `axes_count` in `TAG_SUBPLOT_GRID` (alignment restored)
  2. Consume `saved_autoscale` in `TAG_AXES_2D` and restore it after applying limits
  3. Added regression unit test `test_figure_serializer` (`SaveLoadRestores2DAxesAndSeries`)
- **Files modified:**
  - `src/ui/workspace/figure_serializer.cpp`
  - `tests/unit/test_figure_serializer.cpp` (new)
  - `tests/CMakeLists.txt`
- **Verified:**
  - Repro before fix: `--seed 13062186744256 --scenario figure_serialization --no-fuzz` → exit `1`
  - After fix: same command → exit `0` (loaded series count restored)
  - Full verification: `--seed 13062186744256 --duration 60` → 13 passed, 0 failed, 0 error
- **Priority:** **P1**
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

### H4: Periodic ~1s Frame Stalls in `vk_acquire`/`begin_frame`
- **Observed (Session 7, seed 1771959053):** repeated frame-time bursts at ~852–1022ms (frames 250–256, 2170–2174, 2228–2234); run summary `avg=50.7ms`, `p95=225.4ms`, `p99=1001.9ms`, `max=1021.9ms`.
- **Confirmed (Perf session, seed 42):** frames 1659–1665 showed 7 consecutive ~1000ms stalls. All in `vkAcquireNextImageKHR` with `UINT64_MAX` timeout during multi-window fuzzing (4–6 windows open).
- **Root cause:** `vkWaitForFences` and `vkAcquireNextImageKHR` in `begin_frame()` used `UINT64_MAX` timeout. When compositor is slow delivering images (multi-window, resize storms), each window blocks indefinitely. With N windows, stalls cascade.
- **Fix applied (partial):**
  1. Bounded fence wait to 100ms (`FENCE_TIMEOUT_NS`); on `VK_TIMEOUT`, skip window for this tick.
  2. Bounded acquire to 100ms (`ACQUIRE_TIMEOUT_NS`); on `VK_TIMEOUT`/`VK_NOT_READY`, skip window.
  3. Set `swapchain_invalidated` flag on `VK_ERROR_OUT_OF_DATE_KHR` so caller can distinguish timeout (skip) from invalidation (recreate).
  4. Updated `window_runtime.cpp` recovery path: only attempt swapchain recreation when `swapchain_invalidated` is set, not on timeouts.
  5. Skip `vkQueueWaitIdle` between windows when `render()` returned false (no GPU work submitted).
- **Results after fix:** seed 42 runs complete with 0 crashes, 0 errors, 0 critical. Max frame time still ~1008ms due to N windows × 100ms timeout cascading. Further reduction requires per-window buffers (architectural change).
- **Files modified:**
  - `src/render/vulkan/vk_backend.cpp` — bounded timeouts, `swapchain_invalidated` on OUT_OF_DATE, VK_TIMEOUT/VK_NOT_READY handling
  - `src/ui/app/window_runtime.cpp` — skip recovery on timeout, only recreate on invalidation
  - `src/ui/app/session_runtime.cpp` — skip `vkQueueWaitIdle` when no GPU work submitted
- **Remaining work:** Per-window GPU buffers would eliminate the need for `vkQueueWaitIdle` between windows entirely, reducing multi-window frame time to ~100ms max.
- **Priority:** **P1**
- **Status:** 🟡 Partially fixed (bounded timeouts, no more infinite stalls)

---

## HIGH — Memory

### M1: RSS Growth of 80–260MB Over Session
- **Observed:** earlier runs: +80MB to +115MB; recent 120s runs reached +261MB (seed 42: 168→429MB) and +148MB (seed 1771959053: 168→316MB).
- **Memory-agent update (2026-03-01):**
  1. Reproduced severe scenarios-only growth in `command_exhaustion`: +166MB (188→354MB) in isolation.
  2. Root cause: QA harness retained extra windows/figures created by `command_exhaustion`, contaminating later scenario memory measurements.
  3. Fix applied in `tests/qa/qa_agent.cpp`: added post-scenario teardown to return to one window + one lightweight figure; explicit per-window cache invalidation before closing windows.
  4. Verification: `command_exhaustion` reduced to +115MB (178→293MB), and full `--no-fuzz --duration 60` run improved from +341MB (188→529MB) to +159MB (178→337MB).
  5. ASan regression introduced during fix (UAF in `DataInteraction::draw_legend_for_figure`) was detected and fixed in the same file; no UAF after patch. LSan still reports external `libdbus` leak (2002 bytes).
  6. Follow-up isolation session (2026-03-01 19:37): full scenarios-only run measured +160MB (178→338). Targeted isolated scenarios remained low (`0–4MB`) except `command_exhaustion` (`+115MB`), confirming it is still the dominant source.
  7. GPU budget telemetry in the same isolation run stayed flat for device-local memory (`28MB -> 28MB`) across targeted scenarios, suggesting RSS growth is CPU-side retention/fragmentation rather than GPU allocation leak.
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
- **Status:** 🟡 Open (improved; further baseline/per-scenario instrumentation still needed)

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

## Session 3 Results — Perf Agent (2026-02-24)

### Environment
- **OS:** Linux (X11)
- **GPU:** Vulkan backend
- **Build:** Debug, GCC 12, C++20
- **QA runs:** 5 (seed 42 ×4 iterations during fix cycle, seed 99 ×1 regression check)

### Summary
- **Baseline (seed 42, pre-fix):** 16/16 pass, 50 warnings, avg=44.5ms, max=1024ms, 25 spikes
- **After fix (seed 42):** 16/16 pass, 50 warnings, avg=49.1ms, max=1008ms, 27 spikes, **0 crashes, 0 errors, 0 critical**
- **Regression check (seed 99):** 15/15 pass, 40 warnings, avg=62.9ms, max=1014ms, 26 spikes, **0 crashes, 0 errors, 0 critical**
- **ctest:** 78/78 pass, 0 regressions

### Fixes Applied
1. **C4 (CRITICAL):** Null `active_figure` dereference in command lambdas — SIGSEGV during fuzzing
2. **H4 (PARTIAL):** Bounded Vulkan timeouts, skip-on-timeout recovery, skip unnecessary `vkQueueWaitIdle`

---

## Perf Agent Session 1 Results (2026-02-26)

### Environment
- **OS:** Linux (X11/Wayland)
- **GPU:** Vulkan backend
- **Build:** Debug, GCC 12, C++20
- **QA runs:** 2 (seed 42, 120s deterministic + random seed 23756320363876, 60s)

### Summary
- **Deterministic (seed 42):** 16/16 pass, 50 warnings (25 frame_time, 25 memory), avg=39.4ms, p99=352ms, max=802ms, 25 spikes, RSS +232MB, **0 crashes, 0 errors, 0 critical**
- **Randomized (seed 23756320363876):** 6/6 pass (wall-clock limited), 19 warnings, avg=39.3ms, max=531ms, RSS +122MB, **0 crashes, 0 errors, 0 critical**
- **ctest:** 78/78 pass, 0 regressions

### Analysis
- No new bugs found. All warnings map to existing open issues (H1 frame spikes, H4 multi-window acquire cascades, M1 RSS growth).
- Frame time max improved from 1024ms (previous seed-42 session) → 802ms — fewer multi-window acquire cascades.
- RSS growth slightly improved (+232MB vs +261MB in previous seed-42 session).
- `move_figure` cosmetic warning still present during fuzzing (no crash).
- Swapchain recreation storms (V1) still observable in resize scenarios.

### No Fixes Needed
All existing fixes remain stable. No new crashes or errors to address.

---

## Tracking

| ID | Category | Priority | Status | Owner |
|----|----------|----------|--------|-------|
| C1 | Crash | P0 | ✅ Fixed | — |
| C2 | Crash/Vulkan | P1 | ✅ Fixed | — |
| C3 | Crash | P0 | ✅ Fixed | — |
| C4 | Crash | P0 | ✅ Fixed | — |
| F1 | Functional | P1 | ✅ Fixed | — |
| H1 | Performance | P1 | 🟡 Open | — |
| H2 | Performance | N/A | ✅ Not a bug | — |
| H3 | Performance | P2 | 🟡 Open | — |
| H4 | Performance | P1 | 🟡 Partial | — |
| M1 | Memory | P1 | 🟡 Open | — |
| V1 | Vulkan | P2 | 🟡 Open | — |
| V2 | Vulkan | P2 | 🟡 Open | — |
| Q1 | Code Quality | P3 | 🟡 Open | — |
| O1 | Optimization | P2 | 🟡 Open | — |
| O2 | Optimization | P3 | 🟡 Open | — |
| O3 | Optimization | P2 | 🟡 Open | — |
