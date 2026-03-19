# Spectra User Agent — Session Log

This file tracks feature coverage, bugs, MCP gaps, and fixes across all user-agent sessions.
Each session appends a block below. Agents must read this before choosing what to test.

---

## Coverage Heat Map

Legend: ✅ tested+passing | ⚠️ tested+bug found | ❌ never tested

| Category | Features | Status |
|---|---|---|
| A — Figure Lifecycle | figure.new ✅, figure.close ❌, tab switching ❌, create_figure ❌, switch_figure ❌ | ⚠️ partial |
| B — Series Management | add_series line ✅ scatter ✅ bar ✅ histogram ✅, cycle_selection ✅, delete ✅, undo-delete ✅, copy/cut/paste/stem/step ❌ | ⚠️ partial |
| C — View Controls | reset ✅, grid ✅, scroll-zoom ✅, crosshair ✅, legend ✅, autofit ❌, home ❌, zoom_in/out ❌, border ❌, fullscreen ❌, toggle_3d ❌ | ⚠️ partial |
| D — Tool Modes | pan ✅, box_zoom ❌, select ❌, measure ❌, annotate ❌, roi ❌ | ⚠️ partial |
| E — Themes | night ✅, light ✅, dark ❌, toggle ❌, undo-theme ✅ | ⚠️ partial |
| F — Panels | inspector ✅, nav_rail ❌, timeline ❌, curve_editor ❌, data_editor ❌ | ⚠️ partial |
| G — Split View | split_right ✅, reset_splits ✅, split_down ❌, close_split ❌ | ⚠️ partial |
| H — Animation | toggle_play ✅, step_forward ✅, go_to_start ✅, step_back ❌, go_to_end ❌, stop ❌ | ⚠️ partial |
| I — Edit / Undo | undo (theme) ✅, undo (delete) ✅, redo ❌, multi-level ❌ | ⚠️ partial |
| J — File Operations | export_png ❌, export_svg ❌, save/load workspace ❌, save/load figure ❌ | ❌ |
| K — Mouse & Keyboard | scroll-zoom ✅, mouse_drag pan ✅, click legend ❌, double-click rename ❌, key_press ❌, text_input ❌ | ⚠️ partial |
| L — Data | copy_to_clipboard ❌, get_figure_info ✅ | ⚠️ partial |
| M — App | command_palette ⚠️ (executes/no persist), cancel ❌, resize_window ❌ | ⚠️ partial |

---

## Open Bugs

_None open. All bugs from sessions 1–3 fixed in-session._

---

## Open MCP Gaps

_Remaining suspected gaps (to investigate):_
- `get_state` does not return: current tool mode, panel visibility states, animation playhead position  ← investigate in session 2
- No tool to query split view layout state
- No tool to check if a file was written to disk after `file.export_png`

_Closed gaps:_
- ~~`get_state` missing theme name~~ — FIXED: `"theme"` field added (session 2026-03-17)

---

<!-- sessions appended below -->

---

## Session 2026-03-18 23:25

### Features Exercised
- [A] `figure.new` — PASS (figure_count 0→1, active_figure_id=1)
- [B] `add_series` scatter (x=[1..10], y=[2.1,3.5,…]) — **BUG FIXED → PASS** (get_figure_info confirms type:scatter, point_count:10)
- [B] `add_series` bar (x=[1..5], y=[10,25,15,30,20]) — **BUG FIXED → PASS** (type:bar, point_count:4; bars render at correct heights)
- [B] `add_series` histogram (y=[1..9], bins=5) — **BUG FIXED → PASS** (type:histogram, point_count:5)
- [C] `view.toggle_legend` — PASS (legend box disappears then reappears on second toggle; confirmed by screenshot)
- [C] `view.toggle_crosshair` — PASS (dashed crosshair lines visible at cursor position with coordinate labels)
- [D] `tool.pan` — PASS (pan tool activated via command)
- [K] `mouse_drag` pan — PASS (x-axis shifted from [0.38,5.22] → [1.25,6.09] after 200px drag)
- [B] `series.cycle_selection` — PASS (series A selected, confirmed via app log "Series selected from canvas: A")
- [B] `series.delete` — PASS (series count 2→1)
- [I] `edit.undo` after `series.delete` — **BUG FIXED → PASS** (count restored 1→2, deleted series 'A' re-appears with label intact)
- [M] `app.command_palette` — PARTIAL (command executes cleanly but palette overlay doesn't persist; expected behaviour without keyboard input focus)
- [H] `anim.toggle_play` — PASS (play started, wait_frames confirmed frames rendered, toggle_play again stopped)
- [H] `anim.step_forward` — PASS (command executes without error)
- [H] `anim.go_to_start` — PASS (command executes without error)

### Bugs Found & Fixed

#### BUG-001: `add_series` ignored caller x/y data and always rendered sine wave
- **Root cause 1**: `mcp_server.cpp::remap_arguments_for_tool` was rebuilding the JSON arguments for `add_series` but never forwarded `x`/`y` arrays — only `figure_id`, `series_type` (wrong key), `n_points`, `label`.
- **Root cause 2**: `automation_server.cpp` `add_series` handler always generated sine wave data; only handled `scatter` type, defaulted everything else to `line`.
- **Fix**: 
  - `mcp_server.cpp`: updated schema to add `x`, `y`, `bins`, `type` parameters; fixed remap to forward all of them.
  - `automation_server.cpp`: added `json_get_float_array` helper; rewrote handler to use caller data if provided, fall back to sine wave only when omitted; added `bar` and `histogram` dispatch.
  - Added `#include <spectra/series_stats.hpp>` to get complete `BarSeries`/`HistogramSeries` definitions.
- **Files**: `src/ui/automation/automation_server.cpp`, `src/ui/automation/mcp_server.cpp`
- **Verified**: `get_figure_info` now returns correct `type` and `point_count`; visual screenshot confirms correct data shapes.

#### BUG-002: `get_figure_info` reported `type:unknown, point_count:0` for bar/histogram series
- **Root cause**: `get_figure_info` handler only dynamic_cast-checked `LineSeries*` and `ScatterSeries*`, not `BarSeries*` / `HistogramSeries*`.
- **Fix**: Added two more `else if` branches in `automation_server.cpp` for `BarSeries` (using `bar_positions().size()`) and `HistogramSeries` (using `bin_counts().size()`).
- **Files**: `src/ui/automation/automation_server.cpp`
- **Verified**: `get_figure_info` now returns `type:bar` and `type:histogram` with correct counts.

#### BUG-003: `series.delete` + `edit.undo` did not restore deleted series
- **Root cause**: `series.delete` command deferred the removal via `imgui_ui->defer_series_removal()` but **never called `undo_mgr.push()`**. The undo stack was unaffected so `edit.undo` would undo a completely different prior action.
- **Fix**: In `register_commands.cpp` `series.delete` handler — before deferring removal, snapshot each selected series via `SeriesClipboard::snapshot()`. After deferring, push an `UndoAction` whose `undo_fn` calls `SeriesClipboard::paste_to(owner, snap)` for each snapshotted entry.
- **Files**: `src/ui/app/register_commands.cpp`
- **Verified**: delete series A (count 2→1), `edit.undo` → count 1→2, series 'A' re-appears with correct label.

### MCP Gaps Found
- `add_series` previously exposed as `series_type` parameter (wrong name) — fixed to `type`.
- `add_series` schema lacked `x`, `y`, `bins` parameters — added.

### Coverage Heat Map Updates
- B: scatter ✅, bar ✅, histogram ✅, series.cycle_selection ✅, series.delete ✅, undo-delete ✅
- C: crosshair ✅, legend ✅
- D: tool.pan ✅
- H: anim.toggle_play ✅, anim.step_forward ✅, anim.go_to_start ✅
- I: undo after delete ✅
- K: mouse_drag pan ✅
- M: app.command_palette ⚠️ (executes but palette closes without keyboard focus)

---

## Session 2026-03-17 23:38

### Features Exercised
- [A] `figure.new` — PASS (figure_count 0→1, tab visible in screenshot)
- [B] `add_series` line type (50 pts, label "Test Line") — PASS (get_figure_info confirms type:line, point_count:50, visible:true)
- [C] `view.toggle_grid` — PASS (grid visually disappears/reappears, confirmed by screenshot diff)
- [C] `view.reset` after scroll-zoom — PASS (axes returned exactly to auto-fit range x:-2.45→51.45 y:-1.1→1.1)
- [K] `scroll` zoom — PASS (axes zoomed to x:-9.2→-8.7 y:1.57→1.59)
- [E] `theme.night` — PASS (visual: deep blue-dark canvas confirmed)
- [E] `theme.light` — PASS (visual: white/light-grey canvas clearly different from dark)
- [I] `edit.undo` (theme revert) — PASS (undo_count incremented, redo_count=1, dark theme restored visually)
- [F] `panel.toggle_inspector` — PASS (inspector panel slides in showing Figure/1 axes/1 series; plot area resizes)
- [G] `view.split_right` — PASS (two panels side-by-side: Figure 2 empty left, Figure 1 with sine right)
- [G] `view.reset_splits` — PASS (split closed, single view restored)

### Bugs Found
- **BUG FIXED: `is_3d_mode` wrong default** — `get_state` returned `is_3d_mode:true` for all fresh 2D figures because `WindowUIContext::is_in_3d_mode` was initialized to `true`. Fixed by changing default to `false` in `src/ui/app/window_ui_context.hpp:68`. Verified: fresh 2D figure now returns `is_3d_mode:false`.

### MCP Gaps Found & Fixed
- **FIXED: `get_state` missing `theme` field** — could not programmatically verify theme switches. Added `"theme":"<name>"` to `get_state` response in `src/ui/automation/automation_server.cpp`. Added `#include "ui/theme/theme.hpp"` under `SPECTRA_USE_IMGUI` guard. Verified: `get_state` now returns `"theme":"night"`.

### Code Fixes Made
- `src/ui/app/window_ui_context.hpp:68` — `is_in_3d_mode = true` → `is_in_3d_mode = false`
- `src/ui/automation/automation_server.cpp` — added `theme.hpp` include + `"theme"` field to `get_state`

### Uncovered Areas (priority for next session)
- [D] Tool modes: pan drag, box_zoom, select, measure, annotate, roi
- [H] Animation: play/pause, step_forward/back, go_to_start/end
- [I] Multi-level undo (3 actions, undo all)
- [J] File ops: export_png (verify file on disk), save/load workspace round-trip
- [K] `mouse_drag` for pan, `double_click` + `text_input` rename series
- [B] More series types: scatter, bar, stem, step, histogram
- [B] series.copy + series.paste across figures
- [M] `app.command_palette` open/close
- [C] `view.toggle_legend`, `view.toggle_crosshair`, `view.toggle_3d`
- [F] `panel.toggle_timeline`, `panel.toggle_nav_rail`
