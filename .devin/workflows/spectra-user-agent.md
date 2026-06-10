---
description: Spectra User Agent — Act as a real user, explore features via MCP, verify with screenshots/logs, find bugs, and improve coverage each session.
---

# Spectra User Agent Workflow

**Purpose**: Act as a real Spectra user. Each session: connect via MCP, randomly exercise a subset of features, verify outcomes with screenshots and app state, log bugs found, and improve this workflow + the MCP tools if gaps are found.

---

## Pre-Session Checklist

1. Verify Spectra is running and the MCP server is reachable:
   - Use `run_command` to check: `curl -s -X POST http://127.0.0.1:8765/mcp -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ping","arguments":{}}}'`
   - **If ping fails (connection refused / exit code 7)**: Launch Spectra automatically:
     ```
     /home/daniel/projects/Spectra/build/spectra
     ```
     Run it as a **non-blocking** background command (`Blocking: false`, `WaitMsBeforeAsync: 3000`).
     Then wait 3–4 seconds and ping again. If it still fails after one retry, report the error and stop.
   - **Do NOT stop and ask the user to launch it** — always attempt to launch it yourself first.
2. Call `list_commands` (via HTTP POST to MCP) to get the current live command registry — this is the ground truth of what is explorable.
3. Call `get_state` to understand the current app state (active figure, series count, theme, etc.).
4. Call `get_window_size` to know the canvas dimensions for accurate mouse coordinates.

> **MCP HTTP helper** — all MCP tool calls use:
> ```
> curl -s -X POST http://127.0.0.1:8765/mcp \
>   -H "Content-Type: application/json" \
>   -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"<tool>","arguments":{...}}}'
> ```

---

## Session Feature Budget

**Each session must cover 6–10 features** chosen pseudo-randomly from the categories below. Use the session date/time as the seed for randomness. **Do not repeat the same feature set two sessions in a row** — track coverage in `plans/USER_AGENT_LOG.md`.

### Feature Categories

#### A — Figure Lifecycle
- `figure.new` → verify a new tab appears (screenshot)
- `figure.close` → verify tab count decreases
- `figure.tab_1` through `figure.tab_9` — tab switching
- `figure.next_tab` / `figure.prev_tab`
- `create_figure` MCP tool directly (with custom width/height)
- `switch_figure` to a specific figure ID

#### B — Series Management
- `add_series` with `line`, `scatter`, `bar`, `stem`, `step`, `histogram` types
- `series.copy` + `series.paste` (copy series, switch figure, paste)
- `series.cut` + undo to restore
- `series.delete` (select first, then delete)
- `series.cycle_selection` (Tab key cycle through series)
- `series.deselect`

#### C — View Controls
- `view.reset` (R key) — before/after screenshot compare axes
- `view.autofit` (A key)
- `view.home` (Home key)
- `view.zoom_in` / `view.zoom_out`
- `view.toggle_grid` (G) — verify grid appears/disappears
- `view.toggle_crosshair` (C) — verify crosshair on/off
- `view.toggle_legend` (L) — verify legend on/off
- `view.toggle_border` (B)
- `view.fullscreen` (F) — panels collapse/expand
- `view.toggle_3d` (3) — 2D/3D mode switch

#### D — Tool Modes
- `tool.pan` — then drag on canvas to verify pan
- `tool.box_zoom` — drag to define zoom rect
- `tool.select` — click series to select
- `tool.measure`
- `tool.annotate`
- `tool.roi`

#### E — Themes
- `theme.night` → screenshot → verify dark look
- `theme.dark` → screenshot
- `theme.light` → screenshot
- `theme.toggle` → verify toggle between dark/light
- Undo after theme change (verify revert)

#### F — Panels
- `panel.toggle_inspector` — verify inspector shows/hides
- `panel.toggle_nav_rail` — verify nav rail collapses/expands
- `panel.toggle_timeline` — verify timeline panel
- `panel.toggle_curve_editor`
- `panel.toggle_data_editor`

#### G — Split View
- `view.split_right` — verify side-by-side layout (screenshot)
- `view.split_down` — verify top/bottom layout
- `view.close_split`
- `view.reset_splits`

#### H — Animation
- `anim.toggle_play` (Space) — verify playhead moves
- `anim.step_forward` / `anim.step_back`
- `anim.go_to_start` / `anim.go_to_end`
- `anim.stop`

#### I — Edit / Undo
- `edit.undo` / `edit.redo` — perform an action, undo it, verify state reverts
- Multi-level undo (3 actions, undo all, verify original state)

#### J — File Operations
- `file.export_png` — verify `/tmp/spectra_export.png` created (call `capture_screenshot` to compare)
- `file.export_svg` — verify `/tmp/spectra_export.svg` exists
- `file.save_workspace` + `file.load_workspace` — round-trip test
- `file.save_figure` / `file.load_figure`

#### K — Mouse & Keyboard Interaction
- `mouse_drag` on plot area for pan (with button=0)
- `scroll` on plot for zoom
- `mouse_click` on legend entry to toggle visibility
- `double_click` on series label to rename
- `key_press` with known shortcuts (e.g., key=82 for R=reset, key=71 for G=grid)
- `text_input` after double-click to rename a series

#### L — Data
- `data.copy_to_clipboard` — verify no error
- `get_figure_info` — validate axes/series metadata matches what was added

#### M — App
- `app.command_palette` (Ctrl+K) — open command palette, screenshot
- `app.cancel` (Escape) — close it
- `resize_window` to 800×600 then back to original size

---

## Verification Protocol

For **every feature exercised**, follow this exact sequence:

1. **Before**: `get_state` + `get_figure_info` (if figure-related) → record baseline
2. **Action**: call the MCP tool or `execute_command`
3. **Settle**: `wait_frames` with count=5 (enough for animations to start)
4. **Screenshot**: `get_screenshot_base64` → examine the image visually
5. **After**: `get_state` + `get_figure_info` → compare with baseline
6. **Assert**: State the expected change and confirm or flag as a bug

### Assertion Rules
- `figure.new`: figure_count must increase by 1
- `view.toggle_grid`: screenshot must show grid lines (or lack thereof) matching the toggle state
- `view.toggle_legend`: legend widget presence/absence must match
- `add_series`: `get_figure_info` series array length must increase by 1
- `series.delete`: series count must decrease by 1
- `undo` after delete: series count must return to pre-delete value
- `theme.*`: screenshot background must visually match the theme (dark=near-black, light=near-white, night=deep blue)
- `view.split_right`: screenshot must show two plot panels side by side
- `resize_window`: `get_window_size` after resize must return the requested dimensions

---

## Bug Reporting Protocol

When an assertion fails or an unexpected visual is observed:

1. Record in `plans/USER_AGENT_LOG.md` under `## Session YYYY-MM-DD`:
   ```
   ### BUG: [short title]
   - **Command/Tool**: `execute_command view.toggle_grid`
   - **Expected**: Grid lines visible in screenshot
   - **Actual**: No visible change, get_state shows grid_enabled=false unchanged
   - **Screenshot**: [describe what was seen]
   - **Severity**: P0/P1/P2/P3
   - **Reproducible**: yes/no
   ```
2. If the MCP tool is missing needed data to verify (e.g., can't read theme name from `get_state`), mark it as **"MCP gap"** and proceed to the MCP improvement step below.

---

## MCP Improvement Protocol

If during the session you discover the MCP cannot expose a needed feature or return needed state:

1. **Identify the gap**: e.g., "`get_state` doesn't return current theme name" or "no tool to query selected series info"
2. **Check if automatable**: Look at `src/ui/automation/automation_server.cpp` for existing handlers, and `register_commands.cpp` for command IDs.
3. **Add the tool** to `src/ui/automation/automation_server.cpp` (new method handler) and update `kTools` in `src/ui/automation/mcp_server.cpp`.
4. **Rebuild**: `cmake --build build -j$(nproc) --target spectra`
5. **Test**: call the new tool via MCP, verify it returns expected data.
6. **Update this workflow**: add the new tool to the relevant feature category above.

### Known MCP gaps to investigate (update as discovered):
- `get_state` may not return: current theme, current tool mode, panel visibility states, animation playhead position
- No tool to query undo/redo stack depth
- No tool to query split view layout state
- No tool to check if a file was written to disk after `file.export_png`

---

## Code Improvement Protocol

When a bug is confirmed:

1. **Identify root cause** — read the relevant source file (don't guess).
2. **State intent, scope, non-goals, acceptance criteria, risk** (per Spectra agent rules).
3. **Make minimal fix** — prefer single-line changes over rewrites.
4. **Verify**: Re-run the failing verification step via MCP, confirm assertion passes.
5. **Log the fix** in `plans/USER_AGENT_LOG.md` under the same session block.

---

## Session Log Protocol

At the start of every session, **read `plans/USER_AGENT_LOG.md`** to:
- See which features were covered in recent sessions
- Prioritize **uncovered or previously-bugged** features for this session
- Check open bugs to optionally retest

At the end of every session, **write to `plans/USER_AGENT_LOG.md`**:
```markdown
## Session YYYY-MM-DD HH:MM

### Features Exercised
- [category letter] feature_name — PASS/FAIL/BUG

### Bugs Found
- [list or "none"]

### MCP Gaps Found
- [list or "none"]

### MCP Improvements Made
- [list or "none"]

### Code Fixes Made
- [list or "none"]

### Uncovered Areas (priority for next session)
- [list based on what hasn't been tested]
```

---

## Session Execution Order

1. Pre-session checklist (ping, list_commands, get_state, get_window_size)
2. Read `plans/USER_AGENT_LOG.md` for coverage history
3. Pick 6–10 features, weighted toward least-recently-tested categories
4. For each feature: Before → Action → Settle → Screenshot → After → Assert
5. For each bug: log it, attempt fix if root cause is clear
6. For each MCP gap: implement new tool if straightforward
7. Write session summary to `plans/USER_AGENT_LOG.md`
8. Update this workflow if new patterns, commands, or verification steps are discovered

---

## Quick Reference: All Registered Commands

| ID | Category | Shortcut |
|---|---|---|
| `view.reset` | View | R |
| `view.autofit` | View | A |
| `view.toggle_grid` | View | G |
| `view.toggle_crosshair` | View | C |
| `view.toggle_legend` | View | L |
| `view.toggle_border` | View | B |
| `view.fullscreen` | View | F |
| `view.home` | View | Home |
| `view.zoom_in` | View | — |
| `view.zoom_out` | View | — |
| `view.toggle_3d` | View | 3 |
| `view.split_right` | View | Ctrl+\\ |
| `view.split_down` | View | Ctrl+Shift+\\ |
| `view.close_split` | View | — |
| `view.reset_splits` | View | — |
| `app.command_palette` | App | Ctrl+K |
| `app.cancel` | App | Escape |
| `app.new_window` | App | Ctrl+Shift+N |
| `figure.new` | Figure | Ctrl+T |
| `figure.close` | Figure | Ctrl+W |
| `figure.tab_1`…`figure.tab_9` | Figure | 1–9 |
| `figure.next_tab` | Figure | Ctrl+Tab |
| `figure.prev_tab` | Figure | Ctrl+Shift+Tab |
| `figure.move_to_window` | App | Ctrl+Shift+M |
| `series.cycle_selection` | Series | Tab |
| `series.copy` | Series | Ctrl+C |
| `series.cut` | Series | Ctrl+X |
| `series.paste` | Series | Ctrl+V |
| `series.delete` | Series | Delete |
| `series.deselect` | Series | Escape |
| `data.copy_to_clipboard` | Data | Ctrl+Shift+D |
| `file.export_png` | File | Ctrl+S |
| `file.export_svg` | File | Ctrl+Shift+S |
| `file.save_workspace` | File | — |
| `file.load_workspace` | File | — |
| `file.save_figure` | File | — |
| `file.load_figure` | File | — |
| `edit.undo` | Edit | Ctrl+Z |
| `edit.redo` | Edit | Ctrl+Shift+Z |
| `anim.toggle_play` | Animation | Space |
| `anim.step_back` | Animation | [ |
| `anim.step_forward` | Animation | ] |
| `anim.stop` | Animation | — |
| `anim.go_to_start` | Animation | — |
| `anim.go_to_end` | Animation | — |
| `panel.toggle_timeline` | Panel | T |
| `panel.toggle_curve_editor` | Panel | — |
| `panel.toggle_inspector` | Panel | — |
| `panel.toggle_nav_rail` | Panel | — |
| `panel.toggle_data_editor` | Panel | — |
| `theme.night` | Theme | — |
| `theme.dark` | Theme | — |
| `theme.light` | Theme | — |
| `theme.toggle` | Theme | — |
| `tool.pan` | Tools | — |
| `tool.box_zoom` | Tools | — |
| `tool.select` | Tools | — |
| `tool.measure` | Tools | — |
| `tool.annotate` | Tools | — |
| `tool.roi` | Tools | — |

## MCP Tools (22 total)

| Tool | Purpose |
|---|---|
| `ping` | Liveness check |
| `get_state` | App state snapshot |
| `list_commands` | All registered commands |
| `execute_command` | Run any command by ID |
| `create_figure` | Create figure with size |
| `switch_figure` | Switch active figure |
| `add_series` | Add line/scatter/bar/etc |
| `get_figure_info` | Deep figure introspection |
| `get_window_size` | Window pixel dimensions |
| `get_screenshot_base64` | Full window screenshot inline |
| `capture_screenshot` | Screenshot to file |
| `capture_window` | Full window to file |
| `mouse_move` | Move cursor |
| `mouse_click` | Click at position |
| `double_click` | Double-click at position |
| `mouse_drag` | Drag gesture |
| `scroll` | Scroll at position |
| `key_press` | Keyboard input by keycode |
| `text_input` | Type text into focused widget |
| `pump_frames` | Advance N frames |
| `wait_frames` | Wait for N frames (blocking) |
| `resize_window` | Resize the app window |
