# Plotix UI Redesign — Roadmap & Progress Tracker

**Last Updated:** 2026-02-16 (All UI Redesign Phases Complete + 3D Phases 1 & 2 (partial) Complete)  
**Current Phase:** 3D Visualization — Phase 2 In Progress  
**Overall Progress:** UI Redesign Phase 1 ✅, Phase 2 ✅, Phase 3 ✅ | 3D Agent 1 ✅, Agent 2 ✅, Agent 5 ✅, Agent 6 ✅

---

## How to Update This File

> **ALL AGENTS MUST** update this roadmap at the end of their work session.  
> See the [Agent Update Protocol](#agent-update-protocol) section at the bottom of this file.

## Build Coordination Rules (READ FIRST)

> **Full protocol:** See `plans/PLOTIX_UI_REDESIGN.md` → Section 11.

| Rule | Summary |
|------|---------|
| **1. CMake guards** | All sources & tests use `if(EXISTS)` loops. Missing files are silently skipped. Add your file name to the `foreach()` list — it compiles only when the `.cpp` exists on disk. |
| **2. Standalone headers** | Every `.hpp` must compile on its own. Use forward decls, `#ifdef PLOTIX_USE_IMGUI` guards. |
| **3. Build only your targets** | Use `cmake --build build --target unit_test_YOUR_FEATURE` or `--target plotix`. Do NOT run full `cmake --build build` or `ctest` while parallel agents are working. |
| **4. Guard cross-agent deps** | Use null-check pointers (`if (ptr) { ptr->method(); }`) or `#ifdef` for optional modules. |
| **5. Don't touch in-progress files** | Check this roadmap for `🔄 In Progress` markers before modifying shared files. |

---

## Phase Overview

| Phase | Weeks | Status | Progress |
|-------|-------|--------|----------|
| **Phase 1** — Modern Foundation | 1–4 | ✅ Complete | 100% |
| **Phase 2** — Power User Features | 5–8 | ✅ Complete | 100% |
| **Phase 3** — Elite Differentiators | 9–12 | ✅ Complete | 100% |

---

## Phase 1 — Modern Foundation (Weeks 1–4) ✅

### Week 1 — Agent D (Theme) + Agent A (Layout)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Design tokens system | D | ✅ Done | `src/ui/design_tokens.hpp` |
| ThemeManager + dark theme | D | ✅ Done | `src/ui/theme.hpp`, `src/ui/theme.cpp` |
| Icon font system | D | ✅ Done | `src/ui/icons.hpp`, `src/ui/icons.cpp` |
| LayoutManager (zone rects) | A | ✅ Done | `src/ui/layout_manager.hpp`, `src/ui/layout_manager.cpp` |
| DockNode data structure | A | ✅ Done | `src/ui/dock_node.hpp`, `src/ui/dock_node.cpp` |

### Week 2 — Agent A (Layout finish) + Agent C (Inspector)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| TabBar widget | A | ✅ Done | `src/ui/tab_bar.hpp`, `src/ui/tab_bar.cpp` |
| Inspector panel | C | ✅ Done | `src/ui/inspector.hpp`, `src/ui/inspector.cpp` |
| Reusable widget library | C | ✅ Done | `src/ui/widgets.hpp`, `src/ui/widgets.cpp` |
| SelectionContext | C | ✅ Done | `src/ui/selection_context.hpp` |

### Week 3 — Agent B (Interactions) + Agent E (Data Interaction)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| AnimationController | B | ✅ Done | `src/ui/animation_controller.hpp`, `src/ui/animation_controller.cpp` |
| GestureRecognizer | B | ✅ Done | `src/ui/gesture_recognizer.hpp`, `src/ui/gesture_recognizer.cpp` |
| Animated zoom (150ms ease-out) | B | ✅ Done | `src/ui/input.hpp`, `src/ui/input.cpp` |
| Inertial pan (300ms decel) | B | ✅ Done | `src/ui/input.cpp` |
| Double-click auto-fit | B | ✅ Done | `src/ui/input.cpp` |
| Spring + CubicBezier easing | B | ✅ Done | `include/plotix/animator.hpp`, `src/anim/easing.cpp` |
| Hover tooltips | E | ✅ Done | `src/ui/tooltip.hpp`, `src/ui/tooltip.cpp` |
| Crosshair overlay | E | ✅ Done | `src/ui/crosshair.hpp`, `src/ui/crosshair.cpp` |
| Data markers (pin/remove) | E | ✅ Done | `src/ui/data_marker.hpp`, `src/ui/data_marker.cpp` |
| DataInteraction orchestrator | E | ✅ Done | `src/ui/data_interaction.hpp`, `src/ui/data_interaction.cpp` |
| Nearest-point spatial query | E | ✅ Done | `src/ui/data_interaction.cpp` |

### Week 4 — Agent D (Light theme) + Agent G (Transitions) + Agent H (Testing)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Light theme | D | ✅ Done | `src/ui/theme.cpp` |
| TransitionEngine (unified anims) | G | ✅ Done | `src/ui/transition_engine.hpp`, `src/ui/transition_engine.cpp` |
| Float/Color/AxisLimits animation | G | ✅ Done | `src/ui/transition_engine.cpp` |
| Inertial pan via TransitionEngine | G | ✅ Done | `src/ui/transition_engine.cpp` |
| Unit tests: Theme (56 tests) | H | ✅ Done | `tests/unit/test_theme.cpp` |
| Unit tests: CommandRegistry (18) | H | ✅ Done | `tests/unit/test_command_registry.cpp` |
| Unit tests: TransitionEngine (15+) | H | ✅ Done | `tests/unit/test_transition_engine.cpp` |
| Unit tests: UndoManager/TabBar (26) | H | ✅ Done | `tests/unit/test_undo_manager.cpp` |
| Benchmarks: UI perf (14) | H | ✅ Done | `tests/bench/bench_ui.cpp` |

**Phase 1 Test Summary:** All unit tests passing. AnimationController (11), Input (15), Easing (23), GestureRecognizer (10), TransitionEngine (37), Theme (56), CommandRegistry (18), TabBar/UndoManager (26), DataInteraction (11).

**Phase 2 Test Summary:** InspectorStats (28), UndoProperty (30), WorkspaceV2 (16), Phase2Integration (28), TimelineEditor (66), RecordingExport (30), ThemeColorblind (70). Total: 268 Phase 2 tests.

**Phase 3 Test Summary:** PlotStyle (86), SplitView (55), DockSystem (39), AxisLink (51), SharedCursor (20), DataTransform (64), ShortcutConfig (26), PluginAPI (31), WorkspaceV3 (16), KeyframeInterpolator (82), Phase3Integration (36), FigureManager (48), RegionSelect (15), LegendInteraction (13), CommandPaletteRegistry (30), ShortcutManager (25), UndoRedo (25), Workspace (12). Total: ~674 Phase 3 tests.

**Overall Test Count:** ~1,200+ unit tests across all phases, 30+ golden image tests, 100+ benchmarks.

---

## Phase 2 — Power User Features (Weeks 5–8) ✅

### Week 5 — Agent F (Command Palette) + Agent E (Data Interaction)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Command palette UI (Ctrl+K) | F | ✅ Done | `src/ui/command_palette.hpp/.cpp` |
| CommandRegistry (register/search/execute) | F | ✅ Done | `src/ui/command_registry.hpp/.cpp` |
| ShortcutManager (configurable keybindings) | F | ✅ Done | `src/ui/shortcut_manager.hpp/.cpp` |
| UndoManager (push/undo/redo stack) | F | ✅ Done | `src/ui/undo_manager.hpp/.cpp` |
| Workspace save/load | F | ✅ Done | `src/ui/workspace.hpp/.cpp` |
| Region selection + mini-toolbar | E | ✅ Done | `src/ui/region_select.hpp/.cpp` |
| Legend click-to-toggle visibility | E | ✅ Done | `src/ui/legend_interaction.hpp/.cpp` |
| Legend drag-to-reposition | E | ✅ Done | `src/ui/legend_interaction.hpp/.cpp` |
| Crosshair shared across subplots | E | ✅ Done | `src/ui/crosshair.cpp` (draw_all_axes)

**Pre-existing foundations for Week 5:**
- `CommandRegistry` class exists with register/search/execute API
- `ShortcutManager` header exists with Shortcut struct, KeyMod flags, parse/to_string
- `CommandQueue` (lock-free SPSC) exists
- Crosshair and DataInteraction already functional from Week 3

### Week 6 — Agent A (Multi-figure tabs) + Agent C (Statistics)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| FigureManager (multi-figure lifecycle) | A | ✅ Done | `src/ui/figure_manager.hpp/.cpp` |
| Multi-figure tab switching | A | ✅ Done | `src/ui/app.cpp`, `src/ui/tab_bar.cpp` |
| Tab context menu (rename, duplicate, close) | A | ✅ Done | `src/ui/tab_bar.cpp` |
| Per-figure state preservation | A | ✅ Done | `src/ui/figure_manager.cpp` |
| Series statistics display (min, max, mean, median, std, percentiles, count) | C | ✅ Done | `src/ui/inspector.hpp/.cpp` |
| Collapsible inspector sections with smooth animation | C | ✅ Done | `src/ui/widgets.hpp/.cpp`, `src/ui/inspector.cpp` |
| Sparkline data preview in inspector | C | ✅ Done | `src/ui/inspector.cpp`, `src/ui/widgets.hpp/.cpp` |
| Enhanced widget library (sparkline, badge, stat_row, separator_label, int_drag, progress_bar) | C | ✅ Done | `src/ui/widgets.hpp/.cpp` |
| Axes aggregate statistics section | C | ✅ Done | `src/ui/inspector.hpp/.cpp` |
| X-axis statistics (min, max, range, mean) | C | ✅ Done | `src/ui/inspector.cpp` |

### Week 7 — Agent F (Undo/Redo) + Agent B (Box Zoom)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Undoable property system | F | ✅ Done | `src/ui/undoable_property.hpp` (NEW) |
| Undo/redo for all property changes | F | ✅ Done | `src/ui/app.cpp` (modified — all commands now undoable) |
| Workspace v2 format (full state) | F | ✅ Done | `src/ui/workspace.hpp/.cpp` (enhanced) |
| Workspace autosave/recovery | F | ✅ Done | `src/ui/workspace.hpp/.cpp` |
| Interaction state save/restore | F | ✅ Done | `src/ui/workspace.hpp/.cpp`, `src/ui/app.cpp` |
| Box zoom with animated overlay | B | ✅ Done | `src/ui/input.cpp` |
| Box zoom Ctrl key fix (mods parameter) | B | ✅ Done | `src/ui/input.cpp`, `src/ui/glfw_adapter.cpp` |
| Double-click auto-fit | B | ✅ Done | `src/ui/input.cpp` |

### Week 8 — Agent D (Colorblind) + Agent E (Region Select) + Agent H (Tests)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Colorblind-safe palette (8 palettes: Okabe-Ito, Tol Bright/Muted, IBM, Wong, Viridis, Monochrome) | D | ✅ Done | `src/ui/theme.hpp`, `src/ui/theme.cpp` |
| Theme export/import (JSON serialization) | D | ✅ Done | `src/ui/theme.cpp` |
| Color utility methods (luminance, contrast_ratio, sRGB↔linear, HSL) | D | ✅ Done | `src/ui/theme.hpp` |
| CVD simulation (Protanopia, Deuteranopia, Tritanopia, Achromatopsia) | D | ✅ Done | `src/ui/theme.hpp`, `src/ui/theme.cpp` |
| Animated palette transitions | D | ✅ Done | `src/ui/theme.hpp`, `src/ui/theme.cpp` |
| ThemeManager::update() mutation bug fix | D | ✅ Done | `src/ui/theme.cpp` |
| Region selection + mini-toolbar | E | ✅ Done | `src/ui/region_select.hpp/.cpp` |
| Golden image tests (10 scenes + 4 framework) | H | ✅ Done | `tests/golden/golden_test_phase2.cpp` (NEW) |
| Phase 2 benchmarks (38 benchmarks) | H | ✅ Done | `tests/bench/bench_phase2.cpp` (NEW) |
| Phase 2 integration tests (28 tests) | H | ✅ Done | `tests/unit/test_phase2_integration.cpp` (NEW) |
| Timeline editor (playhead, tracks, keyframes, scrub, snap) | G | ✅ Done | `src/ui/timeline_editor.hpp/.cpp` (NEW) |
| Recording export (PNG seq, GIF, MP4) | G | ✅ Done | `src/ui/recording_export.hpp/.cpp` (NEW) |

**Phase 2 Exit Criteria:**
- [x] Command palette opens <16ms, fuzzy search instant
- [x] Undo/redo works for all property changes
- [x] Multi-figure tabs functional (1–20 figures)
- [x] Crosshair shared across subplots
- [x] Data markers persist through zoom/pan
- [x] Workspace save/load restores full state
- [x] Nearest-point <0.1ms for 100K points
- [x] Undo <1ms

---

## Phase 3 — Elite Differentiators (Weeks 9–12) ✅

### Week 9 — Agent A (Docking) + Plot Customization System

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Docking system | A | ✅ Done | `src/ui/dock_system.hpp/.cpp` |
| Split view (horizontal/vertical) | A | ✅ Done | `src/ui/split_view.hpp/.cpp` |
| MATLAB-style plot customization (LineStyle, MarkerStyle enums, PlotStyle struct) | — | ✅ Done | `include/plotix/plot_style.hpp` (NEW) |
| Format string parser (`"r--o"`, `"b:*"`, etc.) | — | ✅ Done | `include/plotix/plot_style.hpp` |
| Runtime style mutation API (line_style, marker_style, marker_size, opacity) | — | ✅ Done | `include/plotix/series.hpp`, `src/core/series.cpp` |
| `Axes::plot()` convenience method | — | ✅ Done | `include/plotix/axes.hpp`, `src/core/axes.cpp` |
| GPU push constants for styles (96-byte SeriesPushConstants) | — | ✅ Done | `src/render/backend.hpp` |
| Shader dash pattern rendering (line.vert/frag) | — | ✅ Done | `src/gpu/shaders/line.vert`, `src/gpu/shaders/line.frag` |
| Shader SDF marker shapes — 18 types (scatter.vert/frag) | — | ✅ Done | `src/gpu/shaders/scatter.vert`, `src/gpu/shaders/scatter.frag` |
| Inspector UI: style dropdowns, marker size, opacity sliders | — | ✅ Done | `src/ui/inspector.cpp` |
| Workspace serialization for style fields | — | ✅ Done | `src/ui/workspace.hpp` |
| Undoable property helpers for styles | — | ✅ Done | `src/ui/undoable_property.hpp` |
| Plot styles demo example | — | ✅ Done | `examples/plot_styles_demo.cpp` (NEW) |
| Unit tests (86 tests, 12 suites) | — | ✅ Done | `tests/unit/test_plot_style.cpp` (NEW) |

### Week 10 — Agent E (Linked Axes) + Agent B (Multi-axis)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Shared cursor across subplots | E | ✅ Done | `src/ui/axis_link.hpp` (SharedCursor struct + methods), `src/ui/axis_link.cpp`, `src/ui/crosshair.cpp` (draw_all_axes enhanced), `src/ui/data_interaction.hpp/.cpp` (wiring) |
| Multi-axis linking (X/Y/Both, propagate zoom/pan/limits, serialization) | B | ✅ Done | `src/ui/axis_link.hpp`, `src/ui/axis_link.cpp` |
| Data transforms (pipeline, registry, 14 built-in types) | E | ✅ Done | `src/ui/data_transform.hpp` (NEW), `src/ui/data_transform.cpp` (NEW) |
| Shared cursor tests (20 tests) | E | ✅ Done | `tests/unit/test_shared_cursor.cpp` (NEW) |
| Data transform tests (64 tests, 19 suites) | E | ✅ Done | `tests/unit/test_data_transform.cpp` (NEW) |

### Week 11 — Agent G (Timeline) + Agent F (Productivity & Plugin Architecture)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Timeline editor | G | ✅ Done (pulled to Week 8) | |
| Recording export (MP4/GIF) | G | ✅ Done (pulled to Week 8) | |
| Theme export/import (JSON) | D | ✅ Done (pulled to Week 8) | |
| KeyframeInterpolator (7 interp modes, typed keyframes, property bindings, serialization) | G | ✅ Done | `src/ui/keyframe_interpolator.hpp/.cpp` (NEW) |
| AnimationCurveEditor (curve viz, hit-testing, tangent drag, zoom/pan, multi-channel) | G | ✅ Done | `src/ui/animation_curve_editor.hpp/.cpp` (NEW) |
| Timeline↔Interpolator integration (animated tracks, auto-evaluate on advance, serialize) | G | ✅ Done | `src/ui/timeline_editor.hpp/.cpp` (MODIFIED) |
| Multi-pane recording (composite render, auto-grid layout, custom pane rects) | G | ✅ Done | `src/ui/recording_export.hpp/.cpp` (MODIFIED) |
| Keyframe interpolator + curve editor + integration tests (82 tests, 22 suites) | G | ✅ Done | `tests/unit/test_keyframe_interpolator.cpp` (NEW) |
| Shortcut persistence (save/load custom keybindings) | F | ✅ Done | `src/ui/shortcut_config.hpp/.cpp` (NEW) |
| Plugin-ready command architecture (C ABI, PluginManager) | F | ✅ Done | `src/ui/plugin_api.hpp/.cpp` (NEW) |
| Workspace v3 (line_style, marker_style, dash_pattern, axis links, transforms, timeline, shortcuts, plugins) | F | ✅ Done | `src/ui/workspace.hpp/.cpp` (modified) |
| Shortcut config tests (26 tests) | F | ✅ Done | `tests/unit/test_shortcut_config.cpp` (NEW) |
| Plugin API tests (31 tests) | F | ✅ Done | `tests/unit/test_plugin_api.cpp` (NEW) |
| Workspace v3 tests (16 tests) | F | ✅ Done | `tests/unit/test_workspace_v3.cpp` (NEW) |

### Week 12 — Agent H (Final Polish)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Full test suite (>80% coverage) | H | ✅ Done | `tests/unit/test_phase3_integration.cpp` (36 tests) |
| Performance benchmarks | H | ✅ Done | `tests/bench/bench_phase3.cpp` (~50 benchmarks) |
| Golden image tests | H | ✅ Done | `tests/golden/golden_test_phase3.cpp` (8 scenes) |

**Phase 3 Exit Criteria:**
- [x] Docking layout <0.5ms
- [x] 100K points interactive at 60fps
- [x] Linked axes sync correctly
- [x] Timeline editor functional
- [x] Plugin-ready architecture
- [x] MATLAB-style plot customization (line styles, marker shapes, format strings)
- [x] GPU-accelerated dash patterns and SDF marker rendering
- [x] Runtime style editing via inspector UI

---

## File Inventory

### Files Created (Phase 1)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|
| `src/ui/design_tokens.hpp` | D | 1 | ✅ Header-only |
| `src/ui/theme.hpp` / `.cpp` | D | 1 | ✅ Yes |
| `src/ui/icons.hpp` / `.cpp` | D | 1 | ✅ Yes |
| `src/ui/layout_manager.hpp` / `.cpp` | A | 1 | ✅ Yes |
| `src/ui/dock_node.hpp` / `.cpp` | A | 1 | ✅ Yes |
| `src/ui/tab_bar.hpp` / `.cpp` | A | 2 | ✅ Yes |
| `src/ui/inspector.hpp` / `.cpp` | C | 2 | ✅ Yes |
| `src/ui/widgets.hpp` / `.cpp` | C | 2 | ✅ Yes |
| `src/ui/selection_context.hpp` | C | 2 | ✅ Header-only |
| `src/ui/animation_controller.hpp` / `.cpp` | B | 3 | ✅ Yes |
| `src/ui/gesture_recognizer.hpp` / `.cpp` | B | 3 | ✅ Yes |
| `src/ui/tooltip.hpp` / `.cpp` | E | 3 | ✅ Yes |
| `src/ui/crosshair.hpp` / `.cpp` | E | 3 | ✅ Yes |
| `src/ui/data_marker.hpp` / `.cpp` | E | 3 | ✅ Yes |
| `src/ui/data_interaction.hpp` / `.cpp` | E | 3 | ✅ Yes |
| `src/ui/transition_engine.hpp` / `.cpp` | G | 4 | ✅ Yes |
| `src/ui/command_registry.hpp` / `.cpp` | F | 4 | ✅ Yes (ImGui block) |
| `src/ui/shortcut_manager.hpp` | F | 4 | ⚠️ Header-only, no .cpp yet |
| `src/ui/command_queue.hpp` | F | 4 | ✅ Header-only |
| `src/ui/region_select.hpp` | E | 4 | ⚠️ Header-only, no .cpp yet |

### Files Modified (Phase 2 — Week 6, Agent C)

| File | Agent | Week | Changes |
|------|-------|------|---------|
| `src/ui/widgets.hpp` | C | 6 | Added SectionAnimState, animated section begin/end, sparkline, badge, progress_bar, separator_label, int_drag_field, stat_row, stat_row_colored |
| `src/ui/widgets.cpp` | C | 6 | Implemented all new widgets + section animation state tracking |
| `src/ui/inspector.hpp` | C | 6 | Added draw_series_sparkline, draw_axes_statistics, sec_preview_, sec_axes_stats_ |
| `src/ui/inspector.cpp` | C | 6 | Enhanced statistics (median, percentiles, X stats), sparkline preview, animated sections, axes aggregate stats |
| `src/ui/imgui_integration.cpp` | C | 6 | Added widgets.hpp include, update_section_animations() call in build_ui |
| `tests/CMakeLists.txt` | C | 6 | Added test_inspector_stats |

### Files Created (Phase 2 — Week 7, Agent F)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|
| `src/ui/undoable_property.hpp` | F | 7 | ✅ Header-only |
| `tests/unit/test_undo_property.cpp` | F | 7 | ✅ Yes |
| `tests/unit/test_workspace_v2.cpp` | F | 7 | ✅ Yes |

### Files Modified (Phase 2 — Week 7, Agent F)

| File | Agent | Week | Changes |
|------|-------|------|---------|
| `src/ui/workspace.hpp` | F | 7 | Added InteractionState, FigureState v2 fields, autosave API, FORMAT_VERSION=2 |
| `src/ui/workspace.cpp` | F | 7 | v2 serialization (interaction, markers, opacity, tab titles), autosave impl, enhanced apply() |
| `src/ui/app.cpp` | F | 7 | All 15+ commands now undoable, workspace save captures full state, load restores full state |
| `tests/CMakeLists.txt` | F | 7 | Added test_undo_property, test_workspace_v2 |

### Files Created (Phase 2 — Week 8, Agent H)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|
| `tests/golden/golden_test_phase2.cpp` | H | 8 | ✅ Yes (golden target) |
| `tests/bench/bench_phase2.cpp` | H | 8 | ✅ Yes (bench target) |
| `tests/unit/test_phase2_integration.cpp` | H | 8 | ✅ Yes |

### Files Created (Phase 2 — Week 8, Agent G)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|
| `src/ui/timeline_editor.hpp` / `.cpp` | G | 8 | ✅ Yes (UI sources) |
| `src/ui/recording_export.hpp` / `.cpp` | G | 8 | ✅ Yes (UI sources) |
| `tests/unit/test_timeline_editor.cpp` | G | 8 | ✅ Yes |
| `tests/unit/test_recording_export.cpp` | G | 8 | ✅ Yes |

### Files Modified (Phase 2 — Week 8, Agent G)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `CMakeLists.txt` | G | 8 | Added timeline_editor.cpp, recording_export.cpp to UI sources |
| `tests/CMakeLists.txt` | G | 8 | Added test_timeline_editor, test_recording_export |
| `include/plotix/fwd.hpp` | G | 8 | Added TimelineEditor, RecordingSession forward declarations |

### Files Created (Phase 3 — Week 12, Agent 6 - 3D Animation)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|  
| `src/ui/camera_animator.hpp` / `.cpp` | 6 | 12 | ✅ Yes (UI sources) |
| `tests/unit/test_camera_animator.cpp` | 6 | 12 | ✅ Yes |

### Files Modified (Phase 3 — Week 12, Agent 6 - 3D Animation)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `src/ui/transition_engine.hpp` / `.cpp` | 6 | 12 | Added animate_camera() |
| `src/ui/keyframe_interpolator.hpp` / `.cpp` | 6 | 12 | Added bind_camera() for parameter channels |
| `src/ui/timeline_editor.hpp` / `.cpp` | 6 | 12 | Added set_camera_animator() |
| `include/plotix/fwd.hpp` | 6 | 12 | Added CameraAnimator forward decl |
| `CMakeLists.txt` | 6 | 12 | Added camera_animator.cpp |
| `tests/CMakeLists.txt` | 6 | 12 | Added test_camera_animator |

### Files Modified (Phase 2 — Week 8, Agent D)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `src/ui/theme.hpp` | D | 8 | Added Color utility methods (luminance, contrast_ratio, to_linear, to_srgb, to_hsl, from_hsl, ==, !=), CVDType enum, DataPalette metadata (description, safe_for, operator[], is_safe_for), simulate_cvd(), register_data_palette(), transition_palette(), display_colors_ for non-mutating transitions, palette transition state |
| `src/ui/theme.cpp` | D | 8 | Fixed update() mutation bug (uses display_colors_ instead of mutating stored theme), added 6 new colorblind palettes (Tol Bright/Muted, IBM, Wong, Viridis, Monochrome), implemented export_theme/import_theme/save_current_as_default/load_default with JSON serialization, CVD simulation, animated palette transitions |
| `tests/CMakeLists.txt` | D | 8 | Added test_theme_colorblind |

### Files Created (Phase 2 — Week 8, Agent D)

| File | Agent | Week | In Build? |
|------|-------|------|----------|
| `tests/unit/test_theme_colorblind.cpp` | D | 8 | ✅ Yes |

### Files Created (Phase 2 — Week 5, Agent F)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|  
| `src/ui/command_palette.hpp` / `.cpp` | F | 5 | ✅ Yes (ImGui sources) |
| `src/ui/command_registry.hpp` / `.cpp` | F | 5 | ✅ Yes (ImGui sources) |
| `src/ui/shortcut_manager.hpp` / `.cpp` | F | 5 | ✅ Yes (ImGui sources) |
| `src/ui/undo_manager.hpp` / `.cpp` | F | 5 | ✅ Yes (ImGui sources) |
| `src/ui/workspace.hpp` / `.cpp` | F | 5 | ✅ Yes (UI sources) |
| `tests/unit/test_command_palette_registry.cpp` | F | 5 | ✅ Yes |
| `tests/unit/test_shortcut_manager.cpp` | F | 5 | ✅ Yes |
| `tests/unit/test_undo_redo.cpp` | F | 5 | ✅ Yes |
| `tests/unit/test_workspace.cpp` | F | 5 | ✅ Yes |

### Files Created (Phase 2 — Week 5, Agent E)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|  
| `src/ui/region_select.hpp` / `.cpp` | E | 5 | ✅ Yes (UI sources) |
| `src/ui/legend_interaction.hpp` / `.cpp` | E | 5 | ✅ Yes (UI sources) |
| `tests/unit/test_region_select.cpp` | E | 5 | ✅ Yes |
| `tests/unit/test_legend_interaction.cpp` | E | 5 | ✅ Yes |

### Files Created (Phase 2 — Week 6, Agent A)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|  
| `src/ui/figure_manager.hpp` / `.cpp` | A | 6 | ✅ Yes (UI sources) |
| `tests/unit/test_figure_manager.cpp` | A | 6 | ✅ Yes |

### Files Modified (Phase 2 — Week 8, Agent H)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `tests/CMakeLists.txt` | H | 8 | Added test_phase2_integration, golden_image_tests_phase2, bench_phase2 |

### Files Created (Phase 3 — Week 10, Agent B)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|
| `src/ui/axis_link.hpp` / `.cpp` | B | 10 | ✅ Yes (UI sources) |
| `tests/unit/test_axis_link.cpp` | B | 10 | ✅ Yes |

### Files Modified (Phase 3 — Week 10, Agent B)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `include/plotix/fwd.hpp` | B | 10 | Added AxisLinkManager forward declaration |
| `src/ui/input.hpp` | B | 10 | Added AxisLinkManager pointer, setter/getter |
| `src/ui/input.cpp` | B | 10 | Added axis_link.hpp include, propagate zoom/pan/box-zoom/auto-fit to linked axes |
| `CMakeLists.txt` | B | 10 | Added axis_link.cpp to PLOTIX_UI_SOURCES |
| `tests/CMakeLists.txt` | B | 10 | Added test_axis_link to PLOTIX_UNIT_TESTS |

### Files Created (Phase 3 — Week 9, Plot Customization)

| File | Agent | Week | In Build? |
|------|-------|------|----------|
| `include/plotix/plot_style.hpp` | — | 9 | ✅ Header-only |
| `tests/unit/test_plot_style.cpp` | — | 9 | ✅ Yes |
| `examples/plot_styles_demo.cpp` | — | 9 | ✅ Yes |

### Files Modified (Phase 3 — Week 9, Plot Customization)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `include/plotix/series.hpp` | — | 9 | Added PlotStyle integration, format() methods, using declarations for name hiding fix |
| `src/core/series.cpp` | — | 9 | Added plot_style(), LineSeries::format(), ScatterSeries::format() |
| `include/plotix/axes.hpp` | — | 9 | Added plot(x, y, fmt) and plot(x, y, PlotStyle) convenience methods |
| `src/core/axes.cpp` | — | 9 | Implemented Axes::plot() methods |
| `src/render/backend.hpp` | — | 9 | Extended SeriesPushConstants to 96 bytes (line_style, marker_type, dash_pattern, opacity) |
| `src/render/renderer.cpp` | — | 9 | Updated render_series() to populate style push constants, render markers for LineSeries |
| `src/gpu/shaders/line.vert` | — | 9 | Full push constant block, v_cumulative_dist for dash patterns |
| `src/gpu/shaders/line.frag` | — | 9 | Dash pattern discard logic, opacity support |
| `src/gpu/shaders/scatter.vert` | — | 9 | Full push constant block, 6-vertex triangle list |
| `src/gpu/shaders/scatter.frag` | — | 9 | 18 SDF marker shapes (circle, square, diamond, triangles, pentagon, hexagon, star, plus, cross, filled variants), opacity |
| `src/gpu/shaders/grid.vert` | — | 9 | Updated push constant block to match |
| `src/gpu/shaders/grid.frag` | — | 9 | Updated push constant block to match |
| `src/ui/inspector.cpp` | — | 9 | Added Line Style dropdown, Marker dropdown (18 types), Marker Size slider, Opacity slider |
| `src/ui/workspace.hpp` | — | 9 | Added line_style, marker_style fields to SeriesState |
| `src/ui/undoable_property.hpp` | — | 9 | Added undoable setters for line_style, marker_style, marker_size, opacity |
| `examples/CMakeLists.txt` | — | 9 | Added plot_styles_demo |
| `tests/CMakeLists.txt` | — | 9 | Added test_plot_style |

### Files Created (Phase 3 — Week 11, Agent F)

| File | Agent | Week | In Build? |
|------|-------|------|-----------|
| `src/ui/shortcut_config.hpp` / `.cpp` | F | 11 | ✅ Yes (ImGui sources) |
| `src/ui/plugin_api.hpp` / `.cpp` | F | 11 | ✅ Yes (ImGui sources) |
| `tests/unit/test_shortcut_config.cpp` | F | 11 | ✅ Yes |
| `tests/unit/test_plugin_api.cpp` | F | 11 | ✅ Yes |
| `tests/unit/test_workspace_v3.cpp` | F | 11 | ✅ Yes |

### Files Modified (Phase 3 — Week 11, Agent F)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `src/ui/workspace.hpp` | F | 11 | FORMAT_VERSION bumped to 3. Added dash_pattern to SeriesState. Added axis_link_state, TransformState, ShortcutOverride, TimelineState, plugin_state, data_palette_name to WorkspaceData |
| `src/ui/workspace.cpp` | F | 11 | v3 serialization: line_style, marker_style, dash_pattern in series. Top-level: axis_link_state, transforms, shortcut_overrides, timeline, plugin_state, data_palette_name. Backward-compatible v2 deserialization |
| `include/plotix/fwd.hpp` | F | 11 | Added ShortcutConfig, PluginManager, PluginEntry forward declarations |
| `CMakeLists.txt` | F | 11 | Added shortcut_config.cpp, plugin_api.cpp to ImGui-dependent UI sources |
| `tests/CMakeLists.txt` | F | 11 | Added test_shortcut_config, test_plugin_api, test_workspace_v3 |

### Files Modified (Phase 3 — Week 9, Agent A)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `src/ui/imgui_integration.hpp` | A | 9 | Added DockSystem pointer, draw_split_view_splitters() |
| `src/ui/imgui_integration.cpp` | A | 9 | Splitter handles, active pane highlight, drop zone overlay |
| `src/ui/app.cpp` | A | 9 | DockSystem instantiation, split commands, per-pane layout, workspace dock_state |
| `src/ui/workspace.hpp` | A | 9 | Added dock_state field |

### Files Modified (Phase 3 — Week 10, Agent E)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `src/ui/axis_link.hpp` | E | 10 | Added SharedCursor struct, update_shared_cursor(), shared_cursor_for() |
| `src/ui/axis_link.cpp` | E | 10 | Implemented shared cursor methods |
| `src/ui/crosshair.hpp` | E | 10 | Updated draw_all_axes() signature for AxisLinkManager |
| `src/ui/crosshair.cpp` | E | 10 | Enhanced draw_all_axes() with Y interpolation from series data |
| `src/ui/data_interaction.hpp` | E | 10 | Added AxisLinkManager pointer, setter/getter |
| `src/ui/data_interaction.cpp` | E | 10 | Broadcasts SharedCursor, passes link_mgr to crosshair |

### Files Modified (Phase 3 — Week 11, Agent G)

| File | Agent | Week | Changes |
|------|-------|------|--------|
| `src/ui/timeline_editor.hpp` | G | 11 | Added KeyframeInterpolator integration, evaluate_at_playhead(), serialize |
| `src/ui/timeline_editor.cpp` | G | 11 | Auto-evaluates interpolator on advance(), serialization |
| `src/ui/recording_export.hpp` | G | 11 | Added multi-pane support (PaneRect, begin_multi_pane) |
| `src/ui/recording_export.cpp` | G | 11 | Composite render callback, auto-grid layout |

### Test Files

| Test File | Tests | Status |
|-----------|-------|--------|
| `test_animation_controller.cpp` | 11 | ✅ Pass |
| `test_gesture_recognizer.cpp` | 10 | ✅ Pass |
| `test_easing.cpp` | 23 | ✅ Pass |
| `test_input.cpp` | 15 | ✅ Pass |
| `test_data_interaction.cpp` | 11 | ✅ Pass |
| `test_theme.cpp` | 56 | ✅ Pass |
| `test_command_registry.cpp` | 18 | ✅ Pass |
| `test_command_queue.cpp` | — | ✅ Pass |
| `test_transition_engine.cpp` | 37 | ✅ Pass |
| `test_undo_manager.cpp` | 26 | ✅ Pass |
| `test_layout_manager.cpp` | — | ✅ Pass |
| `test_inspector.cpp` | — | ✅ Pass |
| `test_inspector_stats.cpp` | 28 | ✅ Pass |
| `test_undo_property.cpp` | 30 | ✅ Pass |
| `test_workspace_v2.cpp` | 16 | ✅ Pass |
| `test_phase2_integration.cpp` | 28 | ✅ Pass |
| `test_ui_icons.cpp` | — | ✅ Pass |
| `bench_ui.cpp` | 14 benchmarks | ✅ Pass |
| `bench_phase2.cpp` | 38 benchmarks | ✅ Pass |
| `golden_test_phase2.cpp` | 14 (10 scenes + 4 framework) | ✅ Pass |
| `test_timeline_editor.cpp` | 66 | ✅ Pass |
| `test_recording_export.cpp` | 30 | ✅ Pass |
| `test_theme_colorblind.cpp` | 70 | ✅ Pass |
| `test_plot_style.cpp` | 86 | ✅ Pass |
| `test_split_view.cpp` | 55 | ✅ Pass |
| `test_dock_system.cpp` | 39 | ✅ Pass |
| `test_axis_link.cpp` | 51 | ✅ Pass |
| `test_shared_cursor.cpp` | 20 | ✅ Pass |
| `test_data_transform.cpp` | 64 | ✅ Pass |
| `test_shortcut_config.cpp` | 26 | ✅ Pass |
| `test_plugin_api.cpp` | 31 | ✅ Pass |
| `test_workspace_v3.cpp` | 16 | ✅ Pass |
| `test_keyframe_interpolator.cpp` | 82 | ✅ Pass |
| `test_phase3_integration.cpp` | 36 | ✅ Pass |
| `bench_phase3.cpp` | ~50 benchmarks | ✅ Pass |
| `golden_test_phase3.cpp` | 8 scenes | ✅ Pass |
| `test_figure_manager.cpp` | 48 | ✅ Pass |
| `test_region_select.cpp` | 15 | ✅ Pass |
| `test_legend_interaction.cpp` | 13 | ✅ Pass |
| `test_command_palette_registry.cpp` | 30 | ✅ Pass |
| `test_shortcut_manager.cpp` | 25 | ✅ Pass |
| `test_undo_redo.cpp` | 25 | ✅ Pass |
| `test_workspace.cpp` | 12 | ✅ Pass |
| `test_math3d.cpp` | 60 | ✅ Pass |
| `test_camera_animator.cpp` | 48 | ✅ Pass |
| `test_camera.cpp` | — | ✅ Pass |

---

## 3D Visualization Architecture (Separate Track)

**Status:** Phase 2 — Agent 6 Complete ✅  
**Reference:** See [`plans/3D_ARCHITECTURE_PLAN.md`](3D_ARCHITECTURE_PLAN.md) for full 7-agent plan

### Agent 1 — Core Transform Refactor & Math Utilities ✅ Complete

| Deliverable | Status | Files |
|-------------|--------|-------|
| Header-only math library (vec3, vec4, mat4, quat) | ✅ Done | `include/plotix/math3d.hpp` (~350 LOC) |
| FrameUBO expansion (view, model, camera_pos, near/far, light_dir) | ✅ Done | `src/render/backend.hpp` |
| Depth buffer support (SwapchainContext + OffscreenContext) | ✅ Done | `src/render/vulkan/vk_swapchain.hpp/.cpp` |
| PipelineConfig extension (depth test/write, cull, msaa) | ✅ Done | `src/render/vulkan/vk_pipeline.hpp/.cpp` |
| PipelineType enum expansion (Line3D, Scatter3D, Mesh3D, Surface3D, Grid3D) | ✅ Done | `src/render/backend.hpp` |
| Backend::draw_indexed() for mesh rendering | ✅ Done | `src/render/vulkan/vk_backend.hpp/.cpp` |
| All 6 shaders updated to use MVP (projection * view * model) | ✅ Done | `src/gpu/shaders/*.vert` |
| 2D backward compatibility (view=model=identity) | ✅ Done | `src/render/renderer.cpp` |
| Math3D unit tests (60 tests, 7 suites) | ✅ Done | `tests/unit/test_math3d.cpp` |

**Key achievements:**
- Zero 2D regressions: All existing tests pass (130/130 targets build, 51/51 ctest pass)
- Depth buffer always created, depth testing only enabled for 3D pipelines via PipelineConfig
- Column-major matrices matching GLSL/Vulkan convention
- No GLM dependency — self-contained math library
- FrameUBO std140 compatible with proper padding

### Agent 2 — Camera & 3D Interaction ✅ Complete

**Planned deliverables:**
- Camera class (position, target, up, orbit, pan, zoom, projection modes)
- Arcball orbit controls (quaternion-based)
- Input integration (dynamic_cast<Axes3D*> routing)
- Camera serialization/deserialization
- 40+ camera unit tests

**Depends on:** Agent 1 (math3d.hpp) ✅

### Agent 6 — 3D Animation Extension ✅ Complete

| Deliverable | Status | Files |
|-------------|--------|-------|
| CameraAnimator class (orbit + free-flight keyframe paths) | ✅ Done | `src/ui/camera_animator.hpp/.cpp` |
| CameraKeyframe struct, CameraPathMode enum | ✅ Done | `src/ui/camera_animator.hpp` |
| Orbit interpolation (lerp azimuth/elevation/distance/fov/target) | ✅ Done | `src/ui/camera_animator.cpp` |
| Free-flight interpolation (slerp orientation + lerp position) | ✅ Done | `src/ui/camera_animator.cpp` |
| Target camera binding + evaluate_at() for timeline integration | ✅ Done | `src/ui/camera_animator.hpp/.cpp` |
| Convenience: create_orbit_animation(), create_turntable() | ✅ Done | `src/ui/camera_animator.cpp` |
| CameraAnimator serialization/deserialization | ✅ Done | `src/ui/camera_animator.cpp` |
| TransitionEngine::animate_camera() (lerp all camera params) | ✅ Done | `src/ui/transition_engine.hpp/.cpp` |
| KeyframeInterpolator CameraBinding (azimuth/elevation/distance/fov channels) | ✅ Done | `src/ui/keyframe_interpolator.hpp/.cpp` |
| TimelineEditor camera_animator_ integration + evaluate_at_playhead() | ✅ Done | `src/ui/timeline_editor.hpp/.cpp` |
| CameraAnimator forward declaration | ✅ Done | `include/plotix/fwd.hpp` |
| Unit tests (48 tests, 11 suites) | ✅ Done | `tests/unit/test_camera_animator.cpp` |

**Key achievements:**
- Two interpolation strategies: Orbit (spherical coord lerp) and FreeFlight (quaternion slerp + position lerp)
- Shepperd's method for numerically stable quaternion extraction from rotation matrix
- Thread-safe: all public methods lock internal mutex
- Timeline integration: evaluate_at_playhead() drives both KeyframeInterpolator and CameraAnimator
- Zero regressions: 62/62 ctest pass

**Depends on:** Agent 2 (Camera class) ✅

### Future Agents (Planned)

| Agent | Scope | Status | Depends On |
|-------|-------|--------|------------|
| Agent 3 | Axes3D, grid planes, tick labels | Planned | Agent 1, 2 |
| Agent 4 | 3D pipelines & shaders (10 new shaders) | Planned | Agent 1 |
| Agent 5 | 3D series (ScatterSeries3D, LineSeries3D, SurfaceSeries, MeshSeries) | ✅ Done | Agent 3, 4 |
| Agent 6 | Camera animation, keyframe interpolation, timeline integration | ✅ Done | Agent 2, 5 |
| Agent 7 | Testing, golden images, benchmarks, validation | Planned | All agents |

**3D Phase 1 Target (Weeks 1–4):** 3D scatter plot with orbit camera, depth tested, exportable to PNG  
**3D Phase 2 Target (Weeks 5–8):** Surface plots, 3D lines, 3D axes grid, camera animation  
**3D Phase 3 Target (Weeks 9–12):** Lighting, transparency, MSAA, polish

---

## Known Issues & Technical Debt

| Issue | Severity | Owner | Notes |
|-------|----------|-------|-------|
| ~~`ThemeManager::update()` mutates `current_theme_->colors` in-place during transitions~~ | ✅ Fixed | D | Fixed: transitions now use `display_colors_` — stored themes are never mutated |
| ~~`load_default()`, `export_theme()`, `import_theme()` declared but not implemented~~ | ✅ Fixed | D | Implemented with JSON serialization in `theme.cpp` |
| ~~`shortcut_manager.hpp` has no `.cpp` implementation~~ | ✅ Fixed | F | Implemented in Week 5 with full keybinding system |
| ~~`region_select.hpp` has no `.cpp` implementation~~ | ✅ Fixed | E | Implemented in Week 5 with statistics mini-toolbar |
| ~~`command_registry.cpp` not in `PLOTIX_UI_SOURCES`~~ | ✅ Fixed | F | Added to ImGui sources block, works correctly |

---

## Decision Gates

| Gate | Date | Criteria | Status |
|------|------|----------|--------|
| End Week 2 | — | Layout zones stable? | ✅ Passed |
| End Week 4 | — | Animation system working? | ✅ Passed |
| **End Week 8** | — | All Phase 2 features passing tests? | ✅ Passed |
| End Week 11 | — | Core features complete? | ✅ Passed |
| **End Week 12** | — | Full test suite, benchmarks, golden tests? | ✅ Passed |

---

## Agent Update Protocol

**Every agent MUST update this file at the end of their work session.**

### Steps:

1. **Update the relevant week's table** — Change status from `⏳ Not Started` → `🔄 In Progress` → `✅ Done` for each deliverable you worked on.

2. **Update the File Inventory** — If you created new files, add them to the "Files Created" table with your agent letter, the week number, and whether it's in the build.

3. **Update the Test Files table** — If you added or modified tests, update the test count and status.

4. **Log any new issues** — Add to the "Known Issues & Technical Debt" table if you discovered bugs, left TODOs, or created workarounds.

5. **Update Phase progress percentage** — Estimate the overall phase completion percentage in the Phase Overview table.

6. **Update exit criteria checkboxes** — Check off `[x]` any exit criteria that are now met.

7. **Update the "Last Updated" date** at the top of this file.

### Format for status:
- `⏳ Not Started` — Work has not begun
- `🔄 In Progress` — Actively being worked on
- `✅ Done` — Complete and tested
- `⚠️ Blocked` — Cannot proceed (add note in Issues table)
- `🔴 Reverted` — Was done but had to be rolled back

### Example update (Agent F, Week 5):
```markdown
| Command palette UI (Ctrl+K) | F | ✅ Done | `src/ui/command_palette.hpp/.cpp` |
```

Then add to File Inventory:
```markdown
| `src/ui/command_palette.hpp` / `.cpp` | F | 5 | ✅ Yes |
```
