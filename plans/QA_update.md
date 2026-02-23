# QA Agent — Improvement Backlog

> Living document. Updated after each QA session with agent improvements to implement.
> Last updated: 2026-02-23 | Session seeds: 42, 12345

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
- **Fix:** (a) Take RSS baseline after first 100 frames (after init stabilizes). (b) Track RSS per-scenario to identify which scenarios cause growth. (c) Add a `--leak-check` mode that runs scenarios in isolation and reports per-scenario RSS delta.
- **Priority:** P1 — need to distinguish leaks from expected growth.

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
- **Fix:** Query `VmaBudget` via VMA's `vmaGetHeapBudgets()` periodically. Report GPU memory usage and growth.
- **Priority:** P2 — GPU resource monitoring.

### 14. Improve Resize Stress Scenario
- **Problem:** Current resize_stress just pumps frames — can't inject GLFW window resizes programmatically.
- **Fix:** Use `glfwSetWindowSize()` via the `GlfwAdapter` or `WindowManager` to actually trigger resize events. Test extreme sizes (1×1, 4096×4096), rapid resize sequences, and resize during animation.
- **Priority:** P2 — resize is a known fragile path.

---

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
