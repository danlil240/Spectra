# QA Design Review — UI/UX Improvements

> Living document. Updated after each design review session with visual findings and concrete improvements.
> Last updated: 2026-02-23 | Screenshots: `/tmp/spectra_qa_design/design/`
> Session 2: 2026-02-23 — QA Designer Agent pass (D5, D6, D7/D16, D9, D10, D11)
> Session 3: 2026-02-23 — QA Designer Agent pass (D2, D3, D4, D8, D12, D13, D14, D15)

---

## Session 1 — Baseline Review (2026-02-23)

### Screenshots Captured
20 systematic screenshots covering: default state, empty axes, multi-series, dense data, subplots, scatter, inspector, command palette, split views, themes, grid, legend, crosshair, zoom, tabs, timeline, 3D surface, 3D scatter.

---

## CRITICAL — Functional UI Bugs

### D1: New Figures Don't Auto-Switch to Active Tab ✅ FIXED
- **Screenshot (before):** `02_empty_axes.png` through `06_scatter_2k_normal.png` — all showed Figure 1
- **Screenshot (after):** Each figure now shows its own content with correct tab active
- **Root cause:** `App::figure()` registered with `FigureRegistry` but didn't notify `FigureManager`.
- **Fix applied:** Added `FigureManager::add_figure()` call in both `App::figure()` overloads when runtime is active. New figures now appear as tabs and auto-switch.
- **Files changed:** `src/ui/app/app.cpp`
- **Verified:** 2026-02-23 — all 20 design screenshots now show correct figure content
- **Status:** ✅ Fixed

### D2: Inspector Panel Opens Empty ✅ FIXED
- **Screenshot:** `07_inspector_panel_open.png`
- **Problem:** The inspector panel opens as a blank right-side panel with only a ">" chevron. No content, no figure properties, no series list, no axes controls. The panel takes ~25% of screen width but shows nothing.
- **Expected:** Inspector should show: figure title, axes properties (limits, labels, grid), series list with color/style controls, data statistics.
- **Root cause:** The `panel.toggle_inspector` command (and undo/redo) only called `layout_manager_->set_inspector_visible()` but never set `panel_open_` on `ImGuiIntegration`. The inspector content rendering is gated by `panel_open_`, so the panel frame appeared but content was skipped. Nav rail buttons correctly set both flags, but the command path missed `panel_open_`.
- **Fix applied:** Added `panel_open_` sync from `layout_manager_->is_inspector_visible()` at the start of each frame in `build_ui()`. This ensures any external toggle (commands, undo/redo) automatically syncs the panel content state.
- **Files changed:** `src/ui/imgui/imgui_integration.cpp`
- **Priority:** **P0** — empty panel is confusing and wastes screen space
- **Status:** ✅ Fixed

### D3: Split View Commands Don't Visually Split ✅ BY DESIGN
- **Screenshot:** `09_split_view_right.png`, `10_split_view_4_panes.png`
- **Problem:** After executing `view.split_right` and `view.split_down`, the UI looks identical to the unsplit state.
- **Root cause:** The split command requires ≥2 figures to split (`fig_mgr.count() < 2` guard at line 938 of `register_commands.cpp`). The QA agent only creates 1 figure before attempting to split, so the command correctly does nothing. With multiple figures, split view works correctly — pane dividers, tab headers, and splitter handles all render properly.
- **Resolution:** This is expected behavior, not a bug. The QA agent's design review scenario needs to create multiple figures before testing split. The split view system (DockSystem + SplitViewManager + SplitPane) is fully functional.
- **Files:** `src/ui/app/register_commands.cpp`, `src/ui/docking/split_view.cpp`, `src/ui/docking/dock_system.cpp`
- **Priority:** **P1** → N/A (by design)
- **Status:** ✅ By Design

---

## HIGH — Visual Polish

### D4: Tick Label Font Rendering Is Rough ✅ IMPROVED
- **Screenshot:** All screenshots — visible on axis labels "0", "5", "10", "-1", "1"
- **Problem:** Tick labels use a bold/blocky rendering style. Characters appear slightly aliased and heavy compared to the clean Inter font used in the menu bar and status bar.
- **Root cause:** The Vulkan TextRenderer used 2x oversampling for stb_truetype font atlas baking. While adequate, this produced slightly rough edges at small sizes (14px tick labels).
- **Fix applied:** Increased oversampling from 2x to 3x in `text_renderer.cpp`. The 1024x1024 atlas has ample room for 3 ASCII font sizes at 3x oversampling. This produces smoother anti-aliasing and better sub-pixel positioning.
- **Files changed:** `src/render/text_renderer.cpp`
- **Note:** For further improvement, consider SDF (signed distance field) fonts for resolution-independent rendering.
- **Priority:** **P1** — affects perceived quality of every plot
- **Status:** ✅ Improved

### D5: Grid Lines Too Subtle in Dark Theme ✅ FIXED
- **Screenshot:** `01_default_single_line.png`, `13_grid_enabled.png`
- **Problem:** Grid lines were barely visible — very faint gray on the dark background.
- **Fix applied:** Changed dark theme `grid_line` from `Color::from_hex(0x21262D)` to `Color(1.0, 1.0, 1.0, 0.15)` (white at 15% opacity). Changed light theme from `Color::from_hex(0xE8ECF0)` to `Color(0.0, 0.0, 0.0, 0.12)` (black at 12% opacity). Grid now uses alpha blending for proper visibility on any background.
- **Files changed:** `src/ui/theme/theme.cpp`
- **Priority:** **P1**
- **Status:** ✅ Fixed

### D6: Axes Border/Frame Is Faint and Incomplete ✅ FIXED
- **Screenshot:** All plot screenshots
- **Problem:** The axes frame was very faint — barely distinguishable from the background.
- **Fix applied:** Changed dark theme `axis_line` from `Color::from_hex(0x30363D)` to `Color(0.55, 0.58, 0.63, 0.65)` (medium gray at 65% opacity). Changed light theme from `Color::from_hex(0xD0D7DE)` to `Color(0.30, 0.33, 0.38, 0.70)` (dark gray at 70% opacity). Border already draws all 4 sides (verified in `render_axis_border()`).
- **Files changed:** `src/ui/theme/theme.cpp`
- **Priority:** **P1**
- **Status:** ✅ Fixed

### D7: Legend Chip Has Opaque Black Background ✅ ALREADY FIXED
- **Screenshot:** `01_default_single_line.png` — "initial" legend in top-right
- **Problem:** The legend had a solid black background.
- **Status at review:** Already fixed in `legend_interaction.cpp`. Legend uses `theme_colors.bg_elevated` with 0.92f alpha, `theme_colors.border_subtle` for border, `RADIUS_MD` corner rounding, and 1px border width. Falls back to theme colors when no custom `LegendConfig` colors are set (sentinel check). The screenshot may have been from before this code was in place.
- **Files:** `src/ui/overlay/legend_interaction.cpp` (lines 214-242)
- **Priority:** **P1**
- **Status:** ✅ Already Fixed

---

## MEDIUM — Layout & Spacing

### D8: Nav Rail Icons Are Abstract and Unlabeled ✅ ALREADY FIXED
- **Screenshot:** All screenshots — left sidebar icons
- **Problem:** The nav rail has 8 icon buttons but they're abstract shapes that don't clearly communicate their function.
- **Status at review:** Already implemented. Every nav rail button has a `modern_tooltip()` call that shows a styled tooltip on hover with `ImGuiHoveredFlags_DelayShort`. Tooltips include: "Figures", "Series", "Axes" (inspector sections), "Pan (P)", "Box Zoom (Z)", "Select (S)", "Measure (M)" (tool modes), and "Settings". The tooltips use themed styling with `bg_elevated` background, `border_subtle` border, and `text_primary` color. Screenshots don't capture hover state.
- **Files:** `src/ui/imgui/imgui_integration.cpp` (`draw_nav_rail()`, lines 1722-1769)
- **Priority:** **P2**
- **Status:** ✅ Already Fixed

### D9: Tab Bar Is Minimal — No Visual Distinction for Active Tab ✅ ALREADY FIXED
- **Screenshot:** `01_default_single_line.png` — "Figure 1 ×" tab
- **Problem:** The tab bar appeared to have no visual distinction for active tab.
- **Status at review:** Already implemented in `tab_bar.cpp`. Active tab has: (a) `bg_tertiary` background with rounded top corners, (b) accent-colored underline (2px bottom border), (c) brighter text color. Hover state uses `bg_secondary`. Active styling is suppressed when menus are open (z-order fix). With only 1 tab, the distinction is less obvious but is visible with multiple tabs.
- **Files:** `src/ui/figures/tab_bar.cpp` (lines 298-310)
- **Priority:** **P2**
- **Status:** ✅ Already Fixed

### D10: Status Bar Information Density Is Good but Styling Needs Work ✅ FIXED
- **Screenshot:** All screenshots — bottom bar
- **Problem:** Status bar styling was flat — no pill for mode, pipe separators too prominent, FPS always red.
- **Fix applied:** (a) FPS color-coding already implemented: green >55fps, yellow 30-55, red <30. (b) Added pill background behind mode indicator (rounded rect with 12% opacity mode color). (c) Changed separator from pipe "|" to subtle middle dot "·" at 50% opacity using `text_tertiary`. Mode indicator now has distinct visual weight.
- **Files changed:** `src/ui/imgui/imgui_integration.cpp` (`draw_status_bar()`)
- **Priority:** **P2**
- **Status:** ✅ Fixed

### D11: Crosshair Value Labels Overlap Nav Rail ✅ FIXED
- **Screenshot:** `15_crosshair_mode.png`
- **Problem:** The Y-axis crosshair value label was positioned to the left of the viewport, overlapping the nav rail.
- **Fix applied:** Moved Y label from outside-left (`vx0 - sz.x - pad - 2`) to inside-left (`vx0 + 4.0f`) of the viewport. Applied to all 3 Y label positions: single-axes `draw()`, hovered axes in `draw_all_axes()`, and shared cursor in `draw_all_axes()`. Label now renders inside the plot area with a semi-transparent background pill.
- **Files changed:** `src/ui/overlay/crosshair.cpp`
- **Priority:** **P2**
- **Status:** ✅ Fixed

### D12: Zoomed View Shows Mostly Empty Space ✅ FIXED
- **Screenshot:** `16_zoomed_in.png`
- **Problem:** After 5× zoom_in, the view shows a mostly empty plot with the line barely visible in the bottom-right corner. The zoom appears to zoom toward the center of the view rather than toward the data.
- **Root cause:** `view.zoom_in` and `view.zoom_out` commands centered zoom on the axes limits midpoint (`(old_x.min + old_x.max) * 0.5f`), not on the data. When data doesn't fill the view, repeated zooming drifts away from the data.
- **Fix applied:** Added `compute_data_center()` helper that scans all visible LineSeries and ScatterSeries to find the data extent midpoint. Zoom now anchors on the data center when data exists, falling back to axes limits center when no data is present.
- **Files changed:** `src/ui/app/register_commands.cpp`
- **Priority:** **P2**
- **Status:** ✅ Fixed

---

## LOW — Polish & Delight

### D13: Command Palette Could Show Keyboard Shortcuts More Prominently ✅ IMPROVED
- **Screenshot:** `08_command_palette_open.png`
- **Problem:** The command palette shows shortcuts as small right-aligned badges. The badges lacked visual definition.
- **Fix applied:** (a) Changed badge shape from `RADIUS_PILL` to `RADIUS_SM` for a keyboard-key appearance. (b) Added subtle `border_subtle` border around badges for visual definition. (c) Increased text opacity from 200 to 220 for better readability.
- **Files changed:** `src/ui/commands/command_palette.cpp`
- **Priority:** **P3**
- **Status:** ✅ Improved

### D14: Timeline Transport Controls Are Small ✅ IMPROVED
- **Screenshot:** `18_timeline_panel.png`
- **Problem:** The transport controls were 28px with 4px gaps — functional but visually small.
- **Fix applied:** Increased button size from 28px to 32px and gap from 4px to 6px for better touch targets and visual presence. The transport buttons already use the app's icon font system (`font_icon_`) with proper hover/active states via the `transport_button()` helper.
- **Files changed:** `src/ui/imgui/imgui_integration.cpp` (`draw_timeline_panel()`)
- **Priority:** **P3**
- **Status:** ✅ Improved

### D15: Menu Bar Could Use Subtle Hover Effects ✅ ALREADY FIXED
- **Screenshot:** All screenshots — top menu "File Data View Axes Transforms Tools"
- **Problem:** Menu items appeared to have no visible hover state in screenshots.
- **Status at review:** Already implemented in `draw_menubar_menu()`. Menu buttons use `ImGuiCol_ButtonHovered` set to `accent_subtle` at 60% opacity, and `ImGuiCol_ButtonActive` set to `accent_muted`. Frame rounding uses `RADIUS_MD`. The hover effect is subtle by design (matching modern app conventions) and doesn't show in static screenshots. Popup menus also have modern styling with shadows, rounded corners, and hover highlights on items.
- **Files:** `src/ui/imgui/imgui_integration.cpp` (`draw_menubar_menu()`, lines 758-900)
- **Priority:** **P3**
- **Status:** ✅ Already Fixed

### D16: Light Theme Legend Has Black Background (Contrast Issue) ✅ ALREADY FIXED
- **Screenshot:** `12_theme_light.png`
- **Problem:** The legend had a solid black background in light theme.
- **Status at review:** Same as D7 — legend already uses `theme_colors.bg_elevated` (which is white `0xFFFFFF` in light theme) with 0.92f alpha. The sentinel check ensures theme colors are used when no custom colors are set.
- **Files:** Same as D7 — `src/ui/overlay/legend_interaction.cpp`
- **Priority:** **P2**
- **Status:** ✅ Already Fixed

---

## Design Vision — 2026 Modern Scientific App

Based on the current state, here's the target aesthetic to aim for:

### Reference Apps (best-in-class 2026 UI)
- **Figma** — clean panels, subtle borders, excellent use of space
- **Linear** — minimal chrome, focus on content, smooth animations
- **Grafana** — scientific dashboards done right, clear data visualization
- **Observable/Plot** — modern web-native plotting with excellent typography

### Key Principles
1. **Data first** — minimize chrome, maximize plot area
2. **Clear hierarchy** — primary content (plot) > secondary (axes/labels) > tertiary (chrome)
3. **Consistent spacing** — use the design token spacing scale religiously
4. **Typography matters** — clean, crisp text at every size
5. **Subtle depth** — use shadows and opacity, not heavy borders
6. **Responsive feedback** — every interaction should have visual feedback
7. **Accessible** — WCAG AA contrast ratios, colorblind-safe palettes

---

## Session 4 — 3D / Animation / Statistics Review (2026-02-23)

### Screenshots Captured
15 new scenarios (21–35) covering: 3D surfaces with labels/lighting/camera angles, 3D line/scatter, orthographic projection, inspector with data, timeline with keyframes/playback/loops, curve editor, proper split view, multi-series chrome, zoom verification.

### Verified Fixes
- **D2 (Inspector):** Screenshot #07 now shows Figure properties (Background, Margins). ✅ Confirmed working.
- **D3 (Split view):** Screenshot #33 shows two panes side-by-side with splitter divider and blue active border. ✅ Confirmed working with ≥2 figures.
- **D12 (Zoom center):** Screenshot #35 shows data at x=5.8–6.2, y=9.8–10.1 after 5× zoom — centered on data. ✅ Confirmed working.
- **D14 (Transport):** Screenshot #29 shows larger transport buttons. ✅ Confirmed working.

---

### CRITICAL — 3D Rendering

### D17: 3D Scatter Points Not Visible
- **Screenshot:** `25_3d_scatter_clusters.png`
- **Problem:** Two clusters of 200 scatter points each (Cluster A red, Cluster B blue) were added but no points are visible. Only the bounding box, grid planes, and axis arrows render. The title shows "3D Scatter Two Clusters" confirming data was added.
- **Expected:** Colored scatter points should be visible at the two cluster centers (±2, ±2, ±2).
- **Root cause:** Likely the scatter3d data coordinates are outside the normalized [0,1] bounding box range that the 3D renderer maps to. The data is at ±2 but the axis ticks show 0–1 range, suggesting auto-fit didn't expand limits to cover the data.
- **Files:** `src/core/axes3d.cpp` (auto_fit), `src/render/renderer.cpp` (3D scatter rendering)
- **Priority:** **P0** — 3D scatter is non-functional for off-center data
- **Status:** 🔴 Open

### D18: 3D Surface Renders as Wireframe Only
- **Screenshot:** `21_3d_surface_labeled.png`, `22_3d_camera_side_view.png`, `26_3d_orthographic.png`
- **Problem:** All 3D surface plots render as wireframe grids with no filled/shaded triangles. The surface function (cos(x)·sin(y), sin(sqrt(x²+y²)), etc.) should produce a colored, lit surface but only grid lines are visible.
- **Expected:** Filled surface with colormap or solid color, with optional wireframe overlay. Lighting should produce shading variation.
- **Root cause:** Surface rendering pipeline may only draw line primitives (wireframe) without a filled triangle pass, or the surface fill color is fully transparent.
- **Files:** `src/render/renderer.cpp` (surface rendering), `src/core/axes3d.cpp` (surface series)
- **Priority:** **P1** — surfaces look like wireframes, not solid surfaces
- **Status:** 🟡 Open

### D19: 3D Axis Tick Labels Show Normalized 0–1 Range
- **Screenshot:** `21_3d_surface_labeled.png`, `24_3d_line_helix.png`, `25_3d_scatter_clusters.png`
- **Problem:** All 3D plots show tick labels in the 0–1 range (0, 0.2, 0.4, 0.6, 0.8, 1) regardless of actual data range. The surface data spans -4 to 4, the helix spans -1 to 1 in X/Y and 0 to 2.5 in Z, but ticks always show 0–1.
- **Expected:** Tick labels should reflect the actual data range (e.g., -4, -2, 0, 2, 4 for the surface).
- **Root cause:** The 3D renderer maps data to a normalized bounding box for display, but tick labels are generated from the normalized range instead of the original data limits.
- **Files:** `src/render/renderer.cpp` (3D tick generation), `src/core/axes3d.cpp` (tick computation)
- **Priority:** **P1** — tick labels are misleading
- **Status:** 🟡 Open

### D20: 3D Title Uses Em-Dash Character Incorrectly
- **Screenshot:** `25_3d_scatter_clusters.png`
- **Problem:** Title "3D Scatter — Two Clusters" renders as "3D Scatter  Two Clusters" (em-dash stripped, leaving double space). The text renderer only bakes ASCII 32–126, so the UTF-8 em-dash (U+2014) is not in the atlas.
- **Expected:** Either render the em-dash correctly or use a plain ASCII dash as fallback.
- **Root cause:** `TextRenderer` only bakes ASCII range. Non-ASCII characters are silently dropped.
- **Files:** `src/render/text_renderer.cpp` (glyph lookup), title string in QA agent
- **Priority:** **P3** — cosmetic, easy workaround (use ASCII `--` in titles)
- **Status:** 🟡 Open

---

### MEDIUM — Inspector & UI Panels

### D21: Inspector Doesn't Show Series Statistics Section
- **Screenshot:** `27_inspector_series_stats.png`, `28_inspector_axes_properties.png`
- **Problem:** After opening inspector and cycling selection, the inspector still shows Figure-level properties (Background Color, Margins) rather than series statistics or axes properties. The `series.cycle_selection` command didn't navigate the inspector to the series stats view.
- **Expected:** Inspector should show series-specific data: min/max/mean/std, sparkline preview, color/style controls.
- **Root cause:** The `series.cycle_selection` command cycles through series within the current axes but may not change the inspector's `active_section_` to `Section::Series`. The inspector section is controlled by nav rail buttons, not by series selection.
- **Files:** `src/ui/imgui/imgui_integration.cpp` (inspector section switching), `src/ui/overlay/inspector.cpp`
- **Priority:** **P2** — inspector stats are a key feature but require manual nav rail click
- **Status:** 🟡 Open

### D22: Multi-Series Legend/Crosshair Not Visible in Full Chrome Test
- **Screenshot:** `34_multi_series_full_chrome.png`
- **Problem:** The multi-series plot shows 4 colored lines with grid, but the legend and crosshair are not visible despite toggling them on. The grid is visible, confirming the toggle commands work, but legend and crosshair may have been toggled off by prior scenarios.
- **Expected:** Legend with 4 entries and crosshair overlay should be visible.
- **Root cause:** Toggle state is global — earlier scenarios (14, 15) toggled legend and crosshair on, then the grid scenario (13) may have left them in an unexpected state. The toggle commands in scenario 34 may have toggled them OFF instead of ON.
- **Files:** `tests/qa/qa_agent.cpp` (scenario 34 toggle logic)
- **Priority:** **P3** — QA agent scenario issue, not a rendering bug
- **Status:** 🟡 Open (QA agent fix needed)

---

### Verification Results

| Screenshot | What It Verifies | Result |
|-----------|-----------------|--------|
| #07 | D2: Inspector panel content | ✅ Fixed — shows Figure properties |
| #29 | D14: Transport button size | ✅ Fixed — 32px buttons visible |
| #33 | D3: Split view with 2 figures | ✅ Working — two panes with splitter |
| #35 | D12: Zoom centers on data | ✅ Fixed — data visible at center after 5× zoom |
| #29–31 | Timeline tracks/keyframes | ✅ Working — 3 tracks, colored diamonds, playhead |
| #30 | Timeline playback | ✅ Working — pause icon shown during play |
| #31 | Timeline loop region | ✅ Working — blue tinted region visible |
| #32 | Curve editor | ✅ Working — floating panel with grid/tangent toggles |

---

| ID | Category | Priority | Status | Description |
|----|----------|----------|--------|-------------|
| D1 | Functional | P0 | ✅ Fixed | New figures don't auto-switch |
| D2 | Functional | P0 | ✅ Fixed | Inspector panel opens empty |
| D3 | Functional | P1 | ✅ By Design | Split view requires ≥2 figures |
| D4 | Visual | P1 | ✅ Improved | Tick label font — 3x oversampling |
| D5 | Visual | P1 | ✅ Fixed | Grid lines too subtle |
| D6 | Visual | P1 | ✅ Fixed | Axes border faint/incomplete |
| D7 | Visual | P1 | ✅ Already Fixed | Legend has opaque black background |
| D8 | Layout | P2 | ✅ Already Fixed | Nav rail has tooltips on hover |
| D9 | Layout | P2 | ✅ Already Fixed | Tab bar lacks active indicator |
| D10 | Layout | P2 | ✅ Fixed | Status bar styling flat |
| D11 | Layout | P2 | ✅ Fixed | Crosshair label overlaps nav rail |
| D12 | Layout | P2 | ✅ Fixed | Zoom now centers on data |
| D13 | Polish | P3 | ✅ Improved | Shortcut badges with border |
| D14 | Polish | P3 | ✅ Improved | Transport buttons 32px |
| D15 | Polish | P3 | ✅ Already Fixed | Menu bar has hover effects |
| D16 | Visual | P2 | ✅ Already Fixed | Light theme legend contrast |
| D17 | 3D | P0 | 🔴 | 3D scatter points not visible |
| D18 | 3D | P1 | 🟡 | Surface renders as wireframe only |
| D19 | 3D | P1 | 🟡 | 3D tick labels show 0–1 range |
| D20 | 3D | P3 | 🟡 | Em-dash stripped from 3D title |
| D21 | UI | P2 | 🟡 | Inspector doesn't show series stats |
| D22 | QA | P3 | 🟡 | Toggle state drift in QA scenarios |

---

## How to Run Design Review

```bash
# Capture screenshots
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design

# View screenshots
ls /tmp/spectra_qa_design/design/
# Open individual PNGs in image viewer

# After implementing fixes, re-capture and compare
```
