# QA Agent — Instructions & Workflow

> Living document. Defines how to run, interpret, and iterate on Spectra QA testing.
> Last updated: 2026-02-26 (Session 5: added 4 new scenarios, new commands, corrected screenshot count)

---

## Overview

The Spectra QA Agent (`spectra_qa_agent`) is a standalone executable that drives the Spectra application programmatically through predefined stress scenarios and randomized fuzzing. It monitors for crashes, Vulkan errors, frame time regressions, and memory growth, then generates reports and captures screenshots on anomalies.

**Key files:**
- `tests/qa/qa_agent.cpp` — QA agent source
- `plans/QA_update.md` — QA agent improvement backlog (living)
- `plans/QA_results.md` — Program fixes/optimizations from QA findings (living)
- `plans/QA_agent_instructions.md` — This file (living)

---

## Building

The QA agent is gated behind a CMake option (OFF by default):

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

The binary is at `build/tests/spectra_qa_agent`.

---

## Running

Every agent must run the test at least once to serve it's needs!!

### Basic run (60s, random seed)
```bash
./build/tests/spectra_qa_agent --duration 60 --output-dir /tmp/spectra_qa
```

### Reproducible run (fixed seed)
```bash
./build/tests/spectra_qa_agent --seed 42 --duration 120 --output-dir /tmp/spectra_qa
```

### Scenarios only (no fuzzing)
```bash
./build/tests/spectra_qa_agent --seed 42 --no-fuzz --output-dir /tmp/spectra_qa
```

### Fuzzing only (no scenarios)
```bash
./build/tests/spectra_qa_agent --seed 42 --no-scenarios --fuzz-frames 5000 --output-dir /tmp/spectra_qa
```

### Single scenario
```bash
./build/tests/spectra_qa_agent --scenario massive_datasets --no-fuzz --output-dir /tmp/spectra_qa
```

### List available scenarios
```bash
./build/tests/spectra_qa_agent --list-scenarios
```

### Full CLI reference
```
--seed <N>          RNG seed (default: time-based)
--duration <sec>    Max wall-clock runtime (default: 120)
--scenario <name>   Run only this scenario (default: all)
--fuzz-frames <N>   Number of fuzz frames (default: 3000)
--output-dir <path> Report/screenshot directory (default: /tmp/spectra_qa)
--no-fuzz           Skip fuzzing phase
--no-scenarios      Skip scenarios phase
--list-scenarios    Print scenario list and exit
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All scenarios passed, no errors or criticals |
| 1 | At least one ERROR or CRITICAL issue detected |
| 2 | Crash (SIGSEGV/SIGABRT) — seed printed to stderr |

---

## Output Files

After a run, the output directory contains:

- **`qa_report.txt`** — Human-readable report with summary, frame stats, memory, and issue list
- **`qa_report.json`** — Machine-readable JSON report for CI/tooling
- **`screenshot_frame<N>_<category>.png`** — Screenshots captured at anomaly frames

---

## Interpreting Results

### Frame Time Warnings
- Triggered when a frame takes > 3× the EMA (exponential moving average)
- **Known false positive:** VSync-locked frames (~16ms) flagged because early frames are fast (~3ms). See `QA_update.md` item #1.
- **Real spikes:** Look for frames > 50ms — these indicate actual stalls (data upload, swapchain recreation, layout recalculation).

### Memory Warnings
- Triggered when RSS grows > 100MB from initial baseline
- Some growth is expected (figure data, GPU buffers). Investigate if growth exceeds expected data size.

### Crashes
- The crash handler prints the seed to stderr for reproduction
- Re-run with same seed: `--seed <N> --duration <same>`
- Build with sanitizers for diagnosis: `-DCMAKE_CXX_FLAGS="-fsanitize=address"`

---

## Scenarios

| Scenario | What it tests | Frames |
|----------|---------------|--------|
| `rapid_figure_lifecycle` | Create 20 figures, random switching, close all but 1 | ~100 |
| `massive_datasets` | 1M-point line + 5×100K series, render stress | ~40 |
| `undo_redo_stress` | 50 undo ops, undo all, redo all, partial undo + new | ~155 |
| `animation_stress` | Rapid play/pause toggling every 5 frames | ~300 |
| `input_storm` | 500 random mouse events + 100 key presses | ~240 |
| `command_exhaustion` | Execute every command, then 3× random order | varies |
| `series_mixing` | Line + scatter, toggle visibility, remove/re-add | ~30 |
| `mode_switching` | Toggle 2D/3D 10 times with data | ~100 |
| `stress_docking` | 4 figures, split grid, rapid tab switching | ~50 |
| `resize_stress` | 30 rapid frame pumps (resize injection planned) | ~90 |
| `3d_zoom_then_rotate` | 3D camera zoom then orbit rotation correctness | ~210 |
| `window_resize_glfw` | Real GLFW window resize with extreme aspect ratios | ~100 |
| `multi_window_lifecycle` | Create/destroy/move figures across multiple OS windows | ~60 |
| `tab_drag_between_windows` | Detach tabs into new windows, cross-window figure moves | ~60 |
| `window_drag_stress` | Rapid window repositioning + resize combos | ~80 |
| `resize_marathon` | 500+ resize events — smooth/jittery/extreme aspect ratios | ~600 |
| `series_clipboard_selection` | Series copy/cut/paste/delete/deselect/multi-select correctness | ~80 |
| `figure_serialization` | `FigureSerializer::save/load` roundtrip + `file.save_figure`/`file.load_figure` crash-safety | ~35 |
| `series_removed_interaction_safety` | `notify_series_removed()` path — hover+markers then delete, no UAF | ~45 |
| `line_culling_pan_zoom` | Draw-call culling on 10K sorted-point line under pan/zoom stress | ~120 |

## Fuzz Actions

| Action | Weight | Description |
|--------|--------|-------------|
| ExecuteCommand | 15 | Random registered command (skips quit/close) |
| MouseClick | 15 | Random position click |
| MouseDrag | 10 | Random start→end drag with interpolation |
| MouseScroll | 10 | Random position scroll |
| KeyPress | 10 | Random ASCII key press/release |
| CreateFigure | 5 | New figure with random data (max 20) |
| CloseFigure | 3 | Close random figure (keeps at least 1) |
| SwitchTab | 8 | Switch to random figure tab |
| AddSeries | 8 | Add line or scatter to random figure |
| UpdateData | 5 | Replace Y data on random series |
| LargeDataset | 1 | Add 100K–500K point series |
| SplitDock | 3 | Split view right or down |
| Toggle3D | 3 | Toggle 2D/3D mode |
| WaitFrames | 7 | Pump 1–10 idle frames |
| WindowResize | 3 | GLFW window resize events, swapchain recreation |
| WindowDrag | 3 | Window repositioning |
| TabDetach | 3 | Tab tearoff into new OS window |

---

## Workflow: QA Improvement Cycle

This is the iterative process for improving Spectra using the QA agent:

### 1. Run QA Session
```bash
./build/tests/spectra_qa_agent --seed $(date +%s) --duration 120 --output-dir /tmp/spectra_qa_$(date +%Y%m%d)
```

### 2. Read Results
- Check exit code (0/1/2)
- Read `qa_report.txt` summary section
- Look for ERROR/CRITICAL issues first, then warnings
- Check memory growth
- Check frame time P99 and max

### 3. Update Living Documents
- **`QA_results.md`** — Add new program bugs/optimizations found. Update status of existing items.
- **`QA_update.md`** — Add new QA agent improvements needed. Update status of implemented items.

### 4. Fix Issues
- Fix Spectra bugs identified in `QA_results.md` (prioritize P0 → P1 → P2)
- Improve QA agent per `QA_update.md` (prioritize P0 → P1 → P2)

### 5. Verify Fixes
- Re-run with same seed to confirm fix: `--seed <original_seed>`
- Run with new seed to check for regressions
- Update document statuses

### 6. Repeat
- Each session should produce fewer issues as the codebase stabilizes
- Track issue count trend across sessions in `QA_results.md`

---

## Architecture Notes

### App::step() API
The QA agent uses the `init_runtime()` / `step()` / `shutdown_runtime()` API added to `App`:

```cpp
App app(config);
app.figure({1280, 720});  // Create initial figure
app.init_runtime();        // Initialize all subsystems

for (;;) {
    auto result = app.step();  // One frame
    // result.should_exit, result.frame_time_ms, result.frame_number
    if (result.should_exit) break;
}

app.shutdown_runtime();    // Cleanup
```

### Pimpl Pattern
`App` uses `std::unique_ptr<AppRuntime>` for runtime state. Both constructor and destructor are defined in `app_step.cpp` where `AppRuntime` is a complete type. This is required by the C++ standard for `unique_ptr` with incomplete types.

### UI Access
The QA agent accesses internal UI components via `app.ui_context()` which returns a `WindowUIContext*`:
- `ui->cmd_registry` — execute commands
- `ui->input_handler` — inject mouse/keyboard events
- `ui->fig_mgr` — switch/close figures
- `ui->undo_mgr` — undo/redo operations
- `ui->timeline_editor` — animation control
- `ui->dock_system` — docking operations

### Thread Safety
The QA agent runs single-threaded on the main thread (same as normal Spectra). All UI operations are synchronous within `step()`. No additional synchronization needed.

---

## Adding New Scenarios

To add a new scenario:

1. Add a method `bool scenario_<name>()` to `QAAgent` in `qa_agent.cpp`
2. Register it in `register_scenarios()`:
   ```cpp
   scenarios_.push_back({"name", "description",
       [](QAAgent& qa) { return qa.scenario_name(); }});
   ```
3. Use `pump_frames(N)` to advance the app between actions
4. Use `add_issue()` to report problems found
5. Return `true` for pass, `false` for fail

## Adding New Fuzz Actions

1. Add entry to `FuzzAction` enum
2. Add weight in `run_fuzzing()` weights vector
3. Add case in `execute_fuzz_action()` switch
4. Guard platform-specific code with `#ifdef SPECTRA_USE_GLFW` / `SPECTRA_USE_IMGUI`

---

## Design Review Mode

The QA agent includes a `--design-review` flag that systematically captures named screenshots of every meaningful UI state. This enables visual analysis and iterative design improvement.

### Running a Design Review
```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design
```

### What It Captures (51 screenshots)

**Core UI (1–20)**
1. Default single line plot
2. Empty axes
3. Multi-series with labels (line + scatter)
4. Dense data (10K points)
5. Subplot 2×2 grid
6. Large scatter plot (2K points, normal distribution)
7. Inspector panel open
8. Command palette open
9. Split view (2 panes)
10. Split view (4 panes)
11. Dark theme
12. Light theme
13. Grid enabled
14. Legend visible
15. Crosshair mode
16. Zoomed in view
17. Multiple tabs
18. Timeline panel
19. 3D surface plot
20. 3D scatter plot

**3D / Animation / Statistics (21–35)**
21. 3D surface with axis labels + lighting + bounding box
22. 3D camera side view (azimuth=0, elevation=15)
23. 3D camera top-down view (elevation=85)
24. 3D line plot (helix)
25. 3D scatter with two colored clusters
26. 3D orthographic projection
27. Inspector with series selected (statistics visible)
28. Inspector with axes properties
29. Timeline with keyframe tracks and markers
30. Timeline playing (playhead mid-animation)
31. Timeline with loop region overlay
32. Curve editor panel
33. Split view with 2 actual figures (proper split)
34. Multi-series with legend + grid + crosshair (full chrome)
35. Zoomed-in data center verification (D12 fix)

**Menu / Window / Interaction (36–51)**
36. Menu bar activated
37. Command palette with search text
38. Inspector with knobs panel
39. Nav rail visible
40. Tab right-click context menu
41. Window resized 640×480
42. Window resized 1920×600 (ultra-wide)
43. Window resized 600×1080 (tall)
44. Window resized 320×240 (minimum)
45. Multi-window primary (via `target_window` capture)
45b. Multi-window secondary (via `target_window` capture)
46. Window repositioned top-left
47. Split view with inspector + timeline open
48. Two windows side by side
49. Fullscreen mode
50. Minimal chrome (all panels closed)

> **Multi-window screenshot API:** `named_screenshot()` accepts an optional `WindowContext*` so the capture fires only during that window's `end_frame`. Do not use `set_active_window` + `pump_frames` — `step()` overrides the active window.

### Output
- Screenshots go to `<output-dir>/design/` with descriptive names
- `manifest.txt` lists all captured screenshots
- View screenshots in any image viewer to analyze UI/UX

### Design Review Workflow
1. Run `--design-review` to capture baseline
2. Analyze screenshots — look for visual bugs, polish issues, UX problems
3. Update `plans/QA_design_review.md` with findings
4. Implement fixes
5. Re-run `--design-review` and compare before/after
6. Repeat

### Living Documents
- **`plans/QA_design_review.md`** — UI/UX improvement backlog from visual analysis
- **`plans/QA_results.md`** — Program bugs/optimizations from stress testing
- **`plans/QA_update.md`** — QA agent improvement backlog

---

## QA Designer Agent — Visual Review & Fix Workflow

The QA Designer Agent is a specialized role that reviews design screenshots, identifies UI/UX issues, and implements fixes directly in the codebase. It operates on the `plans/QA_design_review.md` living document.

### Role Definition

**Intent:** Systematically improve Spectra's visual quality and UX by analyzing screenshots, diagnosing root causes, and applying minimal targeted fixes.

**Scope:** Theme colors, ImGui rendering code, overlay positioning, status bar styling, crosshair labels, legend rendering, tab bar styling, and any visual element visible in design review screenshots.

**Non-goals:** Architectural changes, new features, performance optimization, IPC protocol changes.

### Workflow

1. **Capture screenshots** — Run `--design-review` to get 35 systematic screenshots
2. **Read `QA_design_review.md`** — Check open items, prioritize P0 > P1 > P2 > P3
3. **Analyze each open item** — Read the relevant source files, understand root cause
4. **Verify before fixing** — Some items may already be fixed by other agents; mark as "✅ Already Fixed"
5. **Apply minimal fixes** — Prefer single-line color/position changes over refactors
6. **Build and test** — `cmake --build build -j$(nproc)` + `ctest`
7. **Update `QA_design_review.md`** — Mark fixed items with fix details, files changed
8. **Re-capture and compare** — Run `--design-review` again to verify visual improvements

### Key File Locations for Common Fixes

| Issue Type | Primary File | What to Change |
|-----------|-------------|----------------|
| Theme colors (grid, axis, text) | `src/ui/theme/theme.cpp` | `initialize_default_themes()` — dark/light/high_contrast color values |
| Axes border rendering | `src/render/renderer.cpp` | `render_axis_border()` — border geometry and push constants |
| Grid line rendering | `src/render/renderer.cpp` | `render_grid()` — grid color from theme or axis_style |
| Legend background/border | `src/ui/overlay/legend_interaction.cpp` | Legend window styling (bg_col, border_col, rounding) |
| Crosshair labels | `src/ui/overlay/crosshair.cpp` | `draw()` and `draw_all_axes()` — label positioning |
| Status bar | `src/ui/imgui/imgui_integration.cpp` | `draw_status_bar()` — FPS color, mode pill, separators |
| Tab bar styling | `src/ui/figures/tab_bar.cpp` | `draw_tabs()` — active/hover backgrounds, accent underline |
| Nav rail icons | `src/ui/imgui/imgui_integration.cpp` | `draw_nav_rail()` — icon buttons, tooltips |
| Command palette | `src/ui/commands/command_palette.cpp` | Overlay rendering, shortcut badges |
| Timeline controls | `src/ui/animation/timeline_editor.cpp` | Transport button rendering |
| Menu bar | `src/ui/imgui/imgui_integration.cpp` | `draw_menubar()` — hover effects |
| Inspector panel | `src/ui/overlay/inspector.cpp` | Panel content when empty |
| Split view dividers | `src/ui/docking/split_view.cpp` | Pane boundary rendering |
| Design tokens | `src/ui/theme/design_tokens.hpp` | Spacing, radius, font sizes, opacity constants |
| Text rendering (Vulkan) | `src/render/text_renderer.cpp` | Font atlas, glyph rendering quality |

### Fix Patterns

**Theme color fix:** Change `Color::from_hex(0xXXXXXX)` to `Color(r, g, b, alpha)` for proper alpha blending. Use float RGBA for transparency control.

**Position fix:** Change label position from outside-viewport (`vx0 - offset`) to inside-viewport (`vx0 + margin`). Always clamp to viewport bounds.

**Styling fix:** Use ImGui draw list primitives (`AddRectFilled`, `AddLine`) for pill backgrounds, borders, separators. Reference `ui::tokens::` for spacing and `ui::theme()` for colors.

**Verify-before-fix:** Always `grep_search` and `read_file` the target code before assuming it needs changes. Other agents may have already fixed the issue.

### Golden Image Test Impact

Theme color changes (grid, axis, border) will cause golden image test failures. This is expected — the golden references need to be regenerated after visual improvements. Run:
```bash
# Regenerate golden images after visual fixes
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

### Design Principles (from QA_design_review.md)

1. **Data first** — minimize chrome, maximize plot area
2. **Clear hierarchy** — primary content (plot) > secondary (axes/labels) > tertiary (chrome)
3. **Consistent spacing** — use design token spacing scale
4. **Typography matters** — clean, crisp text at every size
5. **Subtle depth** — use shadows and opacity, not heavy borders
6. **Responsive feedback** — every interaction should have visual feedback
7. **Accessible** — WCAG AA contrast ratios, colorblind-safe palettes

---

## Known Limitations

- **Requires display:** GLFW window needed (no headless mode yet)
- **Linux RSS only:** Memory monitoring uses `/proc/self/statm`
- **No GPU memory tracking:** Only CPU RSS, not VMA/Vulkan allocations
- **No validation layer integration:** Vulkan errors not monitored yet (QA_update.md item #4)
- **Screenshot timing fixed:** `request_framebuffer_capture()` copies after GPU submit, before present (commit `4477b46`). Do not use `readback_framebuffer()` in QA capture paths.
- **Multi-window screenshot fixed:** `named_screenshot(name, WindowContext*)` targets a specific window's `end_frame` (D41 fix). Do not use `set_active_window` + `pump_frames` workaround.
- **Crash handler:** Dumps seed, last action, and backtrace to `qa_crash.txt`. Reproduce with `--seed <N>`.
- **Design review screenshot count:** 51 (not 35) — includes menu/window/interaction states added in sessions 4–5.
