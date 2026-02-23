# QA Design Review — UI/UX Improvements

> Living document. Updated after each design review session with visual findings and concrete improvements.
> Last updated: 2026-02-23 | Screenshots: `/tmp/spectra_qa_design/design/`
> Session 2: 2026-02-23 — QA Designer Agent pass (D5, D6, D7/D16, D9, D10, D11)

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

### D2: Inspector Panel Opens Empty
- **Screenshot:** `07_inspector_panel_open.png`
- **Problem:** The inspector panel opens as a blank right-side panel with only a ">" chevron. No content, no figure properties, no series list, no axes controls. The panel takes ~25% of screen width but shows nothing.
- **Expected:** Inspector should show: figure title, axes properties (limits, labels, grid), series list with color/style controls, data statistics.
- **Root cause:** Inspector likely needs a selected figure/series context to populate. When opened without a selection, it shows nothing.
- **Fix:** (a) Show a "Select a series or axes to inspect" placeholder when nothing is selected. (b) Auto-select the active figure's first axes when inspector opens. (c) Always show figure-level properties at minimum.
- **Files:** `src/ui/overlay/inspector.cpp`, `src/ui/imgui/imgui_integration.cpp` (`draw_inspector()`)
- **Priority:** **P0** — empty panel is confusing and wastes screen space
- **Status:** 🔴 Open

### D3: Split View Commands Don't Visually Split
- **Screenshot:** `09_split_view_right.png`, `10_split_view_4_panes.png`
- **Problem:** After executing `view.split_right` and `view.split_down`, the UI looks identical to the unsplit state. No visible pane dividers, no second pane content.
- **Expected:** Split right should show two side-by-side panes with the same or different figures. Split down should show top/bottom panes.
- **Root cause:** Split commands may require multiple figures to be meaningful, or the DockSystem isn't rendering pane boundaries when only one figure exists per pane.
- **Fix:** (a) Show empty pane placeholder ("Drop a figure here" or "No figure assigned"). (b) Auto-assign figures to new panes when splitting. (c) Draw visible pane dividers even with single content.
- **Files:** `src/ui/docking/dock_system.cpp`, `src/ui/docking/split_view.cpp`
- **Priority:** **P1** — split view is a key feature that appears non-functional
- **Status:** 🟡 Open

---

## HIGH — Visual Polish

### D4: Tick Label Font Rendering Is Rough
- **Screenshot:** All screenshots — visible on axis labels "0", "5", "10", "-1", "1"
- **Problem:** Tick labels use a bold/blocky rendering style. Characters appear slightly aliased and heavy compared to the clean Inter font used in the menu bar and status bar. The "0" and "5" and "10" labels look chunky.
- **Expected:** Tick labels should use the same clean Inter font as the rest of the UI, at an appropriate size (11-12px), with proper anti-aliasing.
- **Root cause:** Tick labels are rendered by the Vulkan `Renderer::render_plot_text()` path, not ImGui. This path may use a different font atlas or rasterization.
- **Fix:** Ensure the plot text renderer uses the same Inter font atlas as ImGui, or improve the GPU text rendering quality. Consider using SDF (signed distance field) fonts for resolution-independent rendering.
- **Files:** `src/render/renderer.cpp` (text rendering), font atlas generation
- **Priority:** **P1** — affects perceived quality of every plot
- **Status:** 🟡 Open

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

### D8: Nav Rail Icons Are Abstract and Unlabeled
- **Screenshot:** All screenshots — left sidebar icons
- **Problem:** The nav rail has 8 icon buttons but they're abstract shapes that don't clearly communicate their function. From top to bottom: bar chart?, box zoom?, plus?, something highlighted (active), magnifier?, plus?, minus?, gear. Without tooltips visible in screenshots, users must guess.
- **Expected:** Icons should be recognizable at a glance. Consider: (a) adding text labels below icons when rail is expanded, (b) using more standard iconography, (c) showing tooltips on hover with descriptive text.
- **Fix:** Review icon choices against standard UI conventions. Add tooltip text for each button. Consider a "labels on hover" mode where the rail expands to show text on mouse proximity.
- **Files:** `src/ui/imgui/imgui_integration.cpp` (`draw_nav_rail()`), `src/ui/theme/icons.hpp`
- **Priority:** **P2** — discoverability issue
- **Status:** 🟡 Open

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

### D12: Zoomed View Shows Mostly Empty Space
- **Screenshot:** `16_zoomed_in.png`
- **Problem:** After 5× zoom_in, the view shows a mostly empty plot with the line barely visible in the bottom-right corner. The zoom appears to zoom toward the center of the view rather than toward the data.
- **Expected:** Zoom should center on the data or on the cursor position. After zooming, the data should remain visible and centered.
- **Fix:** Ensure `view.zoom_in` command zooms toward the center of the current data range, not the center of the axes limits. Or zoom toward the last cursor position.
- **Files:** `src/ui/input/input.cpp` (zoom logic), command registration
- **Priority:** **P2**
- **Status:** 🟡 Open

---

## LOW — Polish & Delight

### D13: Command Palette Could Show Keyboard Shortcuts More Prominently
- **Screenshot:** `08_command_palette_open.png`
- **Problem:** The command palette shows shortcuts as small right-aligned badges (e.g., "[" and "]" for step frame). The badges are small and hard to read. Category headers ("Panel", "Animation") are good but could use more visual weight.
- **Expected:** Shortcuts should be in a clearly visible monospace pill/badge. Category headers should have a subtle background or divider line.
- **Fix:** Use monospace font for shortcut badges. Add subtle `bg_tertiary` background to category headers. Increase badge font size slightly.
- **Files:** `src/ui/commands/command_palette.cpp`
- **Priority:** **P3**
- **Status:** 🟡 Open

### D14: Timeline Transport Controls Are Small
- **Screenshot:** `18_timeline_panel.png`
- **Problem:** The transport controls (⏮ ⏹ ▶ ⏭) are small and use basic Unicode symbols. They work but look dated compared to the rest of the UI.
- **Expected:** Transport controls should use the app's icon system, be larger (at least 24px hit targets), and have hover/active states.
- **Fix:** Use `ICON_MD` (20px) or `ICON_LG` (24px) for transport icons. Add hover highlight. Consider using custom SVG icons matching the nav rail style.
- **Files:** `src/ui/animation/timeline_editor.cpp` (ImGui draw)
- **Priority:** **P3**
- **Status:** 🟡 Open

### D15: Menu Bar Could Use Subtle Hover Effects
- **Screenshot:** All screenshots — top menu "File Data View Axes Transforms Tools"
- **Problem:** Menu items are plain text with no visible hover state in the screenshots. The menu bar feels static.
- **Expected:** Menu items should have a subtle background highlight on hover, matching modern app conventions (VS Code, Figma, etc.).
- **Fix:** Add `bg_tertiary` background on hover for menu bar items. Add smooth opacity transition.
- **Files:** `src/ui/imgui/imgui_integration.cpp` (`draw_menubar()`)
- **Priority:** **P3**
- **Status:** 🟡 Open

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

## Tracking

| ID | Category | Priority | Status | Description |
|----|----------|----------|--------|-------------|
| D1 | Functional | P0 | ✅ Fixed | New figures don't auto-switch |
| D2 | Functional | P0 | 🔴 | Inspector panel opens empty |
| D3 | Functional | P1 | 🟡 | Split view doesn't visually split |
| D4 | Visual | P1 | 🟡 | Tick label font rendering rough |
| D5 | Visual | P1 | ✅ Fixed | Grid lines too subtle |
| D6 | Visual | P1 | ✅ Fixed | Axes border faint/incomplete |
| D7 | Visual | P1 | ✅ Already Fixed | Legend has opaque black background |
| D8 | Layout | P2 | 🟡 | Nav rail icons abstract/unlabeled |
| D9 | Layout | P2 | ✅ Already Fixed | Tab bar lacks active indicator |
| D10 | Layout | P2 | ✅ Fixed | Status bar styling flat |
| D11 | Layout | P2 | ✅ Fixed | Crosshair label overlaps nav rail |
| D12 | Layout | P2 | 🟡 | Zoom centers on wrong point |
| D13 | Polish | P3 | 🟡 | Command palette shortcut badges |
| D14 | Polish | P3 | 🟡 | Timeline transport controls small |
| D15 | Polish | P3 | 🟡 | Menu bar hover effects |
| D16 | Visual | P2 | ✅ Already Fixed | Light theme legend contrast |

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
