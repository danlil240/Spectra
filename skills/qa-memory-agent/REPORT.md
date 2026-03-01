# QA Memory Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | 2026-03-01 19:37 |
| Last mode | ASan + RSS isolation (`--no-fuzz --duration 60`) + targeted per-scenario isolation |
| Last seed | `42` |
| ASan/LSan result | No Spectra UAF/leak observed; known external `libdbus` leak persists (`2134` bytes) |
| RSS delta (full session) | +160MB (`178MB -> 338MB`) |
| Highest per-scenario RSS delta | `command_exhaustion`: +115MB (`178MB -> 293MB`) |
| Open issues | M1 (RSS growth) 🟡 Open, #7 (baseline stabilization) 🟡 In progress, #13 (GPU tracking) 🟡 In progress |
| SKILL.md last self-updated | 2026-02-26 (initial creation) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-02-26 | Initial file created | New agent created |
| 2026-03-01 | Current Status + Session log | Added and smoke-verified GPU telemetry via `vmaGetHeapBudgets` |
| 2026-03-01 | Current Status + Session log | Added ASan/RSS isolation metrics and fixed `--list-scenarios` teardown crash in QA harness |

---

## Session 2026-03-01 19:37

**Run config**
- Seed: `42`
- Mode:
  - ASan: `--no-fuzz --duration 60`
  - RSS: `--no-fuzz --duration 60`
  - Targeted isolation: `--scenario <name> --no-fuzz`
- Output dirs:
  - `/tmp/spectra_qa_memory_asan_20260301`
  - `/tmp/spectra_qa_memory_scenarios_20260301`
  - `/tmp/spectra_qa_mem_<scenario>_20260301`

**Session summaries**
- ASan run: `62.5s`, `1188` frames, `6/6` scenarios passed, RSS `421MB -> 608MB` (+187MB with sanitizer overhead), GPU device-local `28MB -> 28MB`.
- RSS run: `78.1s`, `2492` frames, `16/16` scenarios passed, RSS `178MB -> 338MB` (+160MB), GPU device-local `28MB -> 28MB`.

**Per-scenario RSS deltas (targeted isolation)**
| Scenario | RSS start (MB) | RSS peak (MB) | Delta (MB) | GPU device-local delta (MB) | Status |
|---|---|---|---|---|---|
| rapid_figure_lifecycle | 178 | 182 | +4 | +0 | ✅ |
| massive_datasets | 178 | 178 | +0 | +0 | ✅ |
| undo_redo_stress | 178 | 181 | +3 | +0 | ✅ |
| command_exhaustion | 178 | 293 | +115 | +0 | ⚠️ dominant |
| series_clipboard_selection | 178 | 181 | +3 | +0 | ✅ |
| figure_serialization | 178 | 178 | +0 | +0 | ✅ |
| series_removed_interaction_safety | 178 | 178 | +0 | +0 | ✅ |

**ASan/LSan summary**
- No Spectra heap UAF/leak found in this session's scenario run.
- Known external leak remains:
  - `libdbus`: `2134` bytes in `3` allocations.

**Leaks found**
| Category | File | Description | Fixed? |
|---|---|---|---|
| Teardown crash (QA harness) | `tests/qa/qa_agent.cpp` | `--list-scenarios` returned before `shutdown_runtime()`, causing GLFW window double-destroy during object teardown. | ✅ |

**Leaks fixed**
- `tests/qa/qa_agent.cpp`
  - In `QAAgent::run()`, `--list-scenarios` now performs:
    - `app_->shutdown_runtime();`
    - `app_.reset();`
  - Verification:
    - `./build/tests/spectra_qa_agent --list-scenarios` exits `0` with clean shutdown logs.
    - ASan no longer crashes on this path (only known external `libdbus` leak remains).

**Open issues updated**
| ID | Old status | New status | Notes |
|---|---|---|---|
| M1 | 🟡 Open | 🟡 Open | Full scenarios-only run still at `+160MB`; targeted isolation pins dominant growth to `command_exhaustion` (`+115MB`). |
| #7 | 🟡 In progress | 🟡 In progress | Per-scenario isolation is now reproducible and narrowed; baseline still above target in long runs. |
| #13 | 🟡 In progress | 🟡 In progress | GPU device-local usage stayed flat (`+0MB`) in all targeted scenarios. |

**Self-updates to SKILL.md**
- none

---

## Session 2026-03-01 19:28

**Run config**
- Seed: `42`
- Mode: `--scenario rapid_figure_lifecycle --no-fuzz --duration 8`
- Tool: QA agent smoke validation (build + runtime report checks)
- Output dirs:
  - `/tmp/spectra_qa_gpu_smoke`
  - `/tmp/spectra_qa_gpu_smoke_scenario`

**Per-scenario RSS deltas**
| Scenario | RSS start (MB) | RSS end/peak (MB) | Delta (MB) | Status |
|---|---|---|---|---|
| rapid_figure_lifecycle | 178 | 182 | +4 | ✅ |

**ASan/LSan summary**
- Not run in this session (telemetry implementation + smoke verification only).

**GPU memory telemetry**
- Added VMA budget polling in `VulkanBackend::query_gpu_memory_stats()` via `vmaGetHeapBudgets`.
- Enabled optional `VK_EXT_memory_budget` during Vulkan logical device creation when supported.
- Added QA report output:
  - Text report now includes initial/peak/budget GPU memory for all heaps and device-local heaps.
  - JSON report now includes GPU memory tracking flags and MB metrics.
- Smoke run result (`seed 42`):
  - Device-local GPU memory: `28MB` initial, `28MB` peak, `10966MB` budget.
  - All-heaps GPU memory: `31MB` initial, `31MB` peak, `34828MB` budget.

**Leaks found**
| Category | File | Description | Fixed? |
|---|---|---|---|
| none | — | No memory safety issues were investigated in this smoke session. | n/a |

**Open issues updated**
| ID | Old status | New status | Notes |
|---|---|---|---|
| #13 | 🟡 Open | 🟡 In progress | GPU memory telemetry is implemented and emitting data in QA reports; needs full leak-check session integration follow-up. |

**Self-updates to SKILL.md**
- none

---

## Session 2026-03-01 18:01

**Run config**
- Seed: `42`
- Mode: `--no-fuzz` (full scenarios, 60s) + per-scenario isolation
- Tool: ASan/LSan + RSS-only
- Output dirs:
  - `/tmp/spectra_qa_memory_asan`
  - `/tmp/spectra_qa_memory_scenarios`
  - `/tmp/spectra_qa_memory_scenarios_after`
  - `/tmp/spectra_qa_mem_*`

**Per-scenario RSS deltas**
| Scenario | RSS start (MB) | RSS end/peak (MB) | Delta (MB) | Status |
|---|---|---|---|---|
| rapid_figure_lifecycle | 188 | 192 | +4 | ✅ |
| massive_datasets | 188 | 188 | +0 | ✅ |
| undo_redo_stress | 188 | 191 | +3 | ✅ |
| series_clipboard_selection | 188 | 191 | +3 | ✅ |
| figure_serialization | 188 | 188 | +0 | ✅ |
| series_removed_interaction_safety | 188 | 188 | +0 | ✅ |
| command_exhaustion (before fix) | 188 | 354 | +166 | ⚠️ |
| command_exhaustion (after fix) | 178 | 293 | +115 | ⚠️ improved |

**ASan/LSan summary**
- `ASan`:
  - Found UAF regression introduced by cleanup helper in `tests/qa/qa_agent.cpp` (stale `DataInteraction` figure cache).
  - Fixed by explicit cache invalidation for all window UI contexts before closing windows.
  - Re-run confirmed no UAF.
- `LSan`:
  - Residual external leak in `libdbus` (`2002` bytes in `3` allocations) remains.

**Leaks found**
| Category | File | Description | Fixed? |
|---|---|---|---|
| UAF (QA harness) | `tests/qa/qa_agent.cpp` | Closing secondary windows without invalidating figure caches caused stale pointer dereference in `DataInteraction::draw_legend_for_figure`. | ✅ |

**Leaks fixed**
- `tests/qa/qa_agent.cpp`
  - Added `reset_to_single_window_lightweight_state()` after `scenario_command_exhaustion()`.
  - Explicitly clears cached figure references across all window UI contexts before `WindowManager::process_pending_closes()`.
  - Closes extra windows and collapses to one lightweight figure to reduce retained allocations between scenarios.

**Open issues updated**
| ID | Old status | New status | Notes |
|---|---|---|---|
| M1 | 🟡 Open | 🟡 Open | Full session improved from `+341MB` (`188->529`) to `+159MB` (`178->337`) but still above target. |
| #7 | 🟡 Open | 🟡 In progress | Scenario contamination reduced by teardown after `command_exhaustion`; still needs formal baseline/per-scenario reporting enhancements. |
| #13 | 🟡 Open | 🟡 Open | `vmaGetHeapBudgets` instrumentation not added in this session. |

**Self-updates to SKILL.md**
- none

---

<!-- ============================================================ -->
<!-- SESSION TEMPLATE                                             -->
<!-- ============================================================ -->
<!--
## Session YYYY-MM-DD HH:MM

**Run config**
- Seed: `42`
- Mode: `--no-fuzz` (isolation)
- Tool: ASan / Valgrind Massif / RSS-only
- Output dir: `/tmp/spectra_qa_memory_YYYYMMDD`

**Per-scenario RSS deltas**
| Scenario | RSS start (MB) | RSS end (MB) | Delta (MB) | Status |
|---|---|---|---|---|
| rapid_figure_lifecycle | ? | ? | ? | ✅ / ⚠️ |
| massive_datasets | ? | ? | ? | ✅ / ⚠️ |
| figure_serialization | ? | ? | ? | ✅ / ⚠️ |

**ASan/LSan summary**
- Leaks: 0
- UAF: 0
- Heap errors: 0

**Leaks found**
| Category | File | Description | Fixed? |
|---|---|---|---|

**Open issues updated**
| ID | Old status | New status | Notes |
|---|---|---|---|

**Self-updates to SKILL.md**
- none
-->
