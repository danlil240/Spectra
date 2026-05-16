# Spectra Visual QA Design Review

> **Last Updated:** 2026-05-16
> **Test Health:** 140/140 unit tests PASS · 59/59 golden image tests PASS · 103/103 accessibility tests PASS
> **Design Health:** ✅ Good — No P0/P1 defects found. WCAG AA contrast failure fixed (A11Y-001).

---

## Open Issues

| ID  | Priority | Status | Description | File | Line |
|-----|----------|--------|-------------|------|------|
| D-3 | P3 | Open | Dark theme tick label has a blue tint (hex `0xA0A8B0` = RGB 160/168/176). The dark theme design intent is "neutral gray, no blue tints," but the 10-unit blue step on each channel is still visible on axis tick text. | `src/ui/theme/theme.cpp` | ~1015 |

---

## Resolved Issues

| ID  | Priority | Resolution | Description | File | Fix |
|-----|----------|------------|-------------|------|-----|
| A11Y-001 | P1 | Fixed 2026-05-16 | Dark theme `text_secondary` (#909090) measured 4.25:1 on `bg_secondary` (#2E2E2E) and 3.79:1 on `bg_tertiary` (#363636) — both below WCAG AA 4.5:1 threshold. Affected inspector labels and placeholder text in inputs. | `src/ui/theme/theme.cpp` | `text_secondary` bumped from `0x909090` to `0xA0A0A0`; now 5.19:1 / 4.62:1 / 5.71:1 / 6.16:1 across all dark surfaces. |
| D-1 | P3 | Fixed 2026-07-08 | Inspector toggle chevron glyph rendered at fractional pixel coordinates — blurry at 125%/150% DPI. Fixed by applying `std::floor()` to both `ix` and `iy` before `AddText`. | `src/ui/imgui/imgui_panels.cpp` | `draw_inspector_toggle()` — `ix`/`iy` now floored |
| D-2 | P3 | Fixed 2026-07-08 | Inspector tab separator horizontal hairline rendered with un-snapped Y coordinate from `GetCursorScreenPos().y`. Fixed by wrapping in `std::floor()`. | `src/ui/imgui/imgui_panels.cpp` | `draw_inspector()` — `sep_y` now `std::floor(GetCursorScreenPos().y)` |

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

### Not Yet Captured (requires display + spectra_qa_agent run)
- Live screenshot baseline for all 57 coverage points in `skills/qa-designer-agent/SKILL.md`
- Runtime verification of dark-theme tick label tint (D-3) at pixel level
- Theme switching hot-path visual correctness
- Crosshair label clipping at viewport edges (runtime only)
- Animation timeline editor visual review
- Command palette badge/background styling
- Split view divider alignment

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

A live screenshot capture (requiring `SPECTRA_BUILD_QA_AGENT=ON` and a display) was not
performed this session. The next review pass should run:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
./build/tests/spectra_qa_agent --seed 42 --design-review
```
