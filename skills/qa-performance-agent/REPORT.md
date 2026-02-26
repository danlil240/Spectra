# QA Performance Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | 2026-02-26 14:35 |
| Last seed | 42 (deterministic), 23756320363876 (random) |
| Last exit code | 0 (both runs) |
| Scenarios passing | 16/16 (seed 42), 6/6 (random, wall-clock limited) |
| Frame time P99 | 352ms (seed 42) |
| Frame time max | 802ms (seed 42) |
| RSS delta | +232MB (seed 42), +122MB (random) |
| Open CRITICAL | 0 |
| Open ERROR | 0 |
| Open issues (QA_results.md) | H1, H3, H4, M1, V1, V2, Q1 |
| SKILL.md last self-updated | 2026-02-26 (initial consolidation) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-02-26 | Initial file created | Consolidation session |

---

## Session 2026-02-26 14:35

**Run config**
- Seed: `42` (deterministic) + `23756320363876` (randomized)
- Duration: `120s` (deterministic) + `60s` (randomized)
- Mode: full (scenarios + fuzz)
- Output dir: `/tmp/spectra_qa_perf_20260226`, `/tmp/spectra_qa_perf_20260226_random`
- Exit code: `0` (both runs)
- ctest: 78/78 pass

**Scenario results (seed 42)**
| Scenario | Result | Notes |
|---|---|---|
| rapid_figure_lifecycle | ✅ pass | |
| massive_datasets | ✅ pass | Frame spikes 194–352ms (H1 — large data upload) |
| undo_redo_stress | ✅ pass | |
| animation_stress | ✅ pass | |
| input_storm | ✅ pass | |
| command_exhaustion | ✅ pass | Multi-window creation, 470ms spike (frame 975) |
| series_mixing | ✅ pass | |
| mode_switching | ✅ pass | |
| stress_docking | ✅ pass | |
| resize_stress | ✅ pass | |
| 3d_zoom_then_rotate | ✅ pass | |
| window_resize_glfw | ✅ pass | Swapchain recreation storms (V1) observed |
| multi_window_lifecycle | ✅ pass | |
| tab_drag_between_windows | ✅ pass | |
| window_drag_stress | ✅ pass | |
| resize_marathon | ✅ pass | 520+ resize events across 7 phases |

**Scenario results (random seed 23756320363876, 60s wall-clock)**
| Scenario | Result | Notes |
|---|---|---|
| rapid_figure_lifecycle | ✅ pass | |
| massive_datasets | ✅ pass | |
| undo_redo_stress | ✅ pass | |
| animation_stress | ✅ pass | |
| input_storm | ✅ pass | |
| command_exhaustion | ✅ pass | Wall-clock limit hit after this scenario |

**Performance metrics (seed 42)**
- Frame time avg: 39.4ms
- Frame time P95: 74.5ms
- Frame time P99: 352ms
- Frame time max: 802ms (frame 1131, multi-window acquire cascade)
- Spikes (>3× avg): 25
- RSS baseline: 170MB
- RSS peak: 402MB
- RSS delta: +232MB
- CRITICAL findings: 0
- ERROR findings: 0
- Warnings: 50 (25 frame_time, 25 memory)

**Performance metrics (random seed)**
- Frame time avg: 39.3ms
- Frame time P95: 287.4ms
- Frame time max: 530.6ms
- Spikes: 17
- RSS baseline: 170MB
- RSS peak: 292MB
- RSS delta: +122MB
- CRITICAL findings: 0
- ERROR findings: 0
- Warnings: 19

**Comparison with previous sessions**
| Metric | Session 3 (seed 42) | This session (seed 42) | Delta |
|---|---|---|---|
| Scenarios | 16/16 | 16/16 | Same |
| Crashes | 0 | 0 | Same |
| Avg frame | 44.5ms | 39.4ms | -11% ✅ |
| Max frame | 1024ms | 802ms | -22% ✅ |
| P99 frame | — | 352ms | — |
| RSS delta | +261MB | +232MB | -11% ✅ |
| Warnings | 50 | 50 | Same |

**Analysis**
- No new bugs found. All warnings map to existing open issues (H1, H4, M1).
- Frame time max improved from 1024ms → 802ms vs. previous seed-42 session — fewer multi-window acquire cascades.
- RSS growth slightly improved (+232MB vs +261MB).
- `move_figure` cosmetic warning still present during fuzzing.
- Swapchain recreation storms (V1) still observable in resize scenarios.

**Fixes applied**
- None needed — clean run.

**Self-updates to SKILL.md**
- None

---

<!-- ============================================================ -->
<!-- SESSION TEMPLATE                                             -->
<!-- ============================================================ -->
<!--
## Session YYYY-MM-DD HH:MM

**Run config**
- Seed: `42`
- Duration: `120s`
- Mode: full (scenarios + fuzz)
- Output dir: `/tmp/spectra_qa_perf_YYYYMMDD`
- Exit code: `0`

**Scenario results**
| Scenario | Result | Notes |
|---|---|---|
| rapid_figure_lifecycle | ✅ pass | |
| massive_datasets | ✅ pass | |
| undo_redo_stress | ✅ pass | |
| animation_stress | ✅ pass | |
| input_storm | ✅ pass | |
| command_exhaustion | ✅ pass | |
| series_mixing | ✅ pass | |
| mode_switching | ✅ pass | |
| stress_docking | ✅ pass | |
| resize_stress | ✅ pass | |
| 3d_zoom_then_rotate | ✅ pass | |
| window_resize_glfw | ✅ pass | |
| multi_window_lifecycle | ✅ pass | |
| tab_drag_between_windows | ✅ pass | |
| window_drag_stress | ✅ pass | |
| resize_marathon | ✅ pass | |
| series_clipboard_selection | ✅ pass | |
| figure_serialization | ✅ pass | |
| series_removed_interaction_safety | ✅ pass | |
| line_culling_pan_zoom | ✅ pass | |

**Performance metrics**
- Frame time P99: ? ms
- Frame time max: ? ms
- RSS baseline: ? MB
- RSS peak: ? MB
- RSS delta: ? MB
- CRITICAL findings: 0
- ERROR findings: 0

**Fixes applied**
| Issue ID | File | Change |
|---|---|---|

**Self-updates to SKILL.md**
- none
-->
