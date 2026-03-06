# QA Memory Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | 2026-03-05 22:44 |
| Last mode | RSS smoke (`rapid_figure_lifecycle`) + ASan smoke (`rapid_figure_lifecycle`, `detect_leaks=0`) |
| Last seed | `42` |
| ASan/LSan result | ASan smoke clean for `rapid_figure_lifecycle`; LSan blocked by ptrace in this tool environment |
| RSS delta (full session) | n/a this session (`single-scenario smoke only`) |
| Highest per-scenario RSS delta | `rapid_figure_lifecycle`: `+0MB` RSS smoke, `+3MB` ASan smoke |
| Open issues | M1 (RSS growth) 🟡 Open, #7 (baseline stabilization) 🟡 In progress, #13 (GPU tracking) 🟡 In progress |
| SKILL.md last self-updated | 2026-02-26 (initial creation) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-02-26 | Initial file created | New agent created |
| 2026-03-01 | Current Status + Session log | Added and smoke-verified GPU telemetry via `vmaGetHeapBudgets` |
| 2026-03-01 | Current Status + Session log | Added ASan/RSS isolation metrics and fixed `--list-scenarios` teardown crash in QA harness |
| 2026-03-05 | Current Status + Session log | Added per-scenario RSS/GPU retention telemetry to QA reports and verified smoke output |

---

## Session 2026-03-05 22:44

**Run config**
- Seed: `42`
- Mode:
  - RSS smoke: `--scenario rapid_figure_lifecycle --no-fuzz --duration 20`
  - ASan smoke: same scenario in `build-asan`
  - ASan verification rerun: `ASAN_OPTIONS=detect_leaks=0` because LSan aborts under ptrace in this tool environment
  - Attempted targeted isolation: `command_exhaustion` (blocked by missing GLFW display in sandbox)
- Output dirs:
  - `/tmp/spectra_qa_memory_20260305_rapid`
  - `/tmp/spectra_qa_memory_20260305_asan_rapid`
  - `/tmp/spectra_qa_memory_20260305_asan_nolsan_rapid`
  - `/tmp/spectra_qa_memory_20260305_command_exhaustion`

**Session summaries**
- RSS smoke run: `0.13s`, `120` frames, `1/1` scenario passed, RSS `79MB -> 80MB` (`+0MB` by report rounding), GPU device-local `22090MB -> 22090MB`.
- ASan smoke run (`detect_leaks=0`): `4.68s`, `120` frames, `1/1` scenario passed, RSS `391MB -> 395MB` (`+3MB`), GPU device-local `22126MB -> 22126MB`.
- Raw LSan attempt reached report generation, then exited because LeakSanitizer cannot operate under ptrace in this tool environment.

**Per-scenario RSS deltas**
| Scenario | Run | RSS start (MB) | RSS end/peak (MB) | Delta (MB) | GPU device-local delta (MB) | Status |
|---|---|---|---|---|---|---|
| rapid_figure_lifecycle | RSS smoke | 79 | 80 | +0 | +0 | ✅ |
| rapid_figure_lifecycle | ASan smoke (`detect_leaks=0`) | 391 | 395 | +3 | +0 | ✅ |

**ASan/LSan summary**
- ASan: no Spectra UAF or heap error observed in `rapid_figure_lifecycle`.
- LSan: tool-environment limitation only; no trustworthy leak summary was produced because LeakSanitizer aborts under ptrace after process exit.

**Leaks found**
| Category | File | Description | Fixed? |
|---|---|---|---|
| none | — | No product memory leak was isolated in this smoke session. | n/a |

**Leaks fixed**
- none

**Agent improvement implemented**
- `tests/qa/qa_agent.cpp`
  - Wrapped every scenario with before/after RSS and GPU device-local snapshots.
  - Added per-scenario memory retention output to `qa_report.txt` and `qa_report.json`.
  - Emit `scenario_memory` warnings above `+20MB` RSS and `scenario_gpu_memory` warnings above `+5MB` device-local GPU retention.
- Verification:
  - `./build/tests/spectra_qa_agent --seed 42 --scenario rapid_figure_lifecycle --no-fuzz --duration 20 --output-dir /tmp/spectra_qa_memory_20260305_rapid`
  - `ASAN_OPTIONS=detect_leaks=0 ./build-asan/tests/spectra_qa_agent --seed 42 --scenario rapid_figure_lifecycle --no-fuzz --duration 20 --output-dir /tmp/spectra_qa_memory_20260305_asan_nolsan_rapid`

**Open issues updated**
| ID | Old status | New status | Notes |
|---|---|---|---|
| M1 | 🟡 Open | 🟡 Open | Full `command_exhaustion` revalidation is still pending, but per-scenario retention is now reported automatically in routine runs. |
| #7 | 🟡 In progress | 🟡 In progress | Subtask "track RSS per-scenario directly in report" is complete. |
| #13 | 🟡 In progress | 🟡 In progress | Per-scenario GPU device-local deltas now ship in both report formats. |
| #8 | P2 backlog | P2 backlog | Missing GLFW display blocked `command_exhaustion` validation here, reinforcing headless-mode need. |

**Self-updates to SKILL.md**
- none

---

## Self-Improvement — 2026-03-05
Improvement: Added automatic per-scenario RSS and GPU device-local retention reporting with warning thresholds to the QA agent report.
Motivation: Previous memory sessions depended on manual isolation loops, so routine runs could miss which scenario retained memory unless someone grepped ad hoc output.
Change: `tests/qa/qa_agent.cpp` now records per-scenario memory snapshots and writes them into `qa_report.txt` and `qa_report.json`.
Next gap: Add real headless or virtual-display coverage so window-dependent scenarios such as `command_exhaustion` can be profiled in CI and sandbox environments.

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
