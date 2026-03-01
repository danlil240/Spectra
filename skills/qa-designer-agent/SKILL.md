---
name: qa-designer-agent
description: Run Spectra visual QA design reviews end-to-end with `spectra_qa_agent`, from screenshot capture to UI/UX bug triage and minimal code fixes. Use when asked to run `--design-review`, inspect screenshot regressions, update `plans/QA_design_review.md`, or apply targeted theme/ImGui/overlay rendering polish tied to QA findings.
---

# QA Designer Agent

Run a deterministic visual QA loop: capture screenshots, triage prioritized issues, apply minimal fixes, verify with before/after captures, and update living QA documents.

---

## Required Context

Before starting any task, read these living documents:
- `plans/QA_design_review.md` — open visual items and priority (`P0 → P1 → P2 → P3`)
- `plans/QA_update.md` — QA-agent capability gaps and improvements
- `plans/QA_results.md` — non-visual product bugs from stress sessions

---

## Workflow

### 1. Build

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

### 2. Capture baseline (required — run at least once per task)

```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_$(date +%Y%m%d)
```

- Screenshots land in `<output-dir>/design/` with descriptive names.
- `manifest.txt` lists all captured files.
- **Expect 51 named screenshots** (see coverage table below).
- Requires a live display — no headless path.

### 3. Triage open items

- Work `P0` → `P1` → `P2` → `P3`.
- Verify each issue is reproducible with current code before touching anything.
- Mark stale items as `Already Fixed` or `By Design` with evidence (file + line).

### 4. Diagnose root cause before editing

- Read the responsible file first (use the Issue-to-File Map below).
- Reproduce with fixed seed where possible.
- Confirm the rendering path: theme → renderer → overlay → ImGui.

### 5. Apply minimal visual fixes

- Prefer `ui::tokens::` and `ui::theme()` driven adjustments over hardcoded values.
- Prefer single-line color/position changes over structural refactors.
- Keep scope on UI/UX polish unless a behavior bug blocks visual correctness.

### 6. Validate and recapture

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_after_$(date +%Y%m%d)
```

- Compare before/after screenshots for every touched issue.
- If theme colors, grid, axes, or border visuals changed, refresh goldens:

```bash
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

### 7. Update living documents

- `plans/QA_design_review.md` — root cause, fix summary, files changed, verification date, status (`Fixed` / `Already Fixed` / `By Design`).
- `plans/QA_update.md` — any new QA-agent gaps discovered.
- `plans/QA_results.md` — non-visual bugs or perf findings.

---

## Design-Review Screenshot Coverage (51 total)

### Core UI (01–20)
| # | Name | What it covers |
|---|------|----------------|
| 01 | `01_default_single_line` | Basic line plot, default theme |
| 02 | `02_empty_axes` | Empty axes state |
| 03 | `03_multi_series_with_labels` | Line + scatter with labels |
| 04 | `04_dense_data_10k` | Dense 10K-point line |
| 05 | `05_subplot_2x2_grid` | Subplot 2×2 layout |
| 06 | `06_scatter_2k_normal` | Large scatter plot |
| 07 | `07_inspector_panel_open` | Inspector panel |
| 08 | `08_command_palette_open` | Command palette overlay |
| 09 | `09_split_view_2panes` | 2-pane split view |
| 10 | `10_split_view_4panes` | 4-pane split view |
| 11 | `11_dark_theme` | Dark theme |
| 12 | `12_light_theme` | Light theme |
| 13 | `13_grid_enabled` | Grid visible |
| 14 | `14_legend_visible` | Legend overlay |
| 15 | `15_crosshair_mode` | Crosshair interaction mode |
| 16 | `16_zoomed_view` | Zoomed-in axes |
| 17 | `17_multiple_tabs` | Multiple figure tabs |
| 18 | `18_timeline_panel` | Timeline editor panel |
| 19 | `19_3d_surface` | 3D surface plot |
| 20 | `20_3d_scatter` | 3D scatter plot |

### 3D / Animation / Statistics (21–35)
| # | Name | What it covers |
|---|------|----------------|
| 21 | `21_3d_surface_labeled` | 3D surface + axis labels + bounding box |
| 22 | `22_3d_camera_side_view` | Camera azimuth=0, elevation=15 |
| 23 | `23_3d_camera_top_down` | Camera elevation=85 |
| 24 | `24_3d_line_helix` | 3D line (helix) |
| 25 | `25_3d_scatter_clusters` | 3D scatter two-color clusters |
| 26 | `26_3d_orthographic` | Orthographic projection |
| 27 | `27_inspector_series_stats` | Inspector with series stats selected |
| 28 | `28_inspector_axes_properties` | Inspector axes tab |
| 29 | `29_timeline_with_keyframes` | Timeline with keyframe tracks |
| 30 | `30_timeline_playing` | Timeline playhead mid-animation |
| 31 | `31_timeline_loop_region` | Timeline loop region overlay |
| 32 | `32_curve_editor` | Animation curve editor panel |
| 33 | `33_split_view_two_figures` | Split view with 2 real figures |
| 34 | `34_multi_series_full_chrome` | Legend + grid + crosshair together |
| 35 | `35_zoom_data_center_verify` | Zoomed-in data center (D12 regression) |

### Menu / Window / Interaction (36–51)
| # | Name | What it covers |
|---|------|----------------|
| 36 | `36_menu_bar_activated` | Menu bar open |
| 37 | `37_command_palette_with_search` | Command palette with search text |
| 38 | `38_inspector_with_knobs` | Inspector with knobs panel |
| 39 | `39_nav_rail_visible` | Nav rail toolbar |
| 40 | `40_tab_context_menu` | Tab right-click context menu |
| 41 | `41_window_resized_640x480` | Window at 640×480 |
| 42 | `42_window_resized_1920x600` | Window at 1920×600 (ultra-wide) |
| 43 | `43_window_resized_600x1080` | Window at 600×1080 (tall) |
| 44 | `44_window_resized_tiny_320x240` | Window at 320×240 (minimum) |
| 45 | `45_multi_window_primary` | Primary window (detach scenario) |
| 45b | `45b_multi_window_secondary` | Secondary window via `target_window` capture |
| 46 | `46_window_moved_top_left` | Window repositioned |
| 47 | `47_split_inspector_timeline_open` | Split view + inspector + timeline |
| 48 | `48_two_windows_side_by_side` | Two OS windows side by side |
| 49 | `49_fullscreen_mode` | Fullscreen canvas |
| 50 | `50_minimal_chrome_all_panels_closed` | Minimal chrome (all panels closed) |

> **Multi-window captures (45/45b):** `named_screenshot()` now accepts a `WindowContext*` parameter. Screenshots 45 and 45b are captured with `target_window` set to the primary and secondary `WindowContext*` respectively, so the capture fires only during that window's `end_frame`. Do not use `set_active_window` + `pump_frames` as a workaround — it gets overridden by `step()`.

---

## Command Reference

### Build & run

```bash
# Build
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)

# Design review (baseline capture)
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design

# Scenarios only
./build/tests/spectra_qa_agent --seed 42 --no-fuzz --output-dir /tmp/spectra_qa

# One scenario
./build/tests/spectra_qa_agent --scenario series_clipboard_selection --no-fuzz \
    --output-dir /tmp/spectra_qa

# List scenarios
./build/tests/spectra_qa_agent --list-scenarios

# Rebuild + test
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Refresh goldens after intentional visual changes
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | No `ERROR` or `CRITICAL` issues |
| `1` | At least one `ERROR` or `CRITICAL` issue |
| `2` | Crash — seed printed to stderr |

---

## Issue-to-File Map

| Issue type | Primary file | Typical touchpoint |
|---|---|---|
| Theme colors (grid, axis, text) | `src/ui/theme/theme.cpp` | `initialize_default_themes()` |
| Axes border rendering | `src/render/renderer.cpp` | `render_axis_border()` |
| Grid line rendering | `src/render/renderer.cpp` | `render_grid()` |
| Legend background/border | `src/ui/overlay/legend_interaction.cpp` | Legend window style |
| Crosshair labels | `src/ui/overlay/crosshair.cpp` | Label position + clamping |
| Status bar styling | `src/ui/imgui/imgui_integration.cpp` | `draw_status_bar()` |
| Tab bar styling | `src/ui/figures/tab_bar.cpp` | `draw_tabs()` |
| Nav rail icons | `src/ui/imgui/imgui_integration.cpp` | `draw_nav_rail()` |
| Command palette badges | `src/ui/commands/command_palette.cpp` | Badge/background/text |
| Timeline controls | `src/ui/animation/timeline_editor.cpp` | Transport visuals |
| Menu bar hover states | `src/ui/imgui/imgui_integration.cpp` | `draw_menubar()` |
| Inspector empty/series/axes states | `src/ui/overlay/inspector.cpp` | Inspector sections |
| Split view dividers | `src/ui/docking/split_view.cpp` | Pane boundaries |
| Design tokens | `src/ui/theme/design_tokens.hpp` | Spacing, radius, opacity |
| Vulkan text quality | `src/render/text_renderer.cpp` | Atlas + glyph quality |
| 3D surface colormaps | `src/gpu/shaders/surface3d.frag` | Colormap functions |
| Series clipboard UI feedback | `src/ui/commands/series_clipboard.cpp` | Clipboard state |
| Figure serialization dialogs | `src/ui/workspace/figure_serializer.cpp` | Save/load dialog |
| Multi-window screenshot capture | `src/render/vulkan/vk_backend.cpp` | `request_framebuffer_capture()` |
| Per-window active figure | `src/ui/app/window_ui_context.hpp` | `per_window_active_figure` |
| Series removal safety | `src/ui/overlay/data_interaction.hpp` | `notify_series_removed()` |

---

## Registered Commands (design-relevant)

| Command ID | Description |
|---|---|
| `view.reset` | Reset view |
| `view.autofit` | Auto-fit axes |
| `view.toggle_grid` | Toggle grid |
| `view.toggle_crosshair` | Toggle crosshair |
| `view.toggle_legend` | Toggle legend |
| `view.toggle_border` | Toggle border |
| `view.home` | Restore home limits |
| `view.fullscreen` | Fullscreen canvas |
| `view.zoom_in` / `view.zoom_out` | Zoom |
| `view.toggle_3d` | Toggle 2D/3D |
| `file.export_png` | Export PNG |
| `file.export_svg` | Export SVG |
| `file.save_figure` | Save figure (binary .spectra) |
| `file.load_figure` | Load figure (binary .spectra) |
| `file.save_workspace` | Save workspace |
| `file.load_workspace` | Load workspace |
| `series.copy` | Copy selected series to clipboard |
| `series.cut` | Cut selected series |
| `series.paste` | Paste clipboard into active figure |
| `series.delete` | Delete selected series |
| `series.deselect` | Deselect current series |
| `series.cycle_selection` | Cycle series selection forward |
| `edit.undo` / `edit.redo` | Undo / redo |
| `figure.new` | New figure tab |
| `figure.close` | Close figure tab |

---

## Fix Patterns

- **Theme alpha:** `Color(r, g, b, a)` — not opaque hex. Transparency requires float RGBA.
- **Position clamping:** Clamp labels/overlays to viewport bounds: `std::clamp(pos, vp_min + margin, vp_max - margin)`.
- **Styling:** Use ImGui draw-list primitives (`AddRectFilled`, `AddLine`) for pills, separators, borders.
- **Verify-before-fix:** `grep_search` + `read_file` the target before editing. Another agent may have already fixed it.
- **Split-view validation:** Ensure ≥2 figures exist before concluding split behavior is broken.
- **3D colormaps:** Colormap type is encoded in push constants — changes are in `surface3d.frag` and the `ColormapType` enum.
- **Multi-window captures:** Pass `WindowContext*` to `named_screenshot()` — do not `set_active_window` manually.

---

## Design Principles

1. **Data first** — minimize chrome, maximize plot area
2. **Clear hierarchy** — plot > axes/labels > chrome
3. **Consistent spacing** — use `ui::tokens::` scale
4. **Typography matters** — crisp text at every size
5. **Subtle depth** — shadows and opacity, not heavy borders
6. **Responsive feedback** — every interaction has visual feedback
7. **Accessible** — WCAG AA contrast, colorblind-safe palettes

---

## Known Constraints

- Requires a live display (no headless mode yet).
- Tracks CPU RSS only — no GPU memory visibility.
- Vulkan validation layer errors not monitored in real-time (see `QA_update.md` item #4).
- Screenshot races fixed via `request_framebuffer_capture()` (commit `4477b46`) — use that API, not `readback_framebuffer()`.

---

## Self-Update Protocol

The agent **may** update this file when it is 100% certain a change is correct. Never speculate — only update when verified by a successful build + design-review capture.

### Permitted self-updates

| Section | When to update | What to write |
|---|---|---|
| **Screenshot Coverage table** | A new screenshot is added/renamed in `qa_agent.cpp` | Add the new row with name, number, what it covers |
| **Issue-to-File Map** | A new subsystem file is discovered during a session | Add the file + issue type mapping |
| **Registered Commands** | A new command is added to `register_commands.cpp` | Add the command ID + description row |
| **Fix Patterns** | A new reliable fix pattern is confirmed by before/after | Append one bullet under Fix Patterns |
| **Known Constraints** | A limitation is removed or a new one discovered | Update the bullet |
| **Known screenshot count** | Design review screenshot count changes | Update all references (header + table + Known Constraints) |

### Forbidden self-updates

- Never change **Workflow steps** without human approval.
- Never change **Guardrails** without human approval.
- Never change **Design Principles** without human approval.
- Never remove an existing entry — mark it `~~deprecated~~` with a date instead.
- Never update if the verification command returned a non-zero exit code.

### Self-update procedure

1. Run `--design-review`, confirm exit code `0` and all expected screenshots present in `manifest.txt`.
2. Identify what is stale or missing in this file.
3. Apply only the update types listed in Permitted above.
4. Append a one-line entry to `REPORT.md` under `## Self-Update Log` with: date, what changed, why.

---

## Mandatory Session Self-Improvement

**This rule is non-negotiable: every session must produce exactly one improvement to this agent's detection capabilities, regardless of whether visual bugs were found.**

There is no such thing as "nothing to improve." If the design review found no issues, that means either the screenshot coverage is incomplete, or the triage criteria are too lenient. The agent must tighten one check, add one screenshot, or add one new issue type to the map — every single session.

### Required format (append to REPORT.md every session)

```
## Self-Improvement — YYYY-MM-DD
Improvement: <one sentence describing what was added/changed>
Motivation: <why the previous version would miss or underreport this>
Change: <file(s) edited OR new screenshot / new audit row added to this SKILL.md>
Next gap: <one sentence describing the next visual blind spot to tackle next session>
```

### How to pick an improvement

1. **If visual bugs were found:** Turn the most visually subtle finding into a new screenshot (add to the coverage table) or a new Fix Pattern. Ask: "What screenshot would have immediately highlighted this?"
2. **If no visual bugs were found:** Coverage is incomplete. Pick from the Improvement Backlog below, add the screenshot or check, and document the before/after.

### Improvement Backlog (consume one per session, add new ones as discovered)

| ID | Improvement | How to implement |
|---|---|---|
| DES-I1 | Add screenshot for error/empty state: figure with no series after deleting the last one | Add `51_empty_figure_after_delete` to `qa_agent.cpp` design review; capture empty axes with placeholder text |
| DES-I2 | Add screenshot for legend with 8+ series (overflow/truncation behavior) | Add `52_legend_overflow_8_series`; verify legend wraps or scrolls correctly |
| DES-I3 | Add screenshot for split view with mismatched axis ranges (zoomed vs auto-fit panes) | Add `53_split_view_mismatched_zoom`; verify no visual bleed between panes |
| DES-I4 | Audit all ImGui separator lines for 1px blurriness at non-integer DPI positions | In `imgui_integration.cpp`, check that `AddLine` calls use `ImFloor()`-snapped coordinates |
| DES-I5 | Add screenshot for command palette with 20+ results (scrollbar visibility) | Add `54_command_palette_scrolled`; verify scrollbar appears and doesn't overlap text |
| DES-I6 | Check toolbar icon alignment at 125% and 150% DPI scale | Add DPI-scaled design review run; verify nav rail icons don't have sub-pixel misalignment |
| DES-I7 | Add screenshot for 3D surface with Jet colormap (colorblind unfriendly — should show deprecation warning or badge) | Add `55_3d_surface_jet_colormap`; check for visual warning indicator |
| DES-I8 | Audit timeline editor transport controls for consistent icon sizing | In `timeline_editor.cpp` draw path, verify play/pause/stop icons use same pixel size constant |
| DES-I9 | Add screenshot for resize to 320×240 with all panels open simultaneously | Add `56_tiny_window_all_panels`; verify no panel overflow / occlusion beyond viewport |
| DES-I10 | Check that selection highlight color is visible against both dark and light plot backgrounds | Compute contrast ratio of `selection_color` against `plot_bg` in both themes |

---

## Live Report

The agent writes to `skills/qa-designer-agent/REPORT.md` at the end of every session.

### Report update procedure

After every run, open `REPORT.md` and:
1. Add a new `## Session YYYY-MM-DD HH:MM` block at the top (newest first).
2. Fill in: seed used, screenshot count confirmed, issues found (ID + severity), fixes applied, files changed, goldens updated, self-updates made.
3. Update the `## Current Status` summary at the very top of the file.
4. Never delete old session blocks — they form the audit trail.

---

## Guardrails

- Run `--design-review` at least once per task before drawing conclusions.
- Never assume an issue is fixed — verify with current code.
- Keep changes minimal and local; preserve architectural boundaries.
- Re-run design-review capture after any UI file change.
- Record QA-agent gaps in `plans/QA_update.md`, not inline.
- Self-updates to this file require a successful, verified run — never update speculatively.
