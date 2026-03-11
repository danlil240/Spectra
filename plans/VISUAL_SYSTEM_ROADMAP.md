# Visual System Redesign — Implementation Roadmap

**Reference:** [VISUAL_SYSTEM_REDESIGN.md](VISUAL_SYSTEM_REDESIGN.md)
**Created:** 2026-03-12
**Status:** In Progress

---

## Agent Instructions

> **All agents working on the Visual System Redesign MUST:**
>
> 1. Read [VISUAL_SYSTEM_REDESIGN.md](VISUAL_SYSTEM_REDESIGN.md) before making changes.
> 2. Check this roadmap file first to understand what is done and what remains.
> 3. After completing any item, update this file:
>    - Move the item from its current section to **Completed** with the date.
>    - Add a brief note on files changed and any caveats.
> 4. Do NOT re-implement completed items. Build on existing work.
> 5. Run `cmake --build build -j$(nproc)` after all changes to verify compilation.
> 6. Run `ctest --test-dir build -LE gpu --output-on-failure` when touching test-adjacent code.
> 7. Follow existing Spectra conventions: Allman braces, 4-space indent, `snake_case`, trailing `_` members.

---

## Completed

### Phase 1 — Design Tokens + Theme Engine

All Phase 1 items are complete. The theme system is fully extended and operational.

| # | Item | Date | Notes |
|---|------|------|-------|
| 1.1 | Extend `ThemeColors` struct with all new fields | Pre-existing | `bg_canvas`, `grid_major`, `grid_minor`, `accent_glow`, `glow_intensity`, `focus_ring`, `scrollbar_thumb`, `scrollbar_track`, `section_header_bg`, `input_bg`, `hover_highlight`, `annotation_bg`, `roi_fill`, `roi_border` — all present in [src/ui/theme/theme.hpp](../src/ui/theme/theme.hpp) |
| 1.2 | Update `interpolate_colors()` for new fields | Pre-existing | All new Color fields lerped; `glow_intensity` interpolated as float. See [theme.cpp ~L1300](../src/ui/theme/theme.cpp) |
| 1.3 | Add Night theme definition | Pre-existing | Vision-inspired ultra-dark with `#3BB2F6` cyan accent, `glow_intensity=0.4`. Registered as `"night"` — default on startup |
| 1.4 | Update Dark theme colors to final spec | Pre-existing | Neutral dark, `#4D8FD6` muted blue accent, `glow_intensity=0.0` |
| 1.5 | Update Light theme colors to final spec | Pre-existing | Paper-white, `#0969DA` rich blue accent, full opacity panels |
| 1.6 | Add High Contrast theme (bonus) | Pre-existing | Pure black canvas, gold `#FFD700` accent, maximum contrast for accessibility |
| 1.7 | Add `bg_canvas` to renderer | Pre-existing | Plot canvas uses `bg_canvas` distinct from `bg_primary`. See [renderer.cpp ~L218](../src/render/renderer.cpp) |
| 1.8 | Add `grid_major`/`grid_minor` to renderer grid drawing | Pre-existing | Two-tier grid with separate alpha per tier. See [renderer.cpp ~L898](../src/render/renderer.cpp) |
| 1.9 | Add `theme_version` counter for GPU-safe propagation | Pre-existing | `theme_version_` monotonically incremented on color change in ThemeManager |
| 1.10 | Add new design tokens to `design_tokens.hpp` | Pre-existing | Glow tokens, grid alpha tokens, motion tokens, inspector rhythm, focus ring — all in [design_tokens.hpp](../src/ui/theme/design_tokens.hpp) |
| 1.11 | 8 data palettes with CVD simulation | Pre-existing | Tableau 10, Okabe-Ito, Tol Bright/Muted, IBM, Wong, Viridis, Monochrome |
| 1.12 | JSON serialization for all theme fields | Pre-existing | `export_theme()`/`import_theme()` cover all 41 color fields + float fields |

### Phase 1.5 — Theme Integration Polish (NEW)

Items bridging Phase 1 and Phase 2: wiring new tokens into ImGui and UI components.

| # | Item | Date | Notes |
|---|------|------|-------|
| 1.5.1 | `apply_to_imgui()` uses `input_bg` for frame backgrounds | 2026-03-11 | `ImGuiCol_FrameBg` now reads `colors.input_bg` instead of `bg_tertiary`. [theme.cpp ~L248](../src/ui/theme/theme.cpp) |
| 1.5.2 | `apply_to_imgui()` uses `scrollbar_thumb`/`scrollbar_track` | 2026-03-11 | `ImGuiCol_ScrollbarBg` → `scrollbar_track`, `ImGuiCol_ScrollbarGrab` → `scrollbar_thumb`. [theme.cpp ~L274](../src/ui/theme/theme.cpp) |
| 1.5.3 | `apply_to_imgui()` uses `section_header_bg` for headers | 2026-03-11 | `ImGuiCol_Header` now reads `section_header_bg`. [theme.cpp ~L299](../src/ui/theme/theme.cpp) |
| 1.5.4 | `apply_to_imgui()` uses `focus_ring` for nav highlight | 2026-03-11 | `ImGuiCol_NavHighlight` reads `focus_ring`. [theme.cpp ~L362](../src/ui/theme/theme.cpp) |
| 1.5.5 | Tab bar height updated to 36px | 2026-03-11 | `TAB_HEIGHT` changed from 32 → 36 per spec. [tab_bar.hpp ~L150](../src/ui/figures/tab_bar.hpp) |
| 1.5.6 | Tooltip dashed connection line to data point | 2026-03-11 | Dashed line drawn from tooltip bottom-center to snap point with `crosshair` color @ 40% alpha. [tooltip.cpp ~L178](../src/ui/overlay/tooltip.cpp) |
| 1.5.7 | Tooltip Night-theme glow on snap dot | 2026-03-11 | Additive glow circle behind snap dot when `glow_intensity > 0.01`. [tooltip.cpp ~L207](../src/ui/overlay/tooltip.cpp) |
| 1.5.8 | Asymmetric panel open/close timing | 2026-03-11 | Open: speed 8.0 (~150ms), Close: speed 10.0 (~120ms). [imgui_integration.cpp ~L383](../src/ui/imgui/imgui_integration.cpp) |
| 1.5.9 | Section header uses `section_header_bg` token | 2026-03-11 | Widget `section_header()` reads `section_header_bg` with +2% lift on hover. [widgets.cpp ~L66](../src/ui/imgui/widgets.cpp) |
| 1.5.10 | Asymmetric section expand/collapse timing | 2026-03-11 | Expand: speed 8.0 (~150ms), Collapse: speed 10.0 (~100ms). [widgets.cpp ~L33](../src/ui/imgui/widgets.cpp) |
| 1.5.11 | Nav rail active tool accent bar + glow | Pre-existing | 2px accent bar + additive glow when `glow_intensity > 0.01`. [imgui_integration.cpp ~L954](../src/ui/imgui/imgui_integration.cpp) |
| 1.5.12 | Hover transition system (80ms/120ms) | Pre-existing | `smooth_hover_state()` with `DURATION_HOVER=0.08f` fade-in, 120ms fade-out. [theme.hpp ~L392](../src/ui/theme/theme.hpp) |
| 1.5.13 | Tab bar 2px bottom accent indicator | Pre-existing | Active tab has 2px accent underline. [tab_bar.cpp ~L303](../src/ui/figures/tab_bar.cpp) |

---

## In Progress

_Nothing currently in progress._

---

## Remaining — Phase 2: Plot Surface + Inspector Modernization

| # | Item | Spec Section | Files to Modify | Priority |
|---|------|-------------|-----------------|----------|
| 2.1 | Zoom-dependent minor grid alpha (fade when spacing <50px) | §4.1 | `src/render/renderer.cpp` | High |
| 2.2 | Tooltip arrow/pointer toward data point | §4.4 | `src/ui/overlay/tooltip.cpp` | Medium |
| 2.3 | Legend panel mode (glass bg, compact series list, max 8 rows) | §4.3 | `src/ui/overlay/legend_interaction.cpp` | Medium |
| 2.4 | Legend inline mode (labels next to traces) | §4.3 | `src/ui/overlay/legend_interaction.cpp` | Low |
| 2.5 | Legend inspector mode (series list in inspector) | §4.3 | `src/ui/overlay/inspector.cpp` | Low |
| 2.6 | Inspector 80px fixed label column alignment | §5.2 | `src/ui/overlay/inspector.cpp`, `src/ui/imgui/widgets.cpp` | High |
| 2.7 | Slider visual redesign (4px track, 14px thumb, accent fill) | §5.4 | `src/ui/imgui/widgets.cpp` | Medium |
| 2.8 | Color picker popover (hue ring, S/L square, hex input) | §5.5 | `src/ui/imgui/widgets.cpp` (new) | Low |
| 2.9 | Focus ring system (keyboard-only visibility) | §5.3 | `src/ui/imgui/imgui_integration.cpp` | Medium |
| 2.10 | Dock tab bar styling (36px, separator hairlines) | §6.4 | `src/ui/figures/tab_bar.cpp` | Low |
| 2.11 | Nav rail tool grouping with separator hairlines | §6.1 | `src/ui/imgui/imgui_integration.cpp` | Low |
| 2.12 | Icon brightness system (55%/85%/100%/25% alpha) | §6.3 | `src/ui/imgui/imgui_integration.cpp` | Medium |

---

## Remaining — Phase 3: Interaction Polish + Glow Refinement

| # | Item | Spec Section | Files to Modify | Priority |
|---|------|-------------|-----------------|----------|
| 3.1 | Scroll-wheel zoom smoothing (exponential decay, factor 12.0) | §7.3 | `src/ui/input/input_handler.cpp` | High |
| 3.2 | Box zoom 200ms ease-out transition | §7.3 | `src/ui/input/input_handler.cpp` | Medium |
| 3.3 | Double-click reset 250ms ease-in-out | §7.3 | `src/ui/input/input_handler.cpp` | Medium |
| 3.4 | Tooltip entrance/exit hysteresis (100ms stay-visible) | §7.4 | `src/ui/overlay/tooltip.cpp` | Medium |
| 3.5 | Night theme crosshair glow (two-pass: 3px bloom + 1px sharp) | §8.4 | `src/render/renderer.cpp` | Medium |
| 3.6 | ROI dashed border + drag handles + glow | §4.7 | `src/render/renderer.cpp`, `src/ui/input/region_select.cpp` | Low |
| 3.7 | Annotation glass appearance + idle fade to 70% | §4.6 | `src/ui/overlay/` (new/existing) | Low |
| 3.8 | Theme picker with preview thumbnails | §9 Phase 3.10 | `src/ui/imgui/imgui_integration.cpp` | Low |
| 3.9 | Axis spine fade during zoom/pan interaction | §4.2 | `src/render/renderer.cpp` | Low |
| 3.10 | Mixed selection UI state (em-dash, striped swatch) | §5.6 | `src/ui/overlay/inspector.cpp` | Low |

---

## Remaining — Uncategorized / Future

| # | Item | Spec Section | Notes |
|---|------|-------------|-------|
| F.1 | Plugin-safe theme API (`include/spectra/theme_api.hpp`) | §2.8 | Public `ThemeSnapshot` struct for stable ABI |
| F.2 | Workspace file stores theme name | §2.7 | `"theme": "night"` in workspace v3 JSON |
| F.3 | Light theme WCAG AA contrast ratio validation | Issue #7 | Automated contrast checks for all text |
| F.4 | Window chrome minimalism (borderless, command bar drag) | §6.5 | Platform-dependent, careful testing needed |

---

## Verification Gates

Before marking any Phase as complete, all of the following must pass:

1. **Build:** `cmake --build build -j$(nproc)` — zero errors
2. **Tests:** `ctest --test-dir build -LE gpu --output-on-failure` — no new failures
3. **Performance:** `bench_render_throughput` must not regress >2% vs pre-change baseline
4. **Visual:** Manual inspection of Night, Dark, and Light themes with representative plots
5. **Memory:** No new per-frame allocations (ASan clean on non-GPU tests)

---

## File Quick Reference

| File | Role |
|------|------|
| [src/ui/theme/theme.hpp](../src/ui/theme/theme.hpp) | `Color`, `ThemeColors`, `Theme`, `ThemeManager` |
| [src/ui/theme/theme.cpp](../src/ui/theme/theme.cpp) | Theme definitions, `apply_to_imgui()`, interpolation, serialization |
| [src/ui/theme/design_tokens.hpp](../src/ui/theme/design_tokens.hpp) | All design constants (spacing, radius, font, animation, glow, grid) |
| [src/render/renderer.cpp](../src/render/renderer.cpp) | Grid rendering, canvas background, crosshair |
| [src/ui/overlay/tooltip.cpp](../src/ui/overlay/tooltip.cpp) | Data tooltip with connection line and glow |
| [src/ui/overlay/inspector.cpp](../src/ui/overlay/inspector.cpp) | Inspector panel layout |
| [src/ui/imgui/widgets.cpp](../src/ui/imgui/widgets.cpp) | Section headers, sliders, separators |
| [src/ui/imgui/imgui_integration.cpp](../src/ui/imgui/imgui_integration.cpp) | Panel timing, nav rail, layout orchestration |
| [src/ui/figures/tab_bar.hpp](../src/ui/figures/tab_bar.hpp) / `.cpp` | Figure tab bar |
| [src/ui/input/input_handler.cpp](../src/ui/input/input_handler.cpp) | Zoom, pan, interaction handling |
| [src/ui/overlay/legend_interaction.cpp](../src/ui/overlay/legend_interaction.cpp) | Legend rendering |
