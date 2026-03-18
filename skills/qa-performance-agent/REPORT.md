# QA Performance Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | 2026-03-06 |
| Last seed | 26349518760598 (random regression), 42 (deterministic verify), 25257321839906 (random), 99 (Nvidia driver crash) |
| Last exit code | 0 (random regression) |
| Scenarios passing | 20/20 (seed 42), 20/20 (seed 25257321839906), 20/20 (seed 26349518760598) |
| Frame time P95 | 41.3ms (seed 26349518760598) |
| Frame time max | 236.4ms (seed 26349518760598) |
| RSS delta | +191MB (seed 26349518760598), +245MB (seed 42) |
| Open CRITICAL | 1 (H4 device lost — known, bounded) |
| Open ERROR | 0 |
| Open issues (QA_results.md) | H1, H3, H4, M1, V1, V2, Q1 |
| SKILL.md last self-updated | 2026-03-06 (Interpretation Rules) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-03-06 | Interpretation Rules | Added clipboard/paste overflow and dangling-marker crash signatures from C7/C8/C9 |
| 2026-03-01 | Interpretation Rules | Added stale-overlay crash signature heuristic from seed-42 repro/fix cycle |
| 2026-02-26 | Initial file created | Consolidation session |

---

## Session 2026-03-06

**Run config**
- Deterministic baseline: `--seed 42 --duration 120 --output-dir /tmp/spectra_qa_perf`
- Randomized pass: `--duration 60 --output-dir /tmp/spectra_qa_perf` (seed 25257321839906)
- Verification seed 99: `--seed 99 --duration 120 --output-dir /tmp/spectra_qa_perf`
- Regression random: `--duration 60 --output-dir /tmp/spectra_qa_perf` (seed 26349518760598)
- Build/tests: `cmake --build build -j$(nproc)`, `ctest --test-dir build -LE gpu --output-on-failure`

**Summary**
- Initial seed 42 run crashed (exit 2) with X11 BadLength at frame 3059 from oversized clipboard write (C7).
- After C7 fix, crashed again with `std::bad_alloc` from unbounded `series.paste` (C8).
- After C8 fix, crashed with SIGABRT from dangling `Series*` in `file.save_workspace` marker serialization (C9).
- After all three fixes, seed 42 completed successfully: 6314 frames, 20/20 scenarios, exit 1 (1 CRITICAL = known H4 device lost).
- Seed 99 verification: SIGSEGV at frame 4960 — Nvidia driver crash inside `libnvidia-glcore.so` during swapchain destruction. Not fixable at app level.
- Random seed 25257321839906: clean exit 0, 3774 frames, 20/20 scenarios, 0 CRITICAL.
- Regression random seed 26349518760598: clean exit 0, 4470 frames, 20/20 scenarios, 0 CRITICAL.
- Unit tests: 110/111 pass (pre-existing `DesignTokens.LayoutConstants` theme failure).

**Performance metrics**
- Seed 42 deterministic: avg `10.5ms`, p95 `36.2ms`, p99 n/a, max `119.4ms`, RSS `198→443MB` (`+245MB`), issues: 1 CRITICAL (H4 device lost at frame 6314).
- Seed 25257321839906 random: avg `13.6ms`, p95 `44.4ms`, max `125.8ms`, RSS `198→440MB` (`+242MB`), issues: 0 CRITICAL, 0 ERROR.
- Seed 26349518760598 regression: avg `11.3ms`, p95 `41.3ms`, max `236.4ms`, RSS `198→389MB` (`+191MB`), issues: 0 CRITICAL, 0 ERROR.

**Root cause + fix**
- C7: X11 `BadLength` from `glfwSetClipboardString` exceeding 4MB. Fix: clipboard size guard in `register_commands.cpp`.
- C8: `std::bad_alloc` from unbounded `series.paste`. Fix: 200-series-per-axes paste limit in `register_commands.cpp`.
- C9: dangling `Series*` in `DataInteraction::markers_` accessed by `file.save_workspace`. Fix: (a) `markers_.clear()` in `clear_figure_cache()` in `data_interaction.hpp`, (b) use `m.series_label` instead of `m.series->label()` in `register_commands.cpp`.
- Nvidia driver SIGSEGV (seed 99): not fixable — crash inside `libnvidia-glcore.so.580.126.20`.

**Files changed**
- `src/ui/app/register_commands.cpp` (C7 clipboard guard, C8 paste limit, C9 series_label)
- `src/ui/overlay/data_interaction.hpp` (C9 markers_.clear())
- `plans/QA_results.md`
- `plans/QA_update.md`
- `skills/qa-performance-agent/SKILL.md`

**Test status**
- `ctest --test-dir build -LE gpu --output-on-failure`: 110/111 pass; known theme test failure.

**Self-updates to SKILL.md**
- Added interpretation rules for clipboard overflow (C7), paste accumulation (C8), and dangling-marker save/load (C9) crash signatures.

## Self-Improvement — 2026-03-06
Improvement: Added three interpretation rules for new crash signature classes: X11 clipboard overflow crashes, unbounded series accumulation OOM, and dangling-pointer access in save/load command paths.
Motivation: Prior guidance only covered overlay stale-figure crashes; these three new crash classes required manual triage because the SKILL.md had no recognition pattern for clipboard size limits, paste accumulation pressure, or command-lambda pointer safety in save/load paths.
Change: `skills/qa-performance-agent/SKILL.md` updated under `## Interpretation Rules` with three new bullets.
Next gap: Add a `workspace_save_load_stress` scenario (PERF-I9) to specifically stress `file.save_workspace` / `file.load_workspace` under concurrent figure churn — this session's C9 crash would have been caught earlier by a dedicated scenario.

---

## Session 2026-03-01 20:34

**Run config**
- Pre-fix crash repro: `--seed 42 --duration 120 --output-dir /tmp/spectra_qa_perf_20260301_seed42_after2`
- Deterministic verify: `--seed 42 --duration 120 --output-dir /tmp/spectra_qa_perf_20260301_seed42_after3`
- Randomized verify: `--duration 60 --output-dir /tmp/spectra_qa_perf_20260301_random_after`
- Build/tests: `cmake --build build`, `ctest --test-dir build --output-on-failure`

**Summary**
- Pre-fix repro crashed (`exit 2`) at frame ~3606 with overlay stacks in `LegendInteraction::draw` / `Crosshair::draw_all_axes`.
- After fix, same deterministic seed (`42`) completed without crash (`exit 1`), `20/20` scenarios passed, `0` CRITICAL.
- Randomized run (seed `27243840318184`) completed without crash (`exit 1`), `16/16` scenarios passed, `0` CRITICAL.

**Performance metrics**
- Seed 42 deterministic verify: avg `24.93ms`, p95 `83.40ms`, p99 `238.15ms`, max `336.01ms`, RSS `178→355MB` (`+177MB`), issues: `134 WARNING frame_time`, `51 WARNING memory`, `12 ERROR vulkan_validation`.
- Seed 27243840318184 random verify: avg `29.79ms`, p95 `146.06ms`, p99 `258.75ms`, max `427.76ms`, RSS `178→305MB` (`+127MB`), issues: `96 WARNING frame_time`, `13 WARNING memory`, `11 ERROR vulkan_validation`.

**Root cause + fix**
- Root cause class: stale figure/axes pointers consumed by overlay rendering during multi-window churn.
- Product fixes:
  - per-window cache invalidation fanout via `WindowManager::clear_figure_caches(Figure*)`
  - `app_step` close callback routing through window manager cache clear
  - stale-axes validation in `InputHandler::on_scroll()`
  - pre-`build_ui` active-figure sync in `WindowRuntime::update()`
  - `DataInteraction::draw_overlays()` now takes current frame figure from `ImGuiIntegration::build_ui()`

**Files changed**
- `src/ui/window/window_manager.hpp`
- `src/ui/window/window_manager.cpp`
- `src/ui/app/app_step.cpp`
- `src/ui/input/input.cpp`
- `src/ui/app/window_runtime.cpp`
- `src/ui/overlay/data_interaction.hpp`
- `src/ui/overlay/data_interaction.cpp`
- `src/ui/imgui/imgui_integration.cpp`
- `plans/QA_results.md`
- `plans/QA_update.md`
- `skills/qa-performance-agent/SKILL.md`

**Test status**
- `ctest --test-dir build --output-on-failure`: 83/85 pass; known golden failures remain (`golden_image_tests`, `golden_image_tests_3d`).

**Self-updates to SKILL.md**
- Added one new `Interpretation Rules` bullet for stale-overlay crash signatures (`LegendInteraction::draw` / `Crosshair::draw_all_axes`).

## Self-Improvement — 2026-03-01
Improvement: Added a stale-overlay crash signature interpretation rule that maps `LegendInteraction::draw` and `Crosshair::draw_all_axes` SIGSEGVs to cache/sync invalidation paths.
Motivation: Prior guidance treated these as separate crashes, which slows triage when the same stale-figure root cause appears under different top frames.
Change: `skills/qa-performance-agent/SKILL.md` updated under `## Interpretation Rules` with one new signature-based rule.
Next gap: Add automated crash-stack bucketing in `qa_report.txt` so repeated root-cause classes are grouped without manual symbol inspection.

---

## Session 2026-02-28 12:32

**Run config**
- Seed: `1772273948` (regression seed)
- Duration: `60s`
- Mode: full (scenarios + fuzz)
- Output dir: `/tmp/spectra_qa_perf_20260228_regression_after`
- Exit code: `0`

**Scenario results**
- 6/6 scenarios passed (wall-clock limited)
- 0 failed

**Performance metrics**
- Frame time avg: 21.3ms
- Frame time P95: 171.9ms
- Frame time max: 311.4ms
- Spikes: 25
- RSS baseline: 169MB
- RSS peak: 290MB
- RSS delta: +121MB
- CRITICAL findings: 0
- ERROR findings: 0

**Analysis**
- No serialization failures observed on regression seed after `FigureSerializer::load()` fix.
- Known warning profile remains (frame-time spikes + memory growth), consistent with open issues H1/H4/M1.
- Validation warning pattern V2 still appears in multi-window churn (existing open issue).

**Fixes applied**
- None in this run (verification-only)

**Self-updates to SKILL.md**
- None

---

## Session 2026-02-28 12:19

**Run config**
- Baseline deterministic: `--seed 42 --duration 120 --output-dir /tmp/spectra_qa_perf_20260228_seed42`
- Initial randomized pass: `--duration 60 --output-dir /tmp/spectra_qa_perf_20260228_random`
- Repro (before fix): `--seed 13062186744256 --scenario figure_serialization --no-fuzz`
- Verify after fix: `--seed 13062186744256 --scenario figure_serialization --no-fuzz`
- Regression verification: `--seed 13062186744256 --duration 60 --output-dir /tmp/spectra_qa_perf_20260228_seed13062186744256_after`
- Build/tests: `cmake --build build`, `ctest -R unit_test_figure_serializer`

**Summary**
- Baseline seed 42 run: exit `0`, 6/6 scenarios passed (wall-clock limited), no ERROR/CRITICAL.
- Randomized run (pre-fix): exit `1`, 18/19 scenarios passed; `figure_serialization` failed with:
  - `[ERROR] Loaded figure has no axes`
  - `[ERROR] figure_serialization FAILED`
- Repro command failed pre-fix (exit `1`) and passed post-fix (exit `0`).
- Full verification run after fix (same seed): exit `0`, 13/13 scenarios passed (wall-clock limited), no ERROR/CRITICAL.

**Performance metrics**
- Seed 42 baseline: avg 28.4ms, p95 263.1ms, p99 291.9ms, max 428.3ms, RSS +121MB.
- Seed 13062186744256 pre-fix: avg 15.0ms, p95 8.45ms, p99 261.0ms, max 1012.5ms, RSS +98MB, 2 ERROR.
- Seed 13062186744256 post-fix verification: avg 15.6ms, p95 8.5ms, p99 289.3ms, max 1001.2ms, RSS +63MB, 0 ERROR.

**Root cause + fix**
- Root cause in `FigureSerializer::load()` stream parsing:
  1. `TAG_SUBPLOT_GRID` wrote `axes_count` but loader did not consume it.
  2. `TAG_AXES_2D` wrote `autoscale_mode` byte but loader did not consume it.
- Both omissions desynchronized chunk parsing and could skip axes reconstruction, triggering `Loaded figure has no axes`.
- Fixes:
  - Consume `axes_count` in `TAG_SUBPLOT_GRID`.
  - Consume and restore `saved_autoscale` in `TAG_AXES_2D`.
  - Added regression test `unit_test_figure_serializer` (`SaveLoadRestores2DAxesAndSeries`).

**Files changed**
- `src/ui/workspace/figure_serializer.cpp`
- `tests/unit/test_figure_serializer.cpp` (new)
- `tests/CMakeLists.txt`

**Self-updates to SKILL.md**
- None

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
