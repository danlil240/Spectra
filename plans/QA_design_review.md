# Spectra Visual QA Design Review

> **Last Updated:** 2026-06-05
> **Test Health:** golden_image_tests 5/5 PASS · unit_test_accessibility 17/17 PASS
> **Design Health:** ✅ Good — No P0/P1 defects open. Live design-review capture: **63/63** screenshots (seed 42).

---

## Open Issues

| ID  | Priority | Status | Description | File | Line |
|-----|----------|--------|-------------|------|------|
| A11Y-SP-4 | P2 | Open (mitigated) | Keyboard nav burden in Shortcuts table: no arrow-key row navigation; reaching bottom-row buttons still requires O(n) Tab presses. Filter tooltip added 2026-06-04; arrow-key table nav deferred (ImGui limitation). Re-verified 2026-06-05 (`60_settings_shortcuts_night.png`). | `src/ui/settings/settings_panel.cpp` | `draw_shortcuts_tab()` |

---

## Resolved Issues

| ID  | Priority | Resolution | Description | File | Fix |
|-----|----------|------------|-------------|------|-----|
| A11Y-SP-3 | P2 | By Design 2026-06-04 | High-contrast `error` #FF0000 stays at 5.41:1 on `bg_secondary` (AAA 7:1 unreachable for pure red on dark). Conflict state uses `[!]` prefix (WCAG 1.4.1) since A11Y-SP-1 — color is not the sole indicator. | `src/ui/settings/settings_panel.cpp` | `draw_shortcuts_tab()` conflict branch |
| A11Y-SP-4 | P2 | Mitigated 2026-06-04 | Filter discoverability: hover tooltip documents filter + Tab row navigation. | `src/ui/settings/settings_panel.cpp` | `draw_shortcuts_tab()` filter `InputTextWithHint` |
| A11Y-SP-5 | P2 | Fixed 2026-06-04 | Light `warning` #9A6700 → **#7A5000** (6.29:1 on `bg_secondary`). | `src/ui/theme/theme.cpp` | light `warning` token |
| D-3 | P3 | Fixed 2026-06-04 | Dark `tick_label` #A0A8B0 → **#A0A0A0** (neutral gray, no blue tint). | `src/ui/theme/theme.cpp` | dark theme `tick_label` |
| D-4 | P3 | Fixed 2026-06-04 | Settings window used fixed 640×480 first-use size leaving ~180px dead space. Now `AlwaysAutoResize` + width constraints (520–640px). | `src/ui/settings/settings_panel.cpp` | `draw()` |
| D-5 | P3 | Fixed 2026-06-04 | Theme/Palette labels use `CalcTextSize` + `AlignTextToFramePadding` instead of `SameLine(170)`. | `src/ui/settings/settings_panel.cpp` | `draw_appearance_tab()` |
| D-7 | P3 | Fixed 2026-06-04 | Removed unused `active_tab_` member; ImGui owns tab state via `pending_tab_` only. | `src/ui/settings/settings_panel.hpp` | — |
| A11Y-SP-1 | P1 | Fixed 2026-05-17 | Conflict shortcut text used hardcoded `ImVec4(1,0.4,0.4,1)` = #FF6666. Contrast on **light** bg_secondary (#DCE5F0): **2.25:1** (required 4.5:1). Contrast on **dark** bg_secondary (#2E2E2E): **4.12:1** (required 4.5:1). | `src/ui/settings/settings_panel.cpp` + `src/ui/theme/theme.cpp` | Used `theme().error` token; raised dark `error` #F85149→**#FF7575** (4.52:1 on dark bg_secondary); darkened light `error` #CF222E→**#B91C1C** (5.09:1 on light bg_secondary). Also prepends `[!]` prefix (WCAG 1.4.1). |
| A11Y-SP-2 | P2 | Fixed 2026-05-17 | Capture indicator "Press key..." used hardcoded amber — not from theme tokens (D-6). Added `(Esc = cancel)` suffix. | `src/ui/settings/settings_panel.cpp` | Replaced with `theme().warning` color. |
| A11Y-SP-6 | P2 | Fixed 2026-05-17 | "X" SmallButton for removing shortcut overrides had no label or tooltip — action was not discoverable for keyboard-only users. | `src/ui/settings/settings_panel.cpp` | Added `SetTooltip("Remove custom shortcut override")` on hover. |
| A11Y-SP-7 | P2 | Fixed 2026-05-17 | Palette dropdown listed internal codenames only ("default", "colorblind"…); CVD status was not communicated, making it inaccessible to users with color vision deficiency. | `src/ui/settings/settings_panel.cpp` | Dropdown items now query `theme_mgr_->get_data_palette(key).colorblind_safe` and append `"(CVD-safe)"` or `"(not CVD-safe)"` to each label. |
| SP-1 | P0 | Fixed 2026-05-17 | Settings panel never appeared after `execute_command("panel.open_settings")` — automation MCP call returned `ok` but panel was invisible. Root cause: `settings_panel.open()` sets `visible_=true` but `RedrawTracker` stayed idle, skipping all render ticks. | `src/ui/automation/handlers/handlers_command.cpp` | Added `sess->redraw_tracker().mark_dirty("execute_command")` after every successful `cmd_registry.execute()` call. |
| SP-2 | P1 | Fixed 2026-05-17 | Welcome screen content (logo, instructions text) bled through Settings window background at `opacity_panel=0.95f` alpha. | `src/ui/settings/settings_panel.cpp` | Push `ImGuiCol_WindowBg` at `0.98f` alpha before `Begin()`, matching the CSV dialog pattern in `imgui_dialogs.cpp`. |
| SP-3 | P1 | Fixed 2026-05-17 | Tab bar near-invisible: inactive tabs were indistinguishable from window background (`tab_idle = bg_secondary.lerp(bg_tertiary, 0.25f)` and `ImGuiCol_Tab` at `0.70f` alpha). | `src/ui/theme/theme.cpp` | Raised `tab_idle` lerp to `0.50`, `tab_hover` to `0.22`, `tab_selected` to `0.36`; raised `ImGuiCol_Tab` alpha to `0.90f`. |
| SP-4 | P2 | Fixed 2026-05-17 | Settings window appeared at `(0,0)` (top-left corner) on first open — centering via `vp->GetCenter()` did not work on first `ImGuiCond_Appearing`. | `src/ui/settings/settings_panel.cpp` | Changed `SetNextWindowPos` pivot to use `io.DisplaySize * 0.5f`, matching the CSV dialog pattern. Panel now renders centered at 1280×720. |
| A11Y-001 | P1 | Fixed 2026-05-16 | Dark theme `text_secondary` (#909090) measured 4.25:1 on `bg_secondary` (#2E2E2E) and 3.79:1 on `bg_tertiary` (#363636) — both below WCAG AA 4.5:1 threshold. Affected inspector labels and placeholder text in inputs. | `src/ui/theme/theme.cpp` | `text_secondary` bumped from `0x909090` to `0xA0A0A0`; now 5.19:1 / 4.62:1 / 5.71:1 / 6.16:1 across all dark surfaces. |
| D-1 | P3 | Fixed 2026-07-08 | Inspector toggle chevron glyph rendered at fractional pixel coordinates — blurry at 125%/150% DPI. Fixed by applying `std::floor()` to both `ix` and `iy` before `AddText`. | `src/ui/imgui/imgui_panels.cpp` | `draw_inspector_toggle()` — `ix`/`iy` now floored |
| D-2 | P3 | Fixed 2026-07-08 | Inspector tab separator horizontal hairline rendered with un-snapped Y coordinate from `GetCursorScreenPos().y`. Fixed by wrapping in `std::floor()`. | `src/ui/imgui/imgui_panels.cpp` | `draw_inspector()` — `sep_y` now `std::floor(GetCursorScreenPos().y)` |
| D-8 | P3 | Fixed 2026-06-05 | Status bar FPS/GPU chips overlapped zoom label when the bar was narrower than ~160px + left content (e.g. `56_tiny_window_all_panels_open` at 320×240). | `src/ui/imgui/imgui_panels.cpp` | `draw_status_bar()` — omit perf block unless `perf_anchor > cursor + gap` |

---

## Static Analysis Coverage (2026-07-08)

Files reviewed for sub-pixel snapping, border rendering, and theme color correctness:

| File | Area Checked | Result |
|------|--------------|--------|
| `src/ui/imgui/imgui_panels.cpp` | `draw_tab_bar()` hairline bottom border | ✅ `std::floor(wpos.y + wsz.y) - 1.0f` correctly applied |
| `src/ui/imgui/imgui_panels.cpp` | `draw_inspector()` left vertical hairline | ✅ `bounds.x` is integer-origin from LayoutManager |
| `src/ui/imgui/imgui_panels.cpp` | `draw_inspector()` tab separator Y | ✅ Fixed (D-2) |
| `src/ui/imgui/imgui_panels.cpp` | `draw_inspector_toggle()` chevron `AddText` | ✅ Fixed (D-1) |
| `src/ui/imgui/imgui_command_bar.cpp` | Nav rail icon `AddText` X (`ix`) | ✅ `std::floor()` applied |
| `src/ui/imgui/imgui_command_bar.cpp` | Nav rail icon `AddText` Y (`y_start`) | ✅ `std::floor()` applied |
| `src/ui/imgui/imgui_command_bar.cpp` | Nav rail label `AddText` (`lx`, `ly`) | ✅ Both floored |
| `src/ui/imgui/imgui_command_bar.cpp` | Status bar bottom hairline | ✅ `std::floor(bottom) - 1.0f` applied |
| `src/ui/imgui/imgui_command_bar.cpp` | Nav rail separator | ✅ `std::floor(GetCursorScreenPos().y)` applied |
| `src/ui/theme/design_tokens.hpp` | `ELEVATION_*` tokens defined | ✅ 0/4/8/16/24px for ELEVATION_0–4 |
| `src/ui/theme/theme.cpp` | Night/Dark/Light theme initialization | ✅ All three themes defined; `apply_to_imgui()` complete |
| `src/ui/theme/theme.cpp` | `apply_to_renderer()` | ⚠️ Architectural no-op (body is `(void)renderer;`). Renderer reads theme colors directly via `ThemeManager::instance()` — no visual defect, but noted as tech debt |
| `src/ui/overlay/crosshair.cpp` | X-label position `vy1 + 2.0f` | ✅ By design — label renders below viewport into the axis tick area |
| `tests/golden/baseline/` | 52 golden baseline images | ✅ All present, all 59 golden tests passing |

---

## Design Health Summary

### Passing
- **Token system**: Complete. All `SPACE_*`, `RADIUS_*`, `FONT_*`, `ELEVATION_*`, animation durations, glow tokens defined.
- **Nav rail**: Fully pixel-snapped (icon X and Y, label X and Y, separator line).
- **Tab bar**: Hairline bottom border correctly snapped.
- **Theme engine**: Night/Dark/Light all complete; `apply_to_imgui()` fully token-driven.
- **Test suite**: 140 unit tests + 59 golden tests all pass.

### Captured 2026-06-05 (seed 42, `spectra_qa_agent --design-review`)
- **63/63** screenshots in `/tmp/spectra_qa_design_20260605/design/`; post-fix recapture in `/tmp/spectra_qa_design_after_20260605/design/` (D-8)
- `manifest.txt` complete; settings shots `57`–`62` present; shortcuts tab readable in night/light themes

### Not Yet Reviewed at Pixel Level
- Crosshair label clipping at viewport edges
- Command palette badge/background styling polish
- Split view divider alignment under extreme aspect ratios

---

## Methodology

This review was conducted as a static code analysis sweep using the issue-to-file map in
`skills/qa-designer-agent/SKILL.md`. The following files were inspected:

- `src/ui/imgui/imgui_panels.cpp` (all draw functions)
- `src/ui/imgui/imgui_command_bar.cpp` (nav rail, status bar, hairlines)
- `src/ui/theme/design_tokens.hpp` (token completeness)
- `src/ui/theme/theme.cpp` (theme initialization, `apply_to_imgui`, `apply_to_renderer`)
- `src/ui/overlay/crosshair.cpp` (label positioning)
- `tests/golden/baseline/` (baseline completeness)

Live screenshot capture performed 2026-06-05 (`SPECTRA_BUILD_QA_AGENT=ON`, DISPLAY=:1).
Command:

```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_20260605
```
