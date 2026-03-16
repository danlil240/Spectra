---
name: graphical-change-workflow
description: >
  End-to-end validation workflow for any graphical or rendering change in Spectra.
  Use when implementing visual features, fixing rendering bugs, adjusting themes, modifying
  shaders, or any change that affects what is drawn on screen. Covers: compile, kill existing
  instances, launch the app, plot representative data, and capture a screenshot for comparison.
applyTo: "src/render/**,src/ui/**,src/gpu/shaders/**,src/core/**,include/spectra/**"
---

# Graphical Change Workflow

After every graphical change, follow this deterministic loop to build, launch, plot, and capture
visual evidence. Never rely on "it looks fine in my head" — always capture a screenshot.

---

## Required Context

Before starting, check:
- `plans/QA_design_review.md` — open visual issues and priorities
- `tests/golden/` — existing golden baselines that your change may affect

---

## Step 1 — Build

Compile a fresh binary after your source edits.

```bash
cmake --build build -j$(nproc)
```

If CMake re-configure is needed (shaders, assets, feature flags changed):

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j$(nproc)
```

Verify the build exits with code 0 before proceeding. Fix any errors before continuing.

---

## Step 2 — Kill Existing Instances

All running Spectra windows must be closed before launching a fresh one so the MCP server
binds to the new process.

```bash
pkill -f spectra || true          # graceful kill; ignore "no process" exit code
pkill -f spectra-backend || true
sleep 0.5                         # give sockets time to release
```

> Watch for lingering processes with `pgrep -a spectra` if anything seems stuck.

---

## Step 3 — Launch the App

Start a fresh Spectra instance. Use the example binary that best exercises the changed code path.
Default to `basic_line` for a minimal 2-D surface. Use a 3D example when the change is 3D-specific.

```bash
./build/examples/basic_line &
SPECTRA_PID=$!
sleep 1     # allow the MCP server to start and bind on port 8765
```

Confirm it is live with the MCP `ping` tool before proceeding:

```
mcp: ping  →  expect { "pong": true }
```

---

## Step 4 — Plot Representative Data

Create a figure and add series that exercise the changed rendering path. Tailor the plot to the
change:

| Change type | Recommended plot |
|---|---|
| Line / marker rendering | `add_series` with `series_type=line`, ~200 points |
| Scatter / point rendering | `add_series` with `series_type=scatter`, ~1000 points |
| 3D surface / depth | `add_series` with `series_type=surface` |
| Theme / color change | Multiple series in a single figure to show palette |
| Grid / axis change | Any series + `execute_command view.toggle_grid` |
| Legend change | Multiple named series + `execute_command view.toggle_legend` |
| Animation | `add_series` then `execute_command anim.toggle_play` |

Minimal example sequence via MCP tools:

```
mcp: get_state
     → confirm figure_count ≥ 0

mcp: create_figure  { "width": 1280, "height": 720 }
     → note returned figure_id

mcp: add_series  { "figure_id": <id>, "series_type": "line", "n_points": 200, "label": "test" }

mcp: wait_frames  { "count": 10 }   ← let the GPU finish rendering
```

Add additional series / commands as needed to fully exercise the change.

---

## Step 5 — Capture Screenshot

Capture both a canvas screenshot and a full-window shot:

```
mcp: capture_screenshot  { "path": "/tmp/spectra_change_canvas.png" }
mcp: capture_window      { "path": "/tmp/spectra_change_window.png" }
```

Then inline the result for immediate visual inspection:

```
mcp: get_screenshot_base64
```

Acceptable result criteria:
- No black / blank canvas
- No visual artifacts, z-fighting, or flickering
- Color, grid, legend, and labels render as designed
- No validation layer errors in the terminal where the app was launched

---

## Step 6 — Compare Against Baseline (if goldens exist)

If the change touches a path covered by golden tests, run them:

```bash
ctest --test-dir build -L golden -j1 --output-on-failure
```

If the change is **intentional** and goldens are now stale, regenerate them after visual sign-off:

```bash
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

Never regenerate goldens automatically without first verifying the new screenshot manually.

---

## Step 7 — Clean Up

```bash
kill $SPECTRA_PID 2>/dev/null || pkill -f spectra || true
```

---

## MCP Tool Reference

All MCP tools are available via the `spectra-automation` server at `http://127.0.0.1:8765/mcp`.
The server is embedded in the Spectra process and starts automatically when the app launches.

| Tool | Purpose |
|---|---|
| `ping` | Verify the MCP server is live |
| `get_state` | Inspect figure count, active figure, undo stack |
| `list_commands` | Enumerate all registered UI commands |
| `execute_command` | Fire any registered command by ID |
| `create_figure` | Open a new figure window |
| `switch_figure` | Focus a specific figure by ID |
| `add_series` | Add a data series to a figure |
| `get_figure_info` | Read axes count, series list, data limits |
| `wait_frames` | Block until N frames have been rendered |
| `pump_frames` | Advance the frame loop manually (N frames) |
| `capture_screenshot` | Save canvas PNG to a path |
| `capture_window` | Save full-window PNG to a path |
| `get_screenshot_base64` | Return inline base64 PNG for immediate display |
| `get_window_size` | Read current window dimensions |
| `resize_window` | Resize the window before capture |
| `mouse_move` | Move cursor to (x, y) |
| `mouse_click` | Click at (x, y) with optional button/modifiers |
| `mouse_drag` | Drag from (x1,y1) to (x2,y2) |
| `double_click` | Double-click at (x, y) |
| `scroll` | Scroll at (x, y) with (dx, dy) |
| `key_press` | Send a key event by keycode + modifiers |
| `text_input` | Type text into the focused widget |

---

## Common Pitfalls

- **MCP connect refused** — app is not running, or the previous instance is still holding port 8765.
  Run the kill step again and wait a full second after relaunch.
- **Black screenshot** — `wait_frames` was skipped. Always wait at least 5–10 frames after adding
  series before capturing.
- **Golden mismatch after theme change** — expected; regenerate goldens only after visual sign-off.
- **Wrong binary** — if the change is in a library, make sure the example binary was rebuilt. Check
  `build/examples/` modification time.
- **Shaders not recompiled** — shader source changes require a full CMake re-configure because SPIR-V
  is compiled and embedded at configure time, not build time.
