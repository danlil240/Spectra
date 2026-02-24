# QA Designer Reference

## Command Cookbook

Build QA agent:

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

Run required baseline design-review capture:

```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design
```

Run deterministic stress pass:

```bash
./build/tests/spectra_qa_agent --seed 42 --duration 120 --output-dir /tmp/spectra_qa
```

Run scenarios only:

```bash
./build/tests/spectra_qa_agent --seed 42 --no-fuzz --output-dir /tmp/spectra_qa
```

Run fuzzing only:

```bash
./build/tests/spectra_qa_agent --seed 42 --no-scenarios --fuzz-frames 5000 --output-dir /tmp/spectra_qa
```

Run one target scenario:

```bash
./build/tests/spectra_qa_agent --scenario massive_datasets --no-fuzz --output-dir /tmp/spectra_qa
```

List scenarios:

```bash
./build/tests/spectra_qa_agent --list-scenarios
```

Rebuild and run tests:

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Refresh goldens after intentional visual updates:

```bash
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | No `ERROR` or `CRITICAL` issues |
| `1` | At least one `ERROR` or `CRITICAL` issue |
| `2` | Crash (seed printed to stderr) |

## Design-Review Coverage

`--design-review` captures 50 named screenshots:

- Core UI states (1-20): basic plots, dense data, inspector/command palette, split views, theme toggles, grid/legend/crosshair, tabs, timeline, and basic 3D states.
- 3D/animation/statistics states (21-35): camera variants, 3D line/scatter variants, inspector statistics panels, timeline playback/loop states, curve editor, split-view correctness, and center-zoom verification.
- Menu/window/interaction states (36-50): menu bar activation, command palette with search text, inspector with knobs, nav rail, tab context menu, window resized to 4 aspect ratios (640×480, 1920×600, 600×1080, 320×240), multi-window with detached figure (primary + secondary), window repositioned, split view with inspector + timeline, two windows side by side, fullscreen mode, and minimal chrome.

Use `<output-dir>/design/` and `manifest.txt` for completeness checks before triage.

## Report Interpretation

- Treat frame spikes over 50 ms as likely real stalls.
- Treat VSync-related spikes near 16 ms as possible false positives when EMA starts low.
- Investigate RSS growth over 100 MB above baseline unless expected by loaded data volume.

## Issue-To-File Map

| Issue type | Primary file | Typical touchpoint |
|---|---|---|
| Theme colors (grid, axis, text) | `src/ui/theme/theme.cpp` | `initialize_default_themes()` color values |
| Axes border rendering | `src/render/renderer.cpp` | `render_axis_border()` |
| Grid line rendering | `src/render/renderer.cpp` | `render_grid()` color source |
| Legend background/border | `src/ui/overlay/legend_interaction.cpp` | Legend style colors, border, rounding |
| Crosshair labels | `src/ui/overlay/crosshair.cpp` | Label position and viewport clamping |
| Status bar styling | `src/ui/imgui/imgui_integration.cpp` | `draw_status_bar()` |
| Tab bar styling | `src/ui/figures/tab_bar.cpp` | `draw_tabs()` active/hover visuals |
| Nav rail icons | `src/ui/imgui/imgui_integration.cpp` | `draw_nav_rail()` |
| Command palette badges | `src/ui/commands/command_palette.cpp` | Badge/background/text styling |
| Timeline controls | `src/ui/animation/timeline_editor.cpp` | Transport visuals and spacing |
| Menu bar hover states | `src/ui/imgui/imgui_integration.cpp` | `draw_menubar()` helpers |
| Inspector panel empty states | `src/ui/overlay/inspector.cpp` | Empty-state rendering |
| Split view dividers | `src/ui/docking/split_view.cpp` | Split pane boundaries |
| Design tokens | `src/ui/theme/design_tokens.hpp` | Spacing/radius/opacity constants |
| Vulkan text quality | `src/render/text_renderer.cpp` | Atlas and glyph rendering choices |

## Common Fix Patterns

- Theme alpha fix: prefer `Color(r, g, b, a)` over opaque hex colors when transparency matters.
- Position fix: move overlays and labels inside viewport bounds and clamp positions.
- Styling fix: use ImGui draw-list primitives (`AddRectFilled`, `AddLine`) for pills, separators, and subtle borders.
- Verify-before-fix: inspect current code and recent QA entries before editing.
- Split-view validation: ensure at least two figures exist before concluding split behavior is broken.

## Verification Checklist

1. Run at least one QA-agent command in the task.
2. Capture baseline and post-fix `--design-review` outputs with same seed.
3. Rebuild and run `ctest --test-dir build --output-on-failure`.
4. Refresh goldens if visual baseline intentionally changed.
5. Update `plans/QA_design_review.md` status and evidence.
6. Record QA-agent gaps in `plans/QA_update.md` and non-visual bugs in `plans/QA_results.md` when applicable.

## Known Constraints

- Requires a live display (no headless mode).
- Tracks CPU RSS, not GPU memory.
- Does not integrate Vulkan validation-layer error capture yet.
