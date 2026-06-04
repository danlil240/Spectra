---
name: qa-performance-agent
description: >-
  Runs Spectra stress/fuzz QA with spectra_qa_agent, triages qa_report, reproduces seed crashes, updates plans/QA_results.md. Use for stability, performance regressions, fuzz failures, or long-running QA sessions.
---

# QA Performance Agent

Drive reproducible QA sessions focused on crashes, frame-time regressions, and memory growth, then convert findings into verified fixes and updated living QA documents.

---

## Required Context

Before starting any task, read these living documents:
- `plans/QA_results.md` — open product bugs and previously observed regressions
- `plans/QA_update.md` — QA-agent capability gaps and backlog items

---

## Workflow

### 1. Build

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

### 2. Run (required — at least once per task)

```bash
# Deterministic baseline
./build/tests/spectra_qa_agent --seed 42 --duration 120 --output-dir /tmp/spectra_qa

# Randomized pass
./build/tests/spectra_qa_agent --duration 60 --output-dir /tmp/spectra_qa
```

### 3. Triage findings

1. Check exit code first: `0` (pass), `1` (issues), `2` (crash).
2. Read `qa_report.txt` summary — severity counts, frame stats, memory.
3. Prioritize `CRITICAL` → `ERROR` → warnings.
4. Inspect frame-time outliers (>50 ms = likely real stall).
5. Check RSS growth (>100 MB = suspicious unless large dataset intended).
6. Correlate anomalies with scenario/fuzz phase context.
7. Use `qa_report.json` for machine-readable diffing or CI scripting.

### 4. Reproduce deterministically

- Re-run with exact same `--seed`, `--duration`, and mode flags.
- For crashes (`exit 2`): preserve the seed from stderr and reproduce **before editing code**.
- Build with ASan for UAF/overflow diagnosis:

```bash
cmake -B build-asan -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=undefined"
cmake --build build-asan -j$(nproc)
./build-asan/tests/spectra_qa_agent --seed <crash-seed> --duration 120 \
    --output-dir /tmp/spectra_qa_asan
```

### 5. Apply minimal, targeted fixes

- Focus on root cause in the affected subsystem (use Issue-to-File Map below).
- Avoid unrelated refactors while triaging regressions.
- Add null guards before derefs; bound Vulkan timeouts; scope memory cleanup.

### 6. Verify

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Confirm fix with repro seed
./build/tests/spectra_qa_agent --seed <repro-seed> --duration 120 \
    --output-dir /tmp/spectra_qa_after

# Regression check with new seed
./build/tests/spectra_qa_agent --seed $(date +%s) --duration 120 \
    --output-dir /tmp/spectra_qa_regression
```

### 7. Update living documents

- `plans/QA_results.md` — add/update product bugs with root cause, fix summary, files changed, status.
- `plans/QA_update.md` — add QA-agent improvements needed.

---

## CLI Reference

```
--seed <N>           RNG seed (default: time-based)
--duration <sec>     Max wall-clock runtime (default: 120)
--scenario <name>    Run one named scenario
--fuzz-frames <N>    Fuzz frame count (default: 3000)
--output-dir <path>  Report/screenshot directory
--no-fuzz            Skip fuzzing phase
--no-scenarios       Skip scenarios phase
--design-review      Capture design-review screenshots (51 total)
--list-scenarios     Print scenario list and exit
```

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | No `ERROR` or `CRITICAL` findings |
| `1` | At least one `ERROR` or `CRITICAL` finding |
| `2` | Crash — seed printed to stderr |

---

## Scenario Coverage Map

| Scenario | Primary stress target |
|---|---|
| `rapid_figure_lifecycle` | Figure creation/switch/close stability |
| `massive_datasets` | 1M-point line + 5×100K series — GPU upload + render pressure |
| `undo_redo_stress` | Command history correctness and state churn |
| `animation_stress` | Timeline play/pause churn |
| `input_storm` | Input handling under heavy event volume |
| `command_exhaustion` | Command registry coverage and sequencing |
| `series_mixing` | Series add/remove/visibility transitions |
| `mode_switching` | 2D/3D mode transition robustness |
| `stress_docking` | Dock layout switching and split stability |
| `resize_stress` | Frame pumping during repeated resize-like load |
| `3d_zoom_then_rotate` | 3D camera zoom then orbit rotation correctness |
| `window_resize_glfw` | Real GLFW window resize with extreme aspect ratios |
| `multi_window_lifecycle` | Create/destroy/move figures across multiple OS windows |
| `tab_drag_between_windows` | Detach tabs into new windows, cross-window figure moves |
| `window_drag_stress` | Rapid window repositioning + resize combos |
| `resize_marathon` | 500+ resize events — smooth/jittery/extreme aspect ratios |
| `series_clipboard_selection` | Series copy/cut/paste/delete/deselect/multi-select correctness |
| `figure_serialization` | `FigureSerializer::save/load` roundtrip; `file.save_figure`/`file.load_figure` crash-safety |
| `series_removed_interaction_safety` | `notify_series_removed()` path — hover+markers then delete, no UAF |
| `line_culling_pan_zoom` | Draw-call culling on 10K sorted-point line — binary-search viewport cull under pan/zoom stress |

---

## Fuzz Action Coverage

| Action | Weight | What it tests |
|---|---|---|
| `ExecuteCommand` | 15 | All registered commands incl. `series.*`, `file.save_figure`, `file.load_figure` |
| `MouseClick` | 15 | UI hit-testing, data interaction selection |
| `MouseDrag` | 10 | Pan, region-select, tab drag |
| `MouseScroll` | 10 | Zoom, line culling trigger |
| `KeyPress` | 10 | Shortcut handling, text input |
| `CreateFigure` | 5 | Figure lifecycle (max 20) |
| `CloseFigure` | 3 | Figure teardown, dangling pointer paths |
| `SwitchTab` | 8 | Per-window active figure tracking |
| `AddSeries` | 8 | Series creation, GPU buffer allocation |
| `UpdateData` | 5 | In-place series data replacement |
| `LargeDataset` | 1 | 100K–500K point series — GPU memory pressure |
| `SplitDock` | 3 | Dock layout mutation |
| `Toggle3D` | 3 | 2D/3D mode switch, shader/camera state |
| `WaitFrames` | 7 | Idle frame pacing |
| `WindowResize` | 3 | GLFW resize events, swapchain recreation |
| `WindowDrag` | 3 | Window repositioning |
| `TabDetach` | 3 | Tab tearoff into new window |

---

## Performance Targets

| Metric | Target |
|---|---|
| Frame time P99 | < 16 ms (60 FPS) |
| Individual frame spike | < 50 ms |
| RSS growth over session | < 100 MB from baseline |
| Crash rate | 0% across all scenarios |
| Scenario pass rate | 100% |

---

## Interpretation Rules

- **Frame spikes >50 ms** — likely real stall (data upload, swapchain recreation, layout recalc).
- **Spikes near 16 ms early in session** — possible VSync/EMA false positive; startup EMA is low; warmup period skips first 30 frames.
- **RSS growth >100 MB** — suspicious unless explained by large dataset (`LargeDataset` fuzz action or `massive_datasets` scenario).
- **~1s stalls in `vk_acquire`** — bounded at 100 ms per window now; cascading stall with N windows = N×100 ms max. See H4 in `QA_results.md`.
- **`move_figure` warning** — cosmetic; no crash; source-window ownership tracking gap.
- **`SIGSEGV` in `LegendInteraction::draw` / `Crosshair::draw_all_axes` during multi-window fuzz** — usually stale figure cache or pre-sync active-figure use; inspect `WindowManager::clear_figure_caches` wiring and `WindowRuntime` active-figure sync order.
- **X11 `BadLength` or `BadRequest` crash during clipboard operations** — `glfwSetClipboardString` exceeds X11 protocol maximum (~4 MB). Check preceding `data.copy_all` or `data.copy_visible` with large datasets; the clipboard size guard should cap at `kMaxClipboardBytes`.
- **`std::bad_alloc` / OOM during fuzz phase** — unbounded series accumulation from repeated `series.paste` or `AddSeries` fuzz actions. Check per-axes series count against `kMaxSeriesPerAxes` guard (200). Also check `LargeDataset` action frequency if RSS growth is extreme.
- **SIGABRT/SIGSEGV in `file.save_workspace` or `file.load_workspace` command lambdas** — dangling `Series*` or `Figure*` accessed in serialization paths. Markers and other caches holding raw pointers must be cleared via `clear_figure_cache()` before save iterates them. Prefer pre-stored string labels (`series_label`) over pointer dereferences (`m.series->label()`).

---

## Issue-to-File Map

| Issue type | Primary file |
|---|---|
| Scenario and fuzz behavior | `tests/qa/qa_agent.cpp` |
| Frame-step runtime lifecycle | `src/ui/app/app_step.cpp` |
| Per-window UI bundles | `src/ui/app/window_ui_context.hpp` |
| Command registration + null guards | `src/ui/app/register_commands.cpp` |
| Figure lifecycle/tab switching | `src/ui/figures/figure_manager.cpp` |
| Per-window active figure tracking | `src/ui/app/window_runtime.cpp` |
| Undo/redo stress findings | `src/ui/commands/undo_manager.cpp` |
| Series clipboard commands | `src/ui/commands/series_clipboard.cpp` |
| Figure serialization | `src/ui/workspace/figure_serializer.cpp` |
| Series removal / dangling pointer | `src/ui/overlay/data_interaction.hpp` + `data_marker.cpp` |
| Line draw-call culling | `src/render/renderer.cpp` (`render_series()` VisibleRange) |
| Dock/split behavior | `src/ui/docking/split_view.cpp` |
| Timeline stress | `src/ui/animation/timeline_editor.cpp` |
| Render-time anomalies | `src/render/renderer.cpp` |
| Vulkan frame stalls + timeouts | `src/render/vulkan/vk_backend.cpp` |
| Swapchain recreation storm | `src/render/vulkan/vk_backend.cpp` + `session_runtime.cpp` |
| GPU memory tracking (future) | `src/render/vulkan/vk_backend.cpp` (VmaBudget) |
| 3D surface colormap rendering | `src/gpu/shaders/surface3d.frag` |
| Screenshot capture timing | `src/render/vulkan/vk_backend.cpp` (`request_framebuffer_capture`) |
| Multi-window screenshot | `tests/qa/qa_agent.cpp` (`named_screenshot` with `WindowContext*`) |

---

## Scenario-Specific Analysis

| Scenario | What to look for | Common root causes |
|---|---|---|
| `rapid_figure_lifecycle` | Memory leaks, resource cleanup | Figure destruction not freeing GPU memory |
| `massive_datasets` | Frame spikes, upload stalls (H1) | Large data → synchronous GPU buffer upload |
| `undo_redo_stress` | Memory growth, performance | Undo stack unbounded |
| `animation_stress` | Frame pacing, timing drift | Animation state corruption |
| `input_storm` | Event handling, crashes | Queue overflow, null derefs |
| `command_exhaustion` | State integrity | Commands with destructive side effects |
| `mode_switching` | 2D/3D transition performance | Resource leak during shader switch |
| `multi_window_lifecycle` | V2 validation error, stale `last_figure_` | Per-window ImGui context confusion |
| `series_clipboard_selection` | Series count assertions | `SelectionContext` stale pointer after close |
| `figure_serialization` | Save/load roundtrip count mismatch | `FigureSerializer` chunk encoding/decoding |
| `series_removed_interaction_safety` | UAF crash after `series.delete` | `notify_series_removed()` not clearing `nearest_` cache |
| `line_culling_pan_zoom` | Visual corruption after pan | Binary-search culling off-by-one in `lo_idx`/`hi_idx` |
| (future) `workspace_save_load_stress` | Dangling pointers in save/load | `DataInteraction::markers_` raw `Series*` not cleared; `FigureSerializer` chunk desync |

---

## Open Issues (from QA_results.md)

| ID | Category | Priority | Status |
|---|---|---|---|
| H1 | Large dataset frame spikes | P1 | 🟡 Open |
| H3 | `win_update` max spike 22ms | P2 | 🟡 Open |
| H4 | Periodic ~1s Vulkan acquire stalls | P1 | 🟡 Partial |
| M1 | RSS growth 80–260MB per session | P1 | 🟡 Open |
| V1 | Swapchain recreation storm | P2 | 🟡 Open |
| V2 | Descriptor image layout mismatch (multi-window) | P2 | 🟡 Open |
| Q1 | `readback_framebuffer()` race (superseded by `request_framebuffer_capture`) | P3 | 🟡 Open |

---

## Session notes

Update [REPORT.md](REPORT.md) and `plans/QA_*.md`; extend `references/` if you discover new patterns.

## Session notes

Update [REPORT.md](REPORT.md) and `plans/QA_*.md`; extend `references/` if you discover new patterns.

## Self-Improvement — YYYY-MM-DD
Improvement: <one sentence describing what was added/changed>
Motivation: <why the previous version would miss or underreport this>
Change: <file(s) edited OR new scenario/fuzz action/interpretation rule added to this SKILL.md>
Next gap: <one sentence describing the next stress blind spot to tackle next session>
```

### How to pick an improvement

1. **If crashes/regressions were found:** Turn the scenario that exposed it into a tighter targeted repro. Ask: "What seed range / scenario combo would have caught this in a 10-second run?"
2. **If no issues were found:** The stress coverage is missing real-world patterns. Pick from the Improvement Backlog below, implement it in `qa_agent.cpp` or this SKILL.md, and document the result.

### Improvement Backlog (consume one per session, add new ones as discovered)

| ID | Improvement | How to implement |
|---|---|---|
| PERF-I1 | Add `data_transform_stress` scenario: apply all 14 transform types on 100K-point series 50× | Add scenario to `qa_agent.cpp`; assert no frame spike >50ms, no RSS growth |
| PERF-I2 | Add `axis_link_storm` scenario: link/unlink 10 axes pairs 200× while panning | Add scenario; assert no deadlock, no stale SharedCursor, no frame hitch |
| PERF-I3 | Track P50 frame time in addition to P99 — large P50-P99 gap indicates periodic hitching | Add `frame_time_p50` to `qa_report.txt`; flag if P99 > 3× P50 |
| PERF-I4 | Add `keyframe_interpolator_stress` scenario: 100 channels × 500 keyframes, evaluate every frame | Add scenario; assert evaluation time stays <1ms; measure with `FrameProfiler` |
| PERF-I5 | Add `embed_surface_stress` scenario: create+destroy `EmbedSurface` 500 times, measure RSS | Add scenario; assert RSS returns to baseline ±5MB; detect `libspectra_embed.so` leak |
| PERF-I6 | Add fuzz action `ApplyTransform` (weight 4): apply random transform to random series | Add to fuzz table; stresses `DataTransform` + series dirty-flag path |
| PERF-I7 | Add `python_ipc_flood` scenario: send 1000 IPC commands as fast as possible, assert all ACKed | Add scenario; measure max IPC roundtrip latency; assert no dropped messages |
| PERF-I8 | Measure and report swapchain recreation count per session in `qa_report.txt` | Add V1 counter to `VulkanBackend`; log in session teardown; flag >10 recreations as V1 warning |
| PERF-I9 | Add `workspace_save_load_stress` scenario: save+load workspace 100× with 10 figures | Add scenario; assert roundtrip time <100ms each; assert figure count preserved |
| PERF-I10 | Cross-validate: run the same seed 3× and assert identical exit code and P99 frame time ±5% | Add `--determinism-check N` flag concept to SKILL.md; verify reproducibility |

---

## Live Report

Append findings to [REPORT.md](REPORT.md) at the end of every session.

### Report update procedure

After every run, open `REPORT.md` and:
1. Add a new `## Session YYYY-MM-DD HH:MM` block at the top (newest first).
2. Fill in: seed used, duration, exit code, scenario results (pass/fail/crash per scenario), frame time P99/max, RSS delta, CRITICAL/ERROR count, fixes applied, files changed, self-updates made.
3. Update the `## Current Status` block at the very top.
4. Never delete old session blocks — they are the performance regression history.

---

## MCP

[spectra-mcp](../spectra-mcp/SKILL.md).

## Guardrails

- Execute `spectra_qa_agent` at least once before drawing conclusions.
- Preserve seed values — paste them into `QA_results.md` with each session entry.
- Treat frame spikes >50 ms as likely real stalls.
- Treat early ~16 ms spikes as possible VSync/EMA false positives.
- Treat RSS growth >100 MB as suspicious unless justified by dataset scale.
- Reproduce every crash with exact seed before modifying code.
- Route visual screenshot-polish tasks to `qa-designer-agent`.
- Self-updates to this file require a verified run — never update speculatively.
## Reference

Detailed tables: [references/qa-performance-reference.md](references/qa-performance-reference.md).
