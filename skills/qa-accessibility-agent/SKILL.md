---
name: qa-accessibility-agent
description: Audit Spectra for colorblind-safe palettes, WCAG AA contrast ratios, keyboard navigation completeness, and high-contrast theme correctness. Use when adding new UI elements, reviewing theme colors, verifying colorblind palette support, or checking that all interactive controls are reachable via keyboard.
---

# QA Accessibility Agent

Ensure Spectra meets accessibility requirements: WCAG AA contrast ratios, colorblind-safe default palettes, full keyboard navigation, and a working high-contrast theme. This agent owns the inclusivity layer of QA.

---

## Required Context

Before starting any task, read:
- `plans/QA_design_review.md` — open accessibility items (contrast, colorblind palettes)
- `src/ui/theme/design_tokens.hpp` — spacing, font sizes, opacity constants
- `src/ui/theme/theme.cpp` — `initialize_default_themes()` — all color values

---

## Workflow

### 1. Build and capture screenshots

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)

./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_a11y_$(date +%Y%m%d)
```

Key screenshots to analyze for accessibility:
- `11_dark_theme.png` — dark theme contrast
- `12_light_theme.png` — light theme contrast
- `07_inspector_panel_open.png` — text on panels
- `08_command_palette_open.png` — command palette text legibility
- `34_multi_series_full_chrome.png` — series colors in legend/plot (colorblind check)
- `19_3d_surface.png` — colormap accessibility (Viridis is colorblind-safe; Jet is not)

### 2. Run colorblind theme unit tests

```bash
ctest --test-dir build --output-on-failure -R "test_theme_colorblind|test_theme"
```

Tests in `tests/unit/test_theme_colorblind.cpp` verify:
- Colorblind-safe palette is registered
- All 8 categorical colors pass deuteranopia/protanopia simulation distance threshold
- High-contrast theme has correct foreground/background ratios

### 3. Audit contrast ratios

For each UI element in the screenshot set, calculate WCAG AA contrast ratio:
- **Normal text:** minimum 4.5:1
- **Large text (≥18px or ≥14px bold):** minimum 3:1
- **UI components and state indicators:** minimum 3:1

Key element pairs to check:
| Element | Foreground | Background | Required ratio |
|---|---|---|---|
| Plot title | `theme.text_primary` | plot background | 4.5:1 |
| Axis labels | `theme.text_secondary` | plot background | 4.5:1 |
| Status bar text | `theme.text_secondary` | status bar background | 4.5:1 |
| Inspector labels | `theme.text_primary` | panel background | 4.5:1 |
| Tab bar active | `theme.accent` | tab background | 3:1 |
| Button icons | icon color | button background | 3:1 |
| Grid lines | `theme.grid_color` | plot background | — (not text, aim for 1.5:1 minimum for visibility) |

Use the WCAG contrast formula: `(L1 + 0.05) / (L2 + 0.05)` where L = relative luminance.

### 4. Check colorblind-safe series palettes

Default categorical colors must be distinguishable under:
- **Deuteranopia** (red-green, most common — 6% of males)
- **Protanopia** (red, 1% of males)
- **Tritanopia** (blue-yellow, rare)

Test: `tests/unit/test_theme_colorblind.cpp`

For 3D surface plots — advise Viridis (default) over Jet:
- Viridis: perceptually uniform, colorblind-safe ✅
- Plasma/Inferno/Magma: perceptually uniform, colorblind-safe ✅
- Coolwarm: acceptable (symmetric, distinguishable) ✅
- Jet: NOT colorblind-safe ⚠️ — consider deprecating as default

### 5. Audit keyboard navigation

Every interactive control must be reachable via keyboard. Check:
- **Tab order:** Tab/Shift+Tab cycles through all focusable controls
- **Menu bar:** Alt key activates menu bar
- **Command palette:** Ctrl+P opens it; arrow keys navigate; Enter confirms
- **Inspector:** Tab navigates between fields
- **Timeline:** Space = play/pause; `[` = step back; `]` = step forward
- **Series selection:** keyboard shortcut for `series.cycle_selection`
- **Clipboard:** Ctrl+C, Ctrl+X, Ctrl+V work for series
- **Zoom/pan:** scroll wheel, +/- keys, Home key for reset

Verify shortcuts in `src/ui/commands/shortcut_manager.cpp` and `src/ui/app/register_commands.cpp`.

### 6. Check high-contrast theme

The high-contrast theme (`ui::ThemeId::HighContrast`) must:
- Have all text at ≥7:1 contrast ratio (AAA for normal text)
- Use only black, white, and one accent color
- Not rely on color alone to convey state (use shape/icon + color)

Test by capturing design review with high-contrast theme:
```bash
# Modify QA agent to use high-contrast theme, or test via theme toggle command
./build/tests/spectra_qa_agent --scenario mode_switching --no-fuzz \
    --output-dir /tmp/spectra_qa_hc
```

### 7. Apply fixes

Common accessibility fix patterns:

| Issue | Fix |
|---|---|
| Low contrast text | Increase lightness of `text_secondary` or darken background |
| Colorblind-unsafe palette | Replace similar-hue colors with perceptually distinct alternatives |
| Missing keyboard shortcut | Add to `register_standard_commands()` with appropriate key |
| Focus indicator not visible | Add `ImGui::SetItemDefaultFocus()` or highlight active control |
| Color-only state indicator | Add icon or shape alongside color |

### 8. Validate and document

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -R "test_theme"

./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_a11y_after
```

Update `plans/QA_design_review.md` with accessibility findings (use `A11Y-` prefix for IDs).

---

## Accessibility Standards Reference

| Standard | Requirement |
|---|---|
| WCAG 2.1 AA | Normal text ≥ 4.5:1 contrast |
| WCAG 2.1 AA | Large text ≥ 3:1 contrast |
| WCAG 2.1 AA | UI components ≥ 3:1 contrast |
| WCAG 2.1 AAA | Normal text ≥ 7:1 (high-contrast theme) |
| Colorblind | All state conveyed by shape/icon + color, not color alone |
| Keyboard | All functionality reachable without mouse |

---

## Issue-to-File Map

| Issue type | Primary file |
|---|---|
| Theme color values | `src/ui/theme/theme.cpp` — `initialize_default_themes()` |
| Design tokens (spacing, font sizes) | `src/ui/theme/design_tokens.hpp` |
| Colorblind palette | `src/ui/theme/theme.cpp` — colorblind-safe categorical colors |
| Keyboard shortcuts | `src/ui/app/register_commands.cpp` |
| Shortcut manager config | `src/ui/commands/shortcut_manager.cpp` |
| Tab order / focus | `src/ui/imgui/imgui_integration.cpp` — panel draw functions |
| High-contrast theme | `src/ui/theme/theme.cpp` — `ThemeId::HighContrast` |
| Colorblind unit tests | `tests/unit/test_theme_colorblind.cpp` |
| 3D colormap accessibility | `src/gpu/shaders/surface3d.frag` — colormap order in UI |

---

## Registered Commands (accessibility-relevant)

| Command ID | Keyboard | Accessibility note |
|---|---|---|
| `series.cycle_selection` | — | Needs keyboard shortcut (Tab or F6) |
| `series.copy` | Ctrl+C | ✅ |
| `series.cut` | Ctrl+X | ✅ |
| `series.paste` | Ctrl+V | ✅ |
| `series.delete` | Delete | ✅ |
| `view.toggle_grid` | G | ✅ |
| `view.toggle_legend` | L | ✅ |
| `view.home` | H | ✅ |
| `anim.toggle_play` | Space | ✅ |
| `edit.undo` | Ctrl+Z | ✅ |
| `edit.redo` | Ctrl+Shift+Z | ✅ |

---

## Performance Targets

| Metric | Target |
|---|---|
| `test_theme_colorblind` | 100% pass |
| Normal text contrast (dark + light themes) | ≥ 4.5:1 |
| UI component contrast | ≥ 3:1 |
| High-contrast theme text | ≥ 7:1 |
| Series palette: deuteranopia distance | > 20 CIELAB units between any two colors |
| Keyboard coverage | 100% of primary actions reachable without mouse |

---

## Self-Update Protocol

The agent **may** update this file when it is 100% certain a change is correct. Never speculate — only update after contrast measurements and `test_theme_colorblind` both confirm correctness.

### Permitted self-updates

| Section | When to update | What to write |
|---|---|---|
| **Keyboard navigation audit table** | A shortcut is added or confirmed missing | Update the `Accessibility note` column |
| **Issue-to-File Map** | A new file is identified during a session | Add the file + issue type row |
| **Registered Commands table** | A new command gets a keyboard shortcut | Update the `Keyboard` column and `Accessibility note` |
| **Audit contrast table** | A new UI element pair is checked | Add the row with foreground, background, required ratio |
| **Accessibility Standards table** | A new standard is confirmed applicable | Add the row |

### Forbidden self-updates

- Never change **Workflow steps** without human approval.
- Never change **Guardrails** without human approval.
- Never mark a contrast check ✅ without computing the actual ratio.
- Never remove an existing entry — mark it `~~deprecated~~` with a date instead.
- Never update if `test_theme_colorblind` failed.

### Self-update procedure

1. Run `test_theme_colorblind` and design-review capture, confirm all pass.
2. Identify what is stale or missing in this file.
3. Apply only the update types listed in Permitted above.
4. Append a one-line entry to `REPORT.md` under `## Self-Update Log`: date, section changed, reason.

---

## Live Report

The agent writes to `skills/qa-accessibility-agent/REPORT.md` at the end of every session.

### Report update procedure

After every run, open `REPORT.md` and:
1. Add a new `## Session YYYY-MM-DD HH:MM` block at the top (newest first).
2. Fill in: colorblind test results, contrast ratios measured (element + ratio + pass/fail), keyboard gaps found, fixes applied (file + change), goldens updated, self-updates made.
3. Update the `## Current Status` block at the very top.
4. Never delete old session blocks — they are the accessibility audit trail.

---

## Guardrails

- Never change a theme color without computing the new contrast ratio first.
- Do not add color-only state indicators — always pair with an icon or shape.
- Jet colormap is not colorblind-safe — do not set it as the default for new surface plots.
- Accessibility fixes are non-negotiable for P0 issues — do not defer them.
- Run `test_theme_colorblind` after every palette change.
- Route rendering/visual bugs to `qa-designer-agent`; route crash/perf bugs to `qa-performance-agent`.
- Self-updates to this file require `test_theme_colorblind` passing — never update speculatively.
