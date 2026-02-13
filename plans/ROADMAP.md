# Plotix UI Redesign — Roadmap & Progress Tracker

**Last Updated:** 2026-02-14 (Week 6 — Agent C complete)  
**Current Phase:** Phase 2 — Power User Features  
**Overall Progress:** Phase 1 complete, Phase 2 in progress

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
| **Phase 2** — Power User Features | 5–8 | 🔄 In Progress | 15% |
| **Phase 3** — Elite Differentiators | 9–12 | ⏳ Not Started | 0% |

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

**Phase 2 Test Summary (so far):** InspectorStats (28): Percentile (8), DataExtraction (2), FullStats (5), AxesAggregate (4), Sparkline (2), SectionAnimation (4), EdgeCases (3).

---

## Phase 2 — Power User Features (Weeks 5–8) 🔄

### Week 5 — Agent F (Command Palette) + Agent E (Crosshair/Markers)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Command palette UI (Ctrl+K) | F | ⏳ Not Started | `src/ui/command_palette.hpp/.cpp` (NEW) |
| 30+ registered commands | F | ⏳ Not Started | `src/ui/command_registry.cpp` (extend) |
| Configurable shortcuts | F | ⏳ Not Started | `src/ui/shortcut_manager.hpp` (extend, add .cpp) |
| Crosshair shared across subplots | E | ⏳ Not Started | `src/ui/crosshair.cpp` (extend) |
| Legend click-to-toggle | E | ⏳ Not Started | `src/ui/data_interaction.cpp` (extend) |
| Legend drag-to-reposition | E | ⏳ Not Started | `src/ui/data_interaction.cpp` (extend) |

**Pre-existing foundations for Week 5:**
- `CommandRegistry` class exists with register/search/execute API
- `ShortcutManager` header exists with Shortcut struct, KeyMod flags, parse/to_string
- `CommandQueue` (lock-free SPSC) exists
- Crosshair and DataInteraction already functional from Week 3

### Week 6 — Agent A (Multi-figure tabs) + Agent C (Statistics) ← **CURRENT**

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Multi-figure tab switching | A | ⏳ Not Started | `src/ui/app.cpp`, `src/ui/tab_bar.cpp` |
| Series statistics display (min, max, mean, median, std, percentiles, count) | C | ✅ Done | `src/ui/inspector.hpp/.cpp` |
| Collapsible inspector sections with smooth animation | C | ✅ Done | `src/ui/widgets.hpp/.cpp`, `src/ui/inspector.cpp` |
| Sparkline data preview in inspector | C | ✅ Done | `src/ui/inspector.cpp`, `src/ui/widgets.hpp/.cpp` |
| Enhanced widget library (sparkline, badge, stat_row, separator_label, int_drag, progress_bar) | C | ✅ Done | `src/ui/widgets.hpp/.cpp` |
| Axes aggregate statistics section | C | ✅ Done | `src/ui/inspector.hpp/.cpp` |
| X-axis statistics (min, max, range, mean) | C | ✅ Done | `src/ui/inspector.cpp` |

### Week 7 — Agent F (Undo/Redo) + Agent B (Box Zoom)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Undo/redo system | F | ⏳ Not Started | `src/ui/undo_manager.hpp/.cpp` (NEW) |
| Workspace save/load | F | ⏳ Not Started | `src/ui/workspace.hpp/.cpp` (NEW) |
| Box zoom with animated overlay | B | ⏳ Not Started | `src/ui/input.cpp` |
| Double-click auto-fit polish | B | ⏳ Not Started | `src/ui/input.cpp` |

### Week 8 — Agent D (Colorblind) + Agent E (Region Select) + Agent H (Tests)

| Deliverable | Agent | Status | Files |
|-------------|-------|--------|-------|
| Colorblind-safe palette | D | ⏳ Not Started | `src/ui/theme.cpp` |
| Region selection + mini-toolbar | E | ⏳ Not Started | `src/ui/region_select.hpp` (extend, add .cpp) |
| Golden image tests | H | ⏳ Not Started | `tests/golden/` |
| Phase 2 benchmarks | H | ⏳ Not Started | `tests/bench/` |

**Phase 2 Exit Criteria:**
- [ ] Command palette opens <16ms, fuzzy search instant
- [ ] Undo/redo works for all property changes
- [ ] Multi-figure tabs functional (1–20 figures)
- [ ] Crosshair shared across subplots
- [ ] Data markers persist through zoom/pan
- [ ] Workspace save/load restores full state
- [ ] Nearest-point <0.1ms for 100K points
- [ ] Undo <1ms

---

## Phase 3 — Elite Differentiators (Weeks 9–12) ⏳

### Week 9 — Agent A (Docking)

| Deliverable | Agent | Status |
|-------------|-------|--------|
| Docking system | A | ⏳ Not Started |
| Split view (horizontal/vertical) | A | ⏳ Not Started |

### Week 10 — Agent E (Linked Axes) + Agent B (Multi-axis)

| Deliverable | Agent | Status |
|-------------|-------|--------|
| Shared cursor across subplots | E | ⏳ Not Started |
| Multi-axis linking | B | ⏳ Not Started |
| Data transforms panel | E | ⏳ Not Started |

### Week 11 — Agent G (Timeline) + Agent F (Export)

| Deliverable | Agent | Status |
|-------------|-------|--------|
| Timeline editor | G | ⏳ Not Started |
| Recording export (MP4/GIF) | G | ⏳ Not Started |
| Theme export/import (JSON) | F | ⏳ Not Started |

### Week 12 — Agent H (Final Polish)

| Deliverable | Agent | Status |
|-------------|-------|--------|
| Full test suite (>80% coverage) | H | ⏳ Not Started |
| Performance optimization pass | H | ⏳ Not Started |
| Documentation | H | ⏳ Not Started |

**Phase 3 Exit Criteria:**
- [ ] Docking layout <0.5ms
- [ ] 100K points interactive at 60fps
- [ ] Linked axes sync correctly
- [ ] Timeline editor functional
- [ ] Plugin-ready architecture

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

### Files Not Yet Created (Phase 2–3)

| File | Agent | Planned Week |
|------|-------|-------------|
| `src/ui/command_palette.hpp` / `.cpp` | F | 5 |
| `src/ui/undo_manager.hpp` / `.cpp` | F | 7 |
| `src/ui/workspace.hpp` / `.cpp` | F | 7 |
| `src/ui/region_select.cpp` | E | 8 |
| `src/ui/shortcut_manager.cpp` | F | 5 |

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
| `test_ui_icons.cpp` | — | ✅ Pass |
| `bench_ui.cpp` | 14 benchmarks | ✅ Pass |

---

## Known Issues & Technical Debt

| Issue | Severity | Owner | Notes |
|-------|----------|-------|-------|
| `ThemeManager::update()` mutates `current_theme_->colors` in-place during transitions | 🟡 Medium | D | Corrupts stored theme data; test workaround in place |
| `load_default()`, `export_theme()`, `import_theme()` declared but not implemented | 🟡 Medium | D | Stubs in `theme.hpp`, removed from `theme.cpp` |
| `shortcut_manager.hpp` has no `.cpp` implementation | 🟡 Medium | F | Header exists with types; needs implementation in Week 5 |
| `region_select.hpp` has no `.cpp` implementation | 🟢 Low | E | Header with structs; `.cpp` planned for Week 8 |
| `command_registry.cpp` not in `PLOTIX_UI_SOURCES` (only in ImGui block) | 🟢 Low | — | Works when ImGui is enabled |

---

## Decision Gates

| Gate | Date | Criteria | Status |
|------|------|----------|--------|
| End Week 2 | — | Layout zones stable? | ✅ Passed |
| End Week 4 | — | Animation system working? | ✅ Passed |
| **End Week 8** | — | All Phase 2 features passing tests? | ⏳ Pending |
| End Week 11 | — | Core features complete? | ⏳ Pending |

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
