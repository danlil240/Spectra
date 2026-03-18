# Spectra User Agent — Session Log

This file tracks feature coverage, bugs, MCP gaps, and fixes across all user-agent sessions.
Each session appends a block below. Agents must read this before choosing what to test.

---

## Coverage Heat Map

Legend: ✅ tested+passing | ⚠️ tested+bug found | ❌ never tested

| Category | Features | Status |
|---|---|---|
| A — Figure Lifecycle | figure.new ✅, figure.close ❌, tab switching ❌, create_figure ❌, switch_figure ❌ | ⚠️ partial |
| B — Series Management | add_series line ✅, scatter/bar/stem/step/histogram ❌, copy/cut/paste/delete/cycle/deselect ❌ | ⚠️ partial |
| C — View Controls | reset ✅, grid ✅, scroll-zoom ✅, autofit ❌, home ❌, zoom_in/out ❌, crosshair ❌, legend ❌, border ❌, fullscreen ❌, toggle_3d ❌ | ⚠️ partial |
| D — Tool Modes | pan, box_zoom, select, measure, annotate, roi | ❌ |
| E — Themes | night ✅, light ✅, dark ❌, toggle ❌, undo-theme ✅ | ⚠️ partial |
| F — Panels | inspector ✅, nav_rail ❌, timeline ❌, curve_editor ❌, data_editor ❌ | ⚠️ partial |
| G — Split View | split_right ✅, reset_splits ✅, split_down ❌, close_split ❌ | ⚠️ partial |
| H — Animation | play/pause, step_forward/back, go_to_start/end, stop | ❌ |
| I — Edit / Undo | undo (theme) ✅, redo ❌, multi-level undo ❌ | ⚠️ partial |
| J — File Operations | export_png, export_svg, save/load workspace, save/load figure | ❌ |
| K — Mouse & Keyboard | scroll-zoom ✅, drag pan ❌, click legend ❌, double-click rename ❌, key_press ❌, text_input ❌ | ⚠️ partial |
| L — Data | copy_to_clipboard ❌, get_figure_info ✅ | ⚠️ partial |
| M — App | command_palette, cancel, resize_window | ❌ |

---

## Open Bugs

_None open. All found bugs fixed in session 1._

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
