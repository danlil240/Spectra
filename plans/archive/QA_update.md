# QA Agent — Improvement Backlog

> Living document. Updated after each QA session with agent improvements to implement.
> Last updated: 2026-03-06 | Session seeds: 42, 12345, 99999, 77777, 1771883518, 1771883726, 1771883913, 1771884136, 1771959053, 18335134330653, 42 (perf-agent repro), 27243840318184, 42 (memory-agent telemetry), 42 (real desktop perf), 35619058242308 (real desktop perf random), 42 (perf-agent session 12), 25257321839906, 99, 26349518760598

---

## Session 1 Findings (2026-02-23)

### Run Summary

| Metric | Run 1 (30s) | Run 2 (60s) |
|--------|-------------|-------------|
| Frames | 1,316 | 2,871 |
| Scenarios | 10/10 passed | 10/10 passed |
| Warnings | 93 | 59 |
| Errors | 0 | 0 |
| Crash | SIGSEGV during fuzzing | Clean exit |
| RSS growth | +80MB (199→279) | +115MB (168→284) |
| Avg frame | 10.7ms | 17.2ms |
| P99 frame | 19.1ms | 71.6ms |
| Max frame | 167ms | 178ms |

---

## P0 — Critical Fixes

### 1. ~~Frame Time Spike Threshold Is Too Sensitive~~ ✅ FIXED
- **Problem:** 93 of 93 issues in Run 1 are frame_time warnings. The 3× EMA threshold triggers on normal VSync frames (~16ms) because early frames run at ~3ms (no VSync yet), dragging the EMA down.
- **Fix applied:** Added warmup period (skip first 30 frames) + absolute minimum threshold (33ms) + 3× EMA multiplier. Result: seed 12345 session now reports only 6 frame_time warnings (all genuine 145–297ms spikes from massive_datasets scenario) vs. 93 false positives before.
- **Priority:** P0 — ✅ Done

### 2. ~~Screenshot Flood~~ ✅ FIXED
- **Problem:** 93 screenshots captured, all nearly identical (~87KB each). ~8MB of disk wasted on false positives.
- **Fix applied:** Added `last_screenshot_frame_` map with 60-frame cooldown per category. Combined with fix #1, screenshot count dropped from 93 to ~1-2 per session.
- **Priority:** P0 — ✅ Done

### 3. ~~Crash Handler Needs Stack Trace~~ ✅ FIXED
- **Problem:** SIGSEGV in Run 1 only reports seed. No stack trace, no scenario/action context.
- **Fix applied:** (a) `backtrace()` + `backtrace_symbols_fd()` on Linux for stack dump. (b) `g_last_action` global updated before each fuzz action and scenario. (c) Crash report written to `qa_crash.txt` in output dir. (d) Enhanced crash banner with seed, last action, and reproduce command.
- **Verified:** Crash handler now outputs full stack trace + last action context (e.g., `fuzz:MouseClick (frame 245)`).
- **Priority:** P0 — ✅ Done

### 15. ~~Scenario Isolation — Heavyweight Figures Pollute Subsequent Scenarios~~ ✅ FIXED
- **Problem (Session 2, seed 99999):** `massive_datasets` scenario creates a figure with 1M + 5×100K points. This figure stays active during all subsequent scenarios (`undo_redo_stress`, `animation_stress`, etc.), causing 160ms+ avg `cmd_record` time and 3–6 second frame spikes during undo/redo. Only 4/10 scenarios completed within the 90s wall clock.
- **Root cause:** Scenarios that don't create their own figures inherit the active figure from the previous scenario. After `massive_datasets`, the 1.5M-point figure is still the active tab.
- **Fix applied:** Added `ensure_lightweight_active_figure()` helper that creates a small 50-point figure and switches to it. Called at the start of 7 scenarios: `undo_redo_stress`, `animation_stress`, `input_storm`, `command_exhaustion`, `mode_switching`, `stress_docking`, `resize_stress`.
- **Result:** avg frame time dropped from 254ms → 72.5ms (3.5× faster), max spike from 6708ms → 641ms (10.5× better), scenarios completed increased from 4/10 → 8/10, total frames doubled (615 → 1225).
- **File modified:** `tests/qa/qa_agent.cpp`
- **Priority:** P0 — ✅ Done

---

## Session 2 Findings (2026-02-23)

### Run Summary

| Metric | Before Fix (seed 99999) | After Fix (seed 99999) |
|--------|------------------------|------------------------|
| Frames | 615 | 1,225 |
| Scenarios | 4/10 passed | 8/10 passed |
| Warnings | 9 | 25 |
| Errors | 0 | 0 |
| Crash | Clean exit | Clean exit |
| RSS growth | +58MB (193→251) | +121MB (168→289) |
| Avg frame | 254ms | 72.5ms |
| P99 frame | 366ms | 504ms |
| Max frame | 6708ms | 641ms |
| cmd_record avg | 160ms | 13.6ms |

### New Issues Found & Fixed
- **C2 — Vulkan device lost (seed 77777, frame 2544):** `readback_framebuffer()` used stale dimensions after resize → out-of-bounds `vkCmdCopyImageToBuffer`. **Fixed:** clamp to actual swapchain extent. See `QA_results.md` C2.
- **C3 — SIGSEGV in Inspector (seed 77777, frame 2732):** Dangling `Series*` in `SelectionContext` after figure close. **Fixed:** added `ImGuiIntegration::clear_figure_cache()` wired to figure-closed callback. See `QA_results.md` C3.
- **V2 — Vulkan validation error:** Descriptor image layout mismatch in multi-window — see `QA_results.md` V2. Still open.
- **Memory growth:** +247MB over 90s session (168→415MB) — needs investigation (M1)
- **imgui_build bottleneck:** After fixing cmd_record, `imgui_build` is now the dominant cost at avg=2.6ms (was 65ms before scenario isolation fix, now reasonable)
- **`move_figure` warning:** "source window does not have figure" — figure ownership tracking issue during fuzzing (cosmetic, no crash)

### Verification Run (seed 77777, after all fixes)

| Metric | Value |
|--------|-------|
| Frames | 4,031 |
| Scenarios | 10/10 passed |
| Warnings | 76 |
| Errors | 0 |
| Critical | 0 |
| Crashes | 0 |
| RSS growth | +247MB (168→415MB) |
| Avg frame | 19.4ms (~52 FPS) |
| P95 frame | 21.5ms |
| Max frame | 301ms |
| cmd_record avg | 986μs |

---

## Session 3 Findings (2026-02-23)

### Run Summary (seed 1771883518, 120s)

| Metric | Value |
|--------|-------|
| Frames | 3,034 |
| Scenarios | 10/10 passed |
| Warnings | 22 |
| Errors | 0 |
| Critical | 0 |
| Crashes | 0 |
| RSS growth | +96MB (170→266MB) |
| Avg frame | 39.0ms |
| P95 frame | 149.6ms |
| Max frame | 463ms |

### Analysis
- **All session 2 fixes confirmed stable** — 0 crashes, 0 critical, 10/10 scenarios
- **First ~2400 frames at 60 FPS with 0 hitches** — scenario isolation fix working perfectly
- **Late-session degradation (frames 2400–3034):** `cmd_record` avg=74ms, 532/600 hitches. Caused by fuzzer's `LargeDataset` action creating 100K–500K point series that becomes the active figure. This is the known H1 issue, not a regression.
- **Memory growth modest:** +96MB vs +247MB in session 2 — fewer heavy figures created by this seed's fuzz sequence
- **No new bugs found**
- **`move_figure` warning** still appears during fuzzing — cosmetic, no crash

---

## Session 4 Findings (2026-02-23)

### Run Summary (seed 1771883726, 120s)

| Metric | Value |
|--------|-------|
| Frames | 3,737 |
| Scenarios | 10/10 passed |
| Warnings | 79 |
| Errors | 0 |
| Critical | 0 |
| Crashes | 0 |
| RSS growth | +274MB (168→442MB) |
| Avg frame | 29.2ms |
| P95 frame | 118.0ms |
| Max frame | 526ms |

### Analysis
- **All fixes stable across 4 consecutive sessions** — 0 crashes, 0 critical
- **Steady-state fuzzing at 66 FPS** with only 3 hitches per 600-frame window
- **Memory growth is the top remaining concern:** +274MB over 120s. The fuzzer creates many figures with `LargeDataset` actions (100K–500K points each) that accumulate. This is the known M1 issue — needs heap profiling to distinguish leaks from expected data growth.
- **79 warnings** (mostly memory growth) — no frame_time warnings during steady-state, only during `massive_datasets` scenario and `LargeDataset` fuzz actions
- **`move_figure` warning** still appears — cosmetic
- **No new bugs found**

---

## Session 5 Findings (2026-02-24)

### Run Summary (seed 1771883913, 120s)

| Metric | Value |
|--------|-------|
| Frames | 1,360 |
| Scenarios | 10/10 passed |
| Warnings | 22 |
| Errors | 0 |
| Critical | 0 |
| Crashes | 0 |
| RSS growth | +68MB (168→236MB) |
| Avg frame | 92.7ms |
| P95 frame | 330ms |
| Max frame | 569ms |

### Analysis
- **5 consecutive sessions with 0 crashes, 0 critical** — all fixes stable
- **Low frame count (1360)** due to `command_exhaustion` + `massive_datasets` scenarios consuming most wall clock. `command_exhaustion` creates windows, exports PNGs, and exercises all commands — `imgui_build` avg=71ms during that scenario.
- **Lowest memory growth yet:** +68MB — this seed's fuzz sequence created fewer heavy figures
- **No new bugs found**
- **Observation:** `command_exhaustion` scenario is disproportionately expensive. Consider adding a frame budget or reducing its command iteration count to leave more wall clock for fuzzing.

---

## Session 6 Findings (2026-02-24)

### Run Summary (seed 1771884136, 120s)

| Metric | Value |
|--------|-------|
| Frames | 1,368 |
| Scenarios | 10/10 passed |
| Warnings | 27 |
| Errors | 0 |
| Critical | 0 |
| Crashes | 0 |
| RSS growth | +124MB (168→292MB) |
| Avg frame | 90.5ms |
| P95 frame | 337ms |
| Max frame | 444ms |

### Analysis
- **6 consecutive sessions with 0 crashes, 0 critical** — all fixes stable
- **V2 Vulkan validation error reproduced** — descriptor image layout mismatch (`PRESENT_SRC_KHR` vs `UNDEFINED`) during multi-window rendering after `move_figure` failure. No crash, validation error only. Confirms V2 is a real but non-fatal issue.
- **Same pattern as session 5:** low frame count (1368) due to `command_exhaustion` + `massive_datasets` dominating wall clock
- **No new bugs found**

---

## Session 7 Findings (2026-02-24)

### Run Summary

| Metric | Run A (seed 42) | Run B (seed 1771959053) |
|--------|------------------|--------------------------|
| Frames | 4,710 | 2,253 |
| Scenarios | 11/11 passed | 11/11 passed |
| Warnings | 92 | 64 |
| Errors | 0 | 0 |
| Critical | 0 | 0 |
| Crashes | 0 | 0 |
| RSS growth | +261MB (168→429MB) | +148MB (168→316MB) |
| Avg frame | 22.6ms | 50.7ms |
| P95 frame | 63.5ms | 225.4ms |
| Max frame | 298.2ms | 1021.9ms |

### Analysis
- **8 consecutive sessions with 0 crashes, 0 critical** — crash fixes remain stable.
- **Known V2 validation issue reproduced again** — `PRESENT_SRC_KHR` vs `UNDEFINED` layout mismatch during multi-window churn; still non-fatal.
- **New severe perf pattern observed (seed 1771959053):** repeated ~1s frame stalls in clusters (frames 250–256, 2170–2174, 2228–2234). Profiler indicates the time is concentrated in `begin_frame`/`vk_acquire`, not `cmd_record`.
- **Memory growth remains highly seed-dependent** (from +148MB to +261MB in 120s), consistent with unresolved M1.

---

## Session 8 Findings (2026-03-01, Memory Agent)

### Run Summary

| Metric | Before (`seed 42`) | After (`seed 42`) |
|--------|---------------------|-------------------|
| Mode | `--no-fuzz --duration 60` | `--no-fuzz --duration 60` |
| Frames | 1,909 | 1,929 |
| Scenarios | 15/15 passed | 15/15 passed |
| Peak RSS | 529MB | 337MB |
| RSS delta | +341MB (188→529) | +159MB (178→337) |
| Errors/Critical | 0/0 | 0/0 |

### Analysis
- `command_exhaustion` was retaining allocations by opening windows/figures without teardown.
- Added cleanup in QA harness to return to one window + one lightweight figure after `command_exhaustion`.
- During fix, ASan caught a UAF regression in stale figure caches; this was fixed by explicit cache invalidation across all window UI contexts before closing windows.
- `command_exhaustion` isolated RSS delta improved from `+166MB` (peak 354MB) to `+115MB` (peak 293MB).
- M1 and item #7 remain open, but the baseline contamination source is now reduced.

---

## Session 9 Findings (2026-03-01, Memory Agent Follow-up)

### Run Summary

| Metric | ASan run (`--no-fuzz --duration 60`) | RSS run (`--no-fuzz --duration 60`) |
|--------|--------------------------------------|-------------------------------------|
| Frames | 1,188 | 2,492 |
| Scenarios | 6/6 passed | 16/16 passed |
| Peak RSS | 608MB | 338MB |
| RSS delta | +187MB (421→608) | +160MB (178→338) |
| GPU local usage | 28→28MB | 28→28MB |
| Errors/Critical | 0/0 | 0/0 |

### Analysis
- Targeted per-scenario isolation confirms `command_exhaustion` remains the dominant growth source (`+115MB`), while sampled scenarios (`rapid_figure_lifecycle`, `massive_datasets`, `undo_redo_stress`, `series_clipboard_selection`, `figure_serialization`, `series_removed_interaction_safety`) stayed at `0–4MB`.
- GPU budget telemetry remained flat in both full and isolated runs, reducing suspicion of GPU-side leaks for this repro path.
- Found and fixed QA harness teardown crash on `--list-scenarios`: early return skipped `shutdown_runtime()`, causing GLFW double-destroy during destructor path. Added explicit shutdown/reset before return in `tests/qa/qa_agent.cpp`.
- ASan remains clean for Spectra-owned allocations in this run; only known external `libdbus` LSan leak persists.

---

## Session 10 Findings (2026-03-01, Performance Agent)

### Run Summary

| Metric | Seed 42 (pre-fix) | Seed 42 (post-fix) | Random (post-fix) |
|--------|--------------------|--------------------|-------------------|
| Command | `--seed 42 --duration 120` | `--seed 42 --duration 120` | `--duration 60` |
| Exit code | 2 (crash) | 1 | 1 |
| Seed | 42 | 42 | 27243840318184 |
| Frames | crash at ~3606 | 4,134 | 2,492 |
| Scenarios | n/a (crash) | 20/20 passed | 16/16 passed |
| CRITICAL | n/a | 0 | 0 |
| ERROR | n/a | 12 (`vulkan_validation`) | 11 (`vulkan_validation`) |

### Analysis
- Reproduced deterministic seed-42 crash in overlay path with two stack signatures (`LegendInteraction::draw` and `Crosshair::draw_all_axes`), both tied to stale figure/axes pointers during multi-window churn.
- Product fix removed the crash on the same seed (`42`) without changing the existing open warning/error profile (H1/M1/V2 class issues still present).
- Re-run ctest still reports only known golden-image failures (`golden_image_tests`, `golden_image_tests_3d`); no new unit/integration regressions observed.

---

## Session 12 Findings (2026-03-06, Performance Agent)

### Run Summary

| Metric | Seed 42 (deterministic) | Seed 25257321839906 (random) | Seed 99 (verify) | Seed 26349518760598 (regression) |
|--------|------------------------|------------------------------|-------------------|----------------------------------|
| Command | `--seed 42 --duration 120` | `--duration 60` | `--seed 99 --duration 120` | `--duration 60` |
| Exit code | 1 | 0 | 2 (crash) | 0 |
| Frames | 6,314 | 3,774 | 4,960 (crash) | 4,470 |
| Scenarios | 20/20 passed | 20/20 passed | n/a (crash) | 20/20 passed |
| CRITICAL | 1 (H4 device lost) | 0 | n/a | 0 |
| ERROR | 0 | 0 | n/a | 0 |
| RSS delta | +245MB (198→443) | +242MB (198→440) | n/a | +191MB (198→389) |
| Avg frame | 10.5ms | 13.6ms | n/a | 11.3ms |
| P95 frame | 36.2ms | 44.4ms | n/a | 41.3ms |
| Max frame | 119.4ms | 125.8ms | n/a | 236.4ms |

### Analysis
- **Three new crashes found and fixed (C7, C8, C9):**
  - **C7:** X11 `BadLength` crash from `glfwSetClipboardString` exceeding 4MB X11 protocol limit. Fixed with clipboard size guard.
  - **C8:** `std::bad_alloc` OOM from unbounded `series.paste` accumulating hundreds of series. Fixed with 200-series-per-axes paste guard.
  - **C9:** SIGSEGV in `file.save_workspace` from dangling `Series*` in `DataInteraction::markers_`. Fixed by clearing markers in `clear_figure_cache()` and using pre-stored `series_label` instead of `m.series->label()`.
- **Nvidia driver crash on seed 99:** SIGSEGV inside `libnvidia-glcore.so.580.126.20` during `vkDestroyImageView` in swapchain recreation. Not fixable at app level; `vkDeviceWaitIdle` attempted and reverted — no improvement.
- **1 CRITICAL on seed 42 (exit 1):** known H4 Vulkan device lost at frame 6314, bounded timeouts already in place.
- **Unit tests:** 110/111 pass; only pre-existing `DesignTokens.LayoutConstants` theme test failure.
- **Performance improved vs. prior sessions:** P95 `36.2ms` (seed 42) down from `83.40ms` (session 10 seed 42).

### New Improvement Items
- **19. Detect and Group Dangling-Pointer Crash Patterns in save/load Commands**
  - Problem: `file.save_workspace` accessed `m.series->label()` on a dangling pointer. The crash manifested as SIGABRT, not SIGSEGV, making it harder to classify under the existing overlay stale-pointer bucket.
  - Fix: Add crash signature classification for `register_commands.cpp` save/load paths — group any `Series*` or `Figure*` dereference in command lambdas under a `command_stale_pointer` bucket.
  - Priority: P2

---

## Session 11 Findings (2026-03-05, Real Desktop Performance Workflow)

### Run Summary

| Metric | Seed 42 | Random seed |
|--------|---------|-------------|
| Command | `--seed 42 --duration 120` | `--duration 60` |
| Exit code | 0 | 0 |
| Seed | 42 | 35619058242308 |
| Frames | 7,319 | 4,813 |
| Scenarios | 20/20 passed | 20/20 passed |
| Errors/Critical | 0/0 | 0/0 |
| Validation monitor | inactive (`0/0`) | inactive (`0/0`) |
| Peak RSS | 360MB | 360MB |
| Max frame | 187.452ms | 115.108ms |
| Scenario retention warnings | 3 | 2 |

### Analysis
- The sandbox-only GLFW/display failures from the earlier triage did not reproduce on the real desktop/GPU. Clipboard, 3D, and figure-removal scenarios all passed cleanly once the QA agent ran against a real display instead of the sandbox.
- Stable remaining signal still maps to existing open issues: frame-time spikes during heavy resize/multi-window churn and RSS retention in `massive_datasets` (`+53MB`) plus `command_exhaustion` (`+63MB`), with deterministic-only `multi_window_lifecycle` (`+23MB`).
- H4-style ~1s acquire stalls were not reproduced on the RTX 3080 Ti in this session. That reduces urgency for the exact earlier repro path, but it does not close H4 because resize churn still generates real spikes.
- Validation monitoring was inactive in both real desktop runs, so "0 validation errors" here only means "not monitored", not "V2 class issues disproved".
- Full `ctest --test-dir build --output-on-failure` is broadly red and later hung in `unit_test_param_editor_panel`; targeted tests around the touched paths passed (`unit_test_figure_serializer`, `unit_test_multi_window`, `unit_test_window_manager`, `unit_test_series_clipboard`).

---

## P1 — Important Improvements

### 4. Add Vulkan Validation Layer Monitoring
- **Problem:** QA agent doesn't check for Vulkan validation errors. The `ValidationGuard` from `tests/util/validation_guard.hpp` exists but isn't used.
- **Fix:** Wrap the entire QA session in a `ValidationGuard`. Check error count after each scenario and at end of fuzzing. Report any validation errors as `IssueSeverity::Error`.
- **Priority:** P1 — this is a primary goal of the QA agent.

### 5. ~~Separate Issue Categories in Report~~ ✅ FIXED
- **Problem:** Report is a flat list of 93 identical-looking warnings. Hard to scan.
- **Fix applied:** Text report now has: (a) Issue Summary section grouped by category with severity counts and frame ranges. (b) Issue Details section showing first 5 per category + "... and N more" for overflow. JSON report unchanged (still flat for machine parsing).
- **Priority:** P1 — ✅ Done

### 6. Non-Deterministic Crash (Run 1 crashed, Run 2 didn't)
- **Problem:** Same seed produced different outcomes. The crash is likely a race condition or use-after-free triggered by timing differences.
- **Fix:** (a) Run with AddressSanitizer (`-fsanitize=address`) to catch UAF/buffer overflows. (b) Add a `--repeat N` flag to run the same seed N times and report crash rate. (c) Add a `--sanitizer-friendly` mode that disables screenshot capture (which does GPU readback that may race).
- **Priority:** P1 — need to reproduce and diagnose.

### 7. Memory Growth Tracking Needs Baseline Stabilization
- **Problem:** RSS grows 80-115MB over session. Unclear if this is leak or expected (figure data, GPU allocations, ImGui atlas).
- **Progress (2026-03-01):** Reduced retained RSS contamination by tearing down windows/figures after `command_exhaustion` in `tests/qa/qa_agent.cpp`; full `--no-fuzz` peak dropped `529MB -> 337MB` for seed 42. Follow-up run remains `+160MB` (`178->338`) with targeted isolation showing `command_exhaustion` at `+115MB` and other sampled scenarios at `0–4MB`.
- **Progress update (2026-03-05):** QA reports now record per-scenario RSS and GPU device-local deltas directly in both `qa_report.txt` and `qa_report.json`, with automatic warnings above `+20MB` RSS and `+5MB` GPU-local retention. Smoke verified on `rapid_figure_lifecycle` (`seed 42`).
- **Remaining fix:** (a) Take RSS baseline after first 100 frames. (b) Add a dedicated `--leak-check` summary mode. (c) Add true headless/virtual-display coverage so window-dependent scenarios such as `command_exhaustion` can be profiled in sandbox/CI.
- **Priority:** P1 — 🟡 In progress.

### 16. Add Acquire/Present Stall Diagnostics for ~1s Hitches
- **Problem (Session 7, seed 1771959053):** repeated ~1s frame stalls (max 1021.9ms) with profiler showing `begin_frame`/`vk_acquire` p95 near 1s.
- **Fix:** (a) Record per-window acquire/present wait times each frame. (b) Emit warnings for waits >100ms and >500ms with window id and action context. (c) Include swapchain recreate counters and `move_figure` failure correlation in report.
- **Priority:** P1 — major user-visible freeze risk.

### 17. Add Overlay Crash Signature Classification
- **Problem (Session 10, seed 42):** stale-figure crashes surfaced as different top stacks (`LegendInteraction::draw`, `Crosshair::draw_all_axes`) but share one root cause class. Manual triage had to infer the common pattern.
- **Fix:** Add crash-signature grouping in QA agent report: classify stacks touching `DataInteraction::draw_overlays`, `LegendInteraction`, or `Crosshair::draw_all_axes` under one `overlay_stale_figure` bucket, and emit targeted repro hints.
- **Priority:** P1 — faster root-cause convergence for UAF/stale-pointer families.

### 18. Detect Sandboxed or Invalid Display Environments Before GLFW QA
- **Problem (Session 11, 2026-03-05):** sandboxed runs produced false GLFW/display failures, bogus clipboard/3D crash signatures, and misleading SIGSEGV triage. The same workflow passed cleanly on the real desktop/GPU.
- **Fix:** Add a startup environment probe for `DISPLAY`/`WAYLAND_DISPLAY` plus a minimal GLFW create-show-pump smoke check. If the environment is invalid or sandboxed, fail fast or auto-skip GLFW-dependent scenarios and report `environment_invalid` instead of a product failure.
- **Priority:** P1 — protects QA signal integrity and avoids chasing fake crashes.

---

## P2 — Enhancements

### 8. Add Headless Mode Support
- **Problem:** QA agent requires a display (GLFW window). Can't run in CI.
- **Fix:** Add `--headless` flag that sets `AppConfig::headless = true`. Skip GLFW-dependent scenarios. Still exercises rendering pipeline, command system, data operations.
- **Priority:** P2 — enables CI integration.

### 9. Add Per-Scenario Timing and Metrics
- **Problem:** Report only shows global frame stats. Can't tell which scenario is slow.
- **Fix:** Track per-scenario: frame count, avg/p95/max frame time, RSS delta, issue count. Add scenario breakdown table to report.
- **Priority:** P2 — better diagnostics.

### 10. Add Fuzz Action Distribution Logging
- **Problem:** Can't tell what the fuzzer was doing when a spike or crash occurred.
- **Fix:** Log action distribution histogram at end. Also maintain a ring buffer of last 20 actions so crash handler can dump recent action history.
- **Priority:** P2 — crash diagnosis.

### 11. Add `--scenario-only <name>` Isolation Mode
- **Problem:** Running all scenarios + fuzzing makes it hard to isolate issues.
- **Fix:** Already have `--scenario <name>` but it still runs fuzzing. Add `--no-fuzz` combination documentation. Also add `--fuzz-only` to skip scenarios.
- **Priority:** P2 — workflow convenience.

### 12. Swapchain Recreation Storm Detection
- **Problem:** Logs show rapid cascading swapchain recreations (OUT_OF_DATE → recreate → OUT_OF_DATE again). This is a known compositor issue but should be monitored.
- **Fix:** Count swapchain recreations per frame. Flag if > 2 recreations in a single frame as a warning.
- **Priority:** P2 — Vulkan health monitoring.

### 13. Add GPU Memory Tracking
- **Problem:** Only tracking RSS (CPU memory). GPU memory leaks are invisible.
- **Progress (2026-03-01):**
  - Added `VulkanBackend::query_gpu_memory_stats()` using VMA `vmaGetHeapBudgets()`.
  - Enabled optional `VK_EXT_memory_budget` extension in Vulkan device creation when available.
  - QA reports now include GPU memory metrics in both `qa_report.txt` and `qa_report.json` (initial/peak usage + budget for all heaps and device-local heaps).
  - Smoke verified with `--scenario rapid_figure_lifecycle --no-fuzz` (`seed 42`), reporting device-local `28MB` usage against `10966MB` budget.
- **Progress update (2026-03-01 follow-up):** Full `--no-fuzz --duration 60` and targeted isolation runs now completed. Device-local GPU usage stayed flat (`28MB -> 28MB`) including `command_exhaustion`.
- **Progress update (2026-03-05):** Per-scenario device-local GPU deltas are now emitted at scenario teardown in both report formats, so GPU retention regressions surface without manual VMA-budget greps.
- **Remaining fix:** Extend validation to known multi-window validation-error paths (`window_resize_glfw`, `window_drag_stress`) after Vulkan layout issues are addressed.
- **Priority:** P2 — 🟡 In progress.

### 14. Improve Resize Stress Scenario
- **Problem:** Current resize_stress just pumps frames — can't inject GLFW window resizes programmatically.
- **Fix:** Use `glfwSetWindowSize()` via the `GlfwAdapter` or `WindowManager` to actually trigger resize events. Test extreme sizes (1×1, 4096×4096), rapid resize sequences, and resize during animation.
- **Priority:** P2 — resize is a known fragile path.

---

## Session 3 Gaps (2026-02-26)

- [x] **Popup cleanup after screenshot** — The QA agent opens UI popups (context menus, command palette) for screenshots but didn't dismiss them, causing stale overlay in subsequent captures. Fixed by adding `close_tab_context_menu()` API. **Recommendation:** Any future scenario that opens a popup/modal should explicitly close it after capture.
- [x] **Empty figure content in detach scenarios** — Detach-to-window scenario used a figure with empty axes. Fixed by adding data before detach. **Recommendation:** All multi-window scenarios should ensure target figures have visible content.

## Backlog (Future)

- [ ] Add `--json-only` flag for CI consumption
- [ ] Add `--fail-on-warning` flag for strict mode
- [ ] Add figure screenshot comparison (golden image regression)
- [ ] Add multi-window stress scenario (open 5+ windows, close randomly)
- [ ] Add tab drag scenario (programmatic tab detach/reattach)
- [ ] Add theme switching stress test
- [ ] Add CSV import stress test (large files, malformed data)
- [ ] Add 3D-specific scenarios (orbit, surface rendering, transparency)
- [ ] Integration with Vulkan validation layer message callback (real-time, not just guard)
- [ ] Add `--profile` flag that enables frame profiler overlay data capture
