# Spectra Visual System Redesign — Next-Generation Scientific Visualization

**Document Version:** 1.0
**Date:** 2026-03-11
**Status:** Architectural Specification
**Reference:** Vision.png (project root)

---

## Table of Contents

1. [Visual Language Extraction](#1-visual-language-extraction)
2. [Theming System Architecture](#2-theming-system-architecture)
3. [Surface Hierarchy Redesign](#3-surface-hierarchy-redesign)
4. [Plot Area Redesign](#4-plot-area-redesign)
5. [Inspector Panel Modernization](#5-inspector-panel-modernization)
6. [Toolbar and Navigation Language](#6-toolbar-and-navigation-language)
7. [Motion & Interaction Polish](#7-motion--interaction-polish)
8. [Rendering Constraints](#8-rendering-constraints)
9. [Implementation Roadmap](#9-implementation-roadmap)
10. [GitHub Issue Drafts](#10-github-issue-drafts)

---

## 1. Visual Language Extraction

### 1.1 Contrast Philosophy

**Observed:** Vision.png employs an **extreme low-key contrast model** — a near-black canvas (~L*3-5) with luminous data traces and restrained UI chrome. The background recedes completely, making data the undisputed visual hero.

**Design Principle — "Data Luminance Hierarchy":**

| Layer | Luminance | Purpose |
|-------|-----------|---------|
| Canvas background | L*3–5 (near-black) | Maximum recession — invisible substrate |
| UI chrome/panels | L*8–12 | Present but subordinate — never competes with data |
| Grid/axis lines | L*12–18 (very low alpha) | Spatial reference without visual noise |
| Text labels | L*40–55 (secondary), L*85–90 (primary) | Readable but calm |
| Data traces | L*60–95 (vivid, saturated) | Maximum contrast against canvas — unmistakable |
| Accent/interaction | L*60–75 (saturated, optionally glowing) | Action states — clicks, hovers, selections |

**Why this matters for scientific plotting:**
Dense datasets with many overlapping traces need maximum dynamic range between data and background. When the canvas is near-black, even subtle color differences between series remain distinguishable. Light backgrounds compress this range, making faint signals harder to separate from noise.

### 1.2 Depth Model

**Observed:** Vision.png uses a **layered glass depth model** — not flat, not skeuomorphic. Surfaces sit at distinct z-planes communicated through:
- Subtle background luminance steps (not shadows)
- Translucent panel backgrounds (glass-like, 88-95% opacity)
- Soft inner glow on interactive/focused elements
- No drop shadows; depth is chromatic, not geometric

**Design Principle — "Chromatic Depth":**
Each surface layer is 2-4 lightness steps brighter than its parent. This creates a natural z-stack perception without expensive blur/shadow effects. The depth model is:

```
Z0: Canvas (deepest, darkest)        — 0x0A0E13
Z1: Panels, rails, bars              — 0x12161D  (+8 lightness)
Z2: Cards, inputs, sections          — 0x1A1F27  (+8 lightness)
Z3: Elevated (tooltips, popovers)    — 0x222830  (+8 lightness)
Z4: Glow layer (accent highlights)   — additive blend, not a surface
```

**Why this matters:** Scientific tools are used for hours. Geometric shadows (CSS-style box-shadow) create visual fatigue at sustained use. Chromatic depth is quieter and scales better as panels multiply in docked layouts.

### 1.3 Surface Hierarchy

**Five-tier surface system with consistent 8-lightness-step increments:**

| Tier | Token | Night Hex | Usage |
|------|-------|-----------|-------|
| Ground | `bg_canvas` | `#070A0F` | Plot area background — deepest black |
| Base | `bg_primary` | `#0C1117` | Window/app background |
| Surface-1 | `bg_secondary` | `#141A22` | Panels, inspector, nav rail, status bar |
| Surface-2 | `bg_tertiary` | `#1C222B` | Input fields, cards, collapsible section bodies |
| Elevated | `bg_elevated` | `#252C36` | Tooltips, popovers, dropdown menus, context menus |

**Why five tiers:** Docked scientific layouts have panels-within-panels. A 3-tier system collapses visually when an inspector section contains a nested card. Five tiers provide enough headroom.

### 1.4 Accent Color Strategy

**Observed:** Vision.png uses a **cyan-blue primary accent** (`~#3BB2F6` / `~#58B8F0`) with selective neon glow on interactive elements. No secondary accent color — the system is monochromatic with semantic colors only.

**Design Principle — "Monochromatic Accent + Semantic":**

| Role | Color | Usage |
|------|-------|-------|
| Accent primary | `#3BB2F6` (Night) | Active states, selected items, focus rings, sliders, checkmarks |
| Accent hover | `#5CC4FF` | Hover state — slightly brighter and more saturated |
| Accent muted | `#3BB2F6` @ 25% | Selected backgrounds, accent tints |
| Accent subtle | `#3BB2F6` @ 10% | Very faint accent wash for hover surfaces |
| Accent glow | `#3BB2F6` @ 40%, 4px blur | Night theme only — soft bloom on active elements |
| Success | `#34D399` | Confirmation, connected status |
| Warning | `#FBBF24` | Caution states |
| Error | `#F87171` | Destructive actions, validation errors |
| Info | `#60A5FA` | Informational badges |

**Why monochromatic:** Multiple accent colors create visual confusion in data-dense layouts. When the plotting canvas already displays 10+ series colors, the UI chrome must stay monochromatic to avoid competing.

### 1.5 Data Color vs UI Color Separation

**Critical principle:** Data colors and UI colors must NEVER overlap.

| Domain | Palette Source | Saturation | Brightness |
|--------|---------------|------------|------------|
| Data series | Tableau 10, Okabe-Ito, Tol etc. | 70-100% | 60-90% |
| UI interactive | Accent (cyan-blue) | 60-80% | 55-75% |
| UI chrome | Grays only | 0% | 5-55% |

**Implementation rule:** The accent color (`#3BB2F6`) is chosen to NOT appear in any default data palette. This prevents a "which blue is the button vs. which blue is Series 3" confusion.

### 1.6 Gridline Treatment

**Observed:** Vision.png grid lines are barely visible — faint dotted or very low-alpha lines that provide spatial reference without competing with data.

**Design Principle — "Invisible Grid":**

| Grid Element | Night Theme | Dark Theme | Light Theme |
|-------------|-------------|------------|-------------|
| Major grid | `white @ 8%` | `white @ 10%` | `black @ 12%` |
| Minor grid | `white @ 4%` | `white @ 5%` | `black @ 6%` |
| Axis spine | `white @ 35%` | `white @ 45%` | `black @ 50%` |
| Tick marks | `white @ 25%` | `white @ 30%` | `black @ 35%` |
| Tick labels | `#5A6370` | `#6B7582` | `#656D76` |

**Why this aggressive recession:** Grid lines are reference geometry, not data. In oscilloscope-style displays, engineers glance at the grid to estimate values but stare at the waveform. The grid must be subliminal — present when sought, invisible otherwise.

### 1.7 Typography Hierarchy

**Observed:** Vision.png uses a clean sans-serif with clear weight differentiation and monospace for numerical readouts.

**Design Principle — "Three Voice Typography":**

| Voice | Font | Weight | Size | Usage |
|-------|------|--------|------|-------|
| Chrome | Inter | Regular (400) | 11.5-13px | Menu items, labels, section headers |
| Data | Inter Mono / JetBrains Mono | Regular (400) | 12px | Coordinates, values, tick labels, tooltip numbers |
| Title | Inter | SemiBold (600) | 15-20px | Plot titles, panel titles, figure names |

**Spacing rules:**
- Line height: 1.4× font size for readability in dense layouts
- Letter spacing: +0.01em for mono, default for sans
- ALL CAPS for section headers (tracked at +0.05em, scaled to FONT_XS)

### 1.8 Iconography Density

**Observed:** Vision.png uses thin-stroke icons with consistent optical weight, moderate density (not cluttered).

**Design Principle — "Ghost Icons":**
- Icons use `text_secondary` color (L*40-55) — never full white/black
- Active icon uses accent color
- Icon size: 20px standard, 16px compact, 24px emphasis
- Minimum touch target: 32×32px (even if icon is 20px)
- Stroke weight: 1.5px (matches Inter's visual weight)

### 1.9 Motion Philosophy

**Observed:** Vision.png implies subtle, purposeful motion — smooth transitions, no bouncing or overshoot.

**Design Principle — "Instrument Motion":**
Scientific instruments don't animate playfully. Motion must feel like precision equipment:

| Action | Duration | Easing | Notes |
|--------|----------|--------|-------|
| Hover state change | 80ms | ease-out | Near-instant but not jarring |
| Panel expand/collapse | 150ms | ease-in-out | Smooth, deliberate |
| Theme transition | 200ms | ease-in-out | Gentle color morph |
| Plot zoom | 120ms | ease-out | Responsive, no overshoot |
| Tooltip appear | 50ms enter, 100ms exit | ease-out / ease-in | Fast in, gentle out |
| Tab switch | 0ms content, 100ms indicator | instant / ease-out | Content is instant, selection indicator slides |
| Timeline scrub | 0ms | linear | Direct manipulation, no lag |
| Value change (slider) | 0ms | linear | Direct manipulation |

**Why no bounce/spring:** Overshoot animations suggest imprecision. Engineers distrust tools that "wiggle."

---

## 2. Theming System Architecture

### 2.1 Current State Assessment

The existing `ThemeManager` in [src/ui/theme/theme.hpp](src/ui/theme/theme.hpp) already provides:
- Singleton theme manager with runtime transitions
- 28-field `ThemeColors` struct (surfaces, text, borders, interactive, semantic, plot-specific)
- Smooth color interpolation during theme switches
- JSON export/import
- Design tokens in [src/ui/theme/design_tokens.hpp](src/ui/theme/design_tokens.hpp)
- Data palette management with CVD simulation

**What needs to change:** The existing system is well-architected. The redesign extends it rather than replacing it.

### 2.2 Extended Theme Tokens

Add the following to `ThemeColors`:

```cpp
struct ThemeColors
{
    // ── Existing fields (preserved) ──
    // Surfaces: bg_primary, bg_secondary, bg_tertiary, bg_elevated, bg_overlay
    // Text: text_primary, text_secondary, text_tertiary, text_inverse
    // Borders: border_default, border_subtle, border_strong
    // Interactive: accent, accent_hover, accent_muted, accent_subtle
    // Semantic: success, warning, error, info
    // Plot: grid_line, axis_line, tick_label, crosshair, selection_fill,
    //        selection_border, tooltip_bg, tooltip_border

    // ── New fields ──

    // Canvas (separate from bg_primary for plot area)
    Color bg_canvas;            // Plot canvas background (darkest surface)

    // Grid hierarchy
    Color grid_major;           // Major grid lines (replaces grid_line)
    Color grid_minor;           // Minor grid lines (fainter)

    // Glow / accent effects (Night theme distinguisher)
    Color accent_glow;          // Glow color for active elements (0 alpha in non-Night themes)
    float glow_intensity;       // 0.0 = off (Dark/Light), 0.3-0.5 = Night

    // Focus
    Color focus_ring;           // Keyboard focus ring color

    // Scrollbar
    Color scrollbar_thumb;      // Scrollbar thumb color
    Color scrollbar_track;      // Scrollbar track (usually transparent)

    // Inspector-specific
    Color section_header_bg;    // Collapsible section header background
    Color input_bg;             // Input field background (may differ from bg_tertiary)

    // Data interaction
    Color hover_highlight;      // Data point hover highlight
    Color annotation_bg;        // Annotation background
    Color roi_fill;             // ROI fill color
    Color roi_border;           // ROI border color
};
```

### 2.3 Extended Design Tokens

Add to `design_tokens.hpp`:

```cpp
// ── Glow / Accent Effect Tokens ──
constexpr float GLOW_RADIUS_SM = 2.0f;    // Subtle inner glow
constexpr float GLOW_RADIUS_MD = 4.0f;    // Standard glow (active elements)
constexpr float GLOW_RADIUS_LG = 8.0f;    // Emphasis glow (selected items)

// ── Grid Tokens ──
constexpr float GRID_MAJOR_ALPHA_NIGHT = 0.08f;
constexpr float GRID_MAJOR_ALPHA_DARK  = 0.10f;
constexpr float GRID_MAJOR_ALPHA_LIGHT = 0.12f;
constexpr float GRID_MINOR_ALPHA_NIGHT = 0.04f;
constexpr float GRID_MINOR_ALPHA_DARK  = 0.05f;
constexpr float GRID_MINOR_ALPHA_LIGHT = 0.06f;

// ── Motion Tokens (extended) ──
constexpr float DURATION_HOVER      = 0.08f;   // Hover transitions
constexpr float DURATION_TOOLTIP_IN = 0.05f;   // Tooltip appear
constexpr float DURATION_TOOLTIP_OUT= 0.10f;   // Tooltip disappear
constexpr float DURATION_ZOOM       = 0.12f;   // Plot zoom animation

// ── Inspector Rhythm ──
constexpr float SECTION_HEADER_HEIGHT  = 32.0f;
constexpr float SECTION_CONTENT_INSET  = 12.0f;  // Left indent for section content
constexpr float INSPECTOR_LABEL_WIDTH  = 80.0f;  // Fixed label column width
constexpr float INSPECTOR_INPUT_HEIGHT = 28.0f;  // Compact input height

// ── Focus Ring ──
constexpr float FOCUS_RING_WIDTH  = 2.0f;
constexpr float FOCUS_RING_OFFSET = 2.0f;   // Offset from element edge
```

### 2.4 Theme Definitions

#### Theme 1 — Night (Vision-Inspired)

```cpp
Theme night;
night.name = "night";
night.colors = {
    // Canvas & Surfaces — ultra-dark with blue undertone
    .bg_canvas    = Color::from_hex(0x070A0F),   // Deepest — plot area
    .bg_primary   = Color::from_hex(0x0C1117),   // App window background
    .bg_secondary = Color::from_hex(0x141A22),   // Panels, rails
    .bg_tertiary  = Color::from_hex(0x1C222B),   // Inputs, cards
    .bg_elevated  = Color::from_hex(0x252C36),   // Tooltips, popovers
    .bg_overlay   = Color(0.0f, 0.0f, 0.0f, 0.60f),

    // Text — cool-white hierarchy
    .text_primary   = Color::from_hex(0xE8ECF2),   // Pure white feels harsh; this is L*92
    .text_secondary = Color::from_hex(0x6B7A8D),   // Cool gray for labels
    .text_tertiary  = Color::from_hex(0x3D4854),   // Very dim for disabled/placeholders
    .text_inverse   = Color::from_hex(0x0C1117),

    // Borders — barely visible structural dividers
    .border_default = Color::from_hex(0x1E2630),
    .border_subtle  = Color::from_hex(0x161C24),
    .border_strong  = Color::from_hex(0x3A4554),

    // Interactive — cyan-blue accent with glow potential
    .accent        = Color::from_hex(0x3BB2F6),   // Vivid cyan-blue
    .accent_hover  = Color::from_hex(0x5CC4FF),   // Brighter on hover
    .accent_muted  = Color(0.23f, 0.70f, 0.96f, 0.25f),
    .accent_subtle = Color(0.23f, 0.70f, 0.96f, 0.10f),

    // Semantic — neon-adjacent for Night theme
    .success = Color::from_hex(0x34D399),
    .warning = Color::from_hex(0xFBBF24),
    .error   = Color::from_hex(0xF87171),
    .info    = Color::from_hex(0x60A5FA),

    // Plot — ultra-receded grid, vivid data
    .grid_major       = Color(1.0f, 1.0f, 1.0f, 0.08f),
    .grid_minor       = Color(1.0f, 1.0f, 1.0f, 0.04f),
    .grid_line        = Color(1.0f, 1.0f, 1.0f, 0.08f),   // Compat
    .axis_line        = Color(0.60f, 0.65f, 0.72f, 0.35f),
    .tick_label       = Color::from_hex(0x5A6370),
    .crosshair        = Color(0.23f, 0.70f, 0.96f, 0.60f),
    .selection_fill   = Color(0.23f, 0.70f, 0.96f, 0.15f),
    .selection_border = Color::from_hex(0x3BB2F6),
    .tooltip_bg       = Color(0.10f, 0.12f, 0.16f, 0.92f),
    .tooltip_border   = Color(0.20f, 0.24f, 0.30f, 0.30f),

    // New fields
    .accent_glow      = Color(0.23f, 0.70f, 0.96f, 0.40f),
    .glow_intensity   = 0.4f,
    .focus_ring       = Color::from_hex(0x3BB2F6),
    .scrollbar_thumb  = Color(1.0f, 1.0f, 1.0f, 0.15f),
    .scrollbar_track  = Color(0.0f, 0.0f, 0.0f, 0.0f),
    .section_header_bg = Color(1.0f, 1.0f, 1.0f, 0.03f),
    .input_bg         = Color::from_hex(0x1C222B),
    .hover_highlight  = Color(0.23f, 0.70f, 0.96f, 0.30f),
    .annotation_bg    = Color(0.10f, 0.12f, 0.16f, 0.85f),
    .roi_fill         = Color(0.23f, 0.70f, 0.96f, 0.10f),
    .roi_border       = Color(0.23f, 0.70f, 0.96f, 0.50f),
};
night.opacity_panel    = 0.92f;
night.opacity_tooltip  = 0.94f;
night.shadow_intensity = 0.0f;   // No shadows — chromatic depth only
night.use_blur         = false;  // No blur — pure glass via alpha
```

#### Theme 2 — Dark (Professional)

```cpp
Theme dark;
dark.name = "dark";
dark.colors = {
    // Canvas & Surfaces — neutral dark, no blue tint
    .bg_canvas    = Color::from_hex(0x0E1218),
    .bg_primary   = Color::from_hex(0x0A0E13),   // (Existing — preserved)
    .bg_secondary = Color::from_hex(0x12161D),
    .bg_tertiary  = Color::from_hex(0x1A1F27),
    .bg_elevated  = Color::from_hex(0x222830),
    .bg_overlay   = Color(0.0f, 0.0f, 0.0f, 0.50f),

    // Text — neutral white hierarchy
    .text_primary   = Color::from_hex(0xE2E8F0),
    .text_secondary = Color::from_hex(0x7B8794),
    .text_tertiary  = Color::from_hex(0x454D56),
    .text_inverse   = Color::from_hex(0x0A0E13),

    // Borders
    .border_default = Color::from_hex(0x2A3038),
    .border_subtle  = Color::from_hex(0x1E242C),
    .border_strong  = Color::from_hex(0x6E7681),

    // Interactive — muted blue (existing palette preserved)
    .accent        = Color::from_hex(0x4D8FD6),
    .accent_hover  = Color::from_hex(0x6AAAE8),
    .accent_muted  = Color(0.30f, 0.56f, 0.84f, 0.25f),
    .accent_subtle = Color(0.30f, 0.56f, 0.84f, 0.10f),

    // Semantic (existing — preserved)
    .success = Color::from_hex(0x3FB950),
    .warning = Color::from_hex(0xD29922),
    .error   = Color::from_hex(0xF85149),
    .info    = Color::from_hex(0x4D8FD6),

    // Plot — standard recession
    .grid_major       = Color(1.0f, 1.0f, 1.0f, 0.10f),
    .grid_minor       = Color(1.0f, 1.0f, 1.0f, 0.05f),
    .grid_line        = Color(1.0f, 1.0f, 1.0f, 0.10f),
    .axis_line        = Color(0.55f, 0.58f, 0.63f, 0.50f),
    .tick_label       = Color::from_hex(0x6B7582),
    .crosshair        = Color(0.30f, 0.56f, 0.84f, 0.70f),
    .selection_fill   = Color(0.30f, 0.56f, 0.84f, 0.20f),
    .selection_border = Color::from_hex(0x4D8FD6),
    .tooltip_bg       = Color(0.13f, 0.15f, 0.19f, 0.92f),
    .tooltip_border   = Color(0.22f, 0.25f, 0.30f, 0.35f),

    // New fields — glow off
    .accent_glow      = Color(0.0f, 0.0f, 0.0f, 0.0f),
    .glow_intensity   = 0.0f,
    .focus_ring       = Color::from_hex(0x4D8FD6),
    .scrollbar_thumb  = Color(1.0f, 1.0f, 1.0f, 0.20f),
    .scrollbar_track  = Color(0.0f, 0.0f, 0.0f, 0.0f),
    .section_header_bg = Color(1.0f, 1.0f, 1.0f, 0.04f),
    .input_bg         = Color::from_hex(0x1A1F27),
    .hover_highlight  = Color(0.30f, 0.56f, 0.84f, 0.25f),
    .annotation_bg    = Color(0.13f, 0.15f, 0.19f, 0.85f),
    .roi_fill         = Color(0.30f, 0.56f, 0.84f, 0.12f),
    .roi_border       = Color(0.30f, 0.56f, 0.84f, 0.45f),
};
dark.opacity_panel    = 0.95f;
dark.opacity_tooltip  = 0.95f;
dark.shadow_intensity = 0.0f;
dark.use_blur         = false;
```

#### Theme 3 — Light (Scientific Publication)

```cpp
Theme light;
light.name = "light";
light.colors = {
    // Canvas & Surfaces — warm paper-white
    .bg_canvas    = Color::from_hex(0xFAFBFC),   // Very slightly warm
    .bg_primary   = Color::from_hex(0xFFFFFF),
    .bg_secondary = Color::from_hex(0xF6F8FA),
    .bg_tertiary  = Color::from_hex(0xF0F2F5),
    .bg_elevated  = Color::from_hex(0xFFFFFF),
    .bg_overlay   = Color(0.0f, 0.0f, 0.0f, 0.30f),

    // Text — dark ink
    .text_primary   = Color::from_hex(0x1F2328),
    .text_secondary = Color::from_hex(0x656D76),
    .text_tertiary  = Color::from_hex(0x8B949E),
    .text_inverse   = Color::from_hex(0xFFFFFF),

    // Borders — subtle rules
    .border_default = Color::from_hex(0xD0D7DE),
    .border_subtle  = Color::from_hex(0xE8ECF0),
    .border_strong  = Color::from_hex(0x8C959F),

    // Interactive — rich blue
    .accent        = Color::from_hex(0x0969DA),
    .accent_hover  = Color::from_hex(0x0860CA),
    .accent_muted  = Color(0.04f, 0.41f, 0.85f, 0.15f),
    .accent_subtle = Color(0.04f, 0.41f, 0.85f, 0.06f),

    // Semantic — publication-appropriate saturation
    .success = Color::from_hex(0x1A7F37),
    .warning = Color::from_hex(0x9A6700),
    .error   = Color::from_hex(0xCF222E),
    .info    = Color::from_hex(0x0969DA),

    // Plot — clean academic gridlines
    .grid_major       = Color(0.0f, 0.0f, 0.0f, 0.12f),
    .grid_minor       = Color(0.0f, 0.0f, 0.0f, 0.06f),
    .grid_line        = Color(0.0f, 0.0f, 0.0f, 0.12f),
    .axis_line        = Color(0.15f, 0.18f, 0.22f, 0.60f),
    .tick_label       = Color::from_hex(0x656D76),
    .crosshair        = Color(0.04f, 0.41f, 0.85f, 0.70f),
    .selection_fill   = Color(0.04f, 0.41f, 0.85f, 0.12f),
    .selection_border = Color::from_hex(0x0969DA),
    .tooltip_bg       = Color::from_hex(0xFFFFFF),
    .tooltip_border   = Color::from_hex(0xD0D7DE),

    // New fields — no glow, no effects
    .accent_glow       = Color(0.0f, 0.0f, 0.0f, 0.0f),
    .glow_intensity    = 0.0f,
    .focus_ring        = Color::from_hex(0x0969DA),
    .scrollbar_thumb   = Color(0.0f, 0.0f, 0.0f, 0.18f),
    .scrollbar_track   = Color(0.0f, 0.0f, 0.0f, 0.0f),
    .section_header_bg = Color(0.0f, 0.0f, 0.0f, 0.03f),
    .input_bg          = Color::from_hex(0xFFFFFF),
    .hover_highlight   = Color(0.04f, 0.41f, 0.85f, 0.15f),
    .annotation_bg     = Color(1.0f, 1.0f, 1.0f, 0.90f),
    .roi_fill          = Color(0.04f, 0.41f, 0.85f, 0.08f),
    .roi_border        = Color(0.04f, 0.41f, 0.85f, 0.40f),
};
light.opacity_panel    = 1.0f;
light.opacity_tooltip  = 1.0f;
light.shadow_intensity = 0.0f;
light.use_blur         = false;
```

### 2.5 Runtime Switching (No Restart)

**Already implemented.** The existing `ThemeManager::transition_to()` provides smooth runtime switching via `interpolate_colors()`. The new fields will be added to the interpolation function.

**Required change:** Extend `interpolate_colors()` to include the new `ThemeColors` fields (`bg_canvas`, `grid_major`, `grid_minor`, `accent_glow`, `focus_ring`, `scrollbar_thumb`, `scrollbar_track`, `section_header_bg`, `input_bg`, `hover_highlight`, `annotation_bg`, `roi_fill`, `roi_border`), and interpolate `glow_intensity` as a float.

### 2.6 GPU-Safe Color Propagation Strategy

**Current state:** The renderer reads theme colors each frame via `ThemeManager::instance().colors()`. This is already GPU-safe — colors are read on the app thread and passed to the render thread via push constants.

**Enhancement:** Add a version counter to `ThemeColors`:

```cpp
uint32_t theme_version = 0;  // Incremented on every color change
```

The renderer can compare `theme_version` against its cached version to skip re-uploading push constants when nothing changed. This avoids per-frame redundant work.

### 2.7 Serialization Support

**Already implemented.** `ThemeManager::export_theme()` and `import_theme()` provide JSON serialization. Extended fields will follow the same pattern.

**Workspace integration:** The workspace file format (v3) should store `theme_name` in its metadata:

```json
{
  "version": 3,
  "theme": "night",
  "data_palette": "default",
  ...
}
```

On workspace load, call `ThemeManager::instance().set_theme(workspace.theme)`.

### 2.8 Plugin-Safe Theme API

Expose a public header `include/spectra/theme_api.hpp`:

```cpp
namespace spectra
{
    // Read-only theme queries for plugins
    struct ThemeSnapshot
    {
        uint32_t bg_canvas;    // RGBA hex
        uint32_t bg_primary;
        uint32_t accent;
        uint32_t text_primary;
        uint32_t text_secondary;
        float    glow_intensity;
        // ... minimal subset for plugin rendering
    };

    ThemeSnapshot get_current_theme_snapshot();
    void register_theme_change_callback(void (*callback)(const ThemeSnapshot&));
}
```

This provides a stable ABI for plugins without exposing the full Theme struct.

---

## 3. Surface Hierarchy Redesign

### 3.1 Surface Definitions

| Surface | Token | Z-depth | Usage |
|---------|-------|---------|-------|
| **Canvas Ground** | `bg_canvas` | Z0 (deepest) | Plot area background — the "paper" |
| **App Background** | `bg_primary` | Z0.5 | Main window background visible at edges |
| **Panel Surface** | `bg_secondary` | Z1 | Inspector, nav rail, command bar, status bar |
| **Input Surface** | `bg_tertiary` | Z2 | Input fields, chips, cards, collapsible bodies |
| **Hover Surface** | `bg_tertiary + 0.03` | Z2.5 | Hovered inputs (lightness lift) |
| **Elevated Surface** | `bg_elevated` | Z3 | Tooltips, popovers, context menus, dropdowns |
| **Overlay** | `bg_overlay` | Z4 | Modal dim layer |

### 3.2 Z-Depth Perception Strategy (No Blur)

**Primary method — Chromatic stepping:**
Each surface tier is 2-4% brighter than its parent. This creates perceived depth through luminance alone. At Night theme levels (L*3-12), even small steps are perceptible.

**Secondary method — Border hairlines:**
A 0.5px border at `border_subtle` (very low alpha) on panel edges provides structural definition without shadow weight. This is already in the existing codebase (`BORDER_WIDTH_THIN = 0.5f`).

**Tertiary method — Selective alpha transparency:**
Panels at 88-95% opacity let the canvas show through very subtly. This creates a "frosted glass" feel without any blur shader:

```
Panel pixel = panel_color * opacity + canvas_color * (1.0 - opacity)
```

This is a standard alpha blend — zero GPU cost beyond what ImGui already does.

**What to avoid:**
- No box-shadow (expensive, visually heavy)
- No backdrop blur (requires extra render pass or readback)
- No per-pixel elevation shading
- No gradient surfaces (flat color with alpha is sufficient)

### 3.3 How Surfaces Map to Layout Zones

```
┌─────────────────────────────────────────────────┐
│  bg_secondary (Z1)    Command Bar               │
├──────┬──────────────────────────┬────────────────┤
│      │                          │                │
│ bg_  │  bg_canvas (Z0)          │  bg_secondary  │
│ sec. │  Plot Area               │  Inspector     │
│ (Z1) │                          │  Panel (Z1)    │
│ Nav  │  Grid: grid_major/minor  │                │
│ Rail │  Data: series colors     │  bg_tertiary   │
│      │  Tooltip: bg_elevated    │  Sections (Z2) │
│      │                          │                │
├──────┴──────────────────────────┴────────────────┤
│  bg_secondary (Z1)    Status Bar                 │
└─────────────────────────────────────────────────┘
```

---

## 4. Plot Area Redesign

### 4.1 Gridline Alpha System

Implement a **two-tier grid** with separate alpha for major and minor lines:

```cpp
// In Renderer::render_grid():
if (is_major_gridline)
{
    pc.color[0] = colors.grid_major.r;
    pc.color[1] = colors.grid_major.g;
    pc.color[2] = colors.grid_major.b;
    pc.color[3] = colors.grid_major.a;  // 0.08 for Night
}
else
{
    pc.color[0] = colors.grid_minor.r;
    pc.color[1] = colors.grid_minor.g;
    pc.color[2] = colors.grid_minor.b;
    pc.color[3] = colors.grid_minor.a;  // 0.04 for Night
}
```

**Behavior:** Major gridlines appear at labeled tick positions. Minor gridlines appear at subdivision positions (typically 5 per major interval). Minor gridlines fade in only when zoom level makes them readable (alpha scales with zoom).

### 4.2 Axis Spine Visibility Strategy

**Default:** All four spines visible but faint (`axis_line` color).
**On zoom/pan:** Bottom and left spines remain; top and right spines fade to 0 alpha during interaction (reduce clutter during exploration).
**On data hover:** Spines dim slightly to maximize data contrast.

**Rule:** Spines should NEVER be brighter than the data. If any series has luminance below `axis_line`, the axis fades proportionally.

### 4.3 Legend Style

**Three legend modes** (user-selectable per axes):

| Mode | Description | Best For |
|------|-------------|----------|
| **Panel** (default) | Inside plot, glass background `bg_elevated @ 88%`, rounded corners `r8`, compact series list | 1-8 series |
| **Inline** | Labels placed directly next to data traces — no background box | 1-4 non-overlapping series |
| **Inspector** | No in-plot legend; series list in Inspector panel serves as legend | 9+ series or screen-constrained layouts |

**Panel legend visual spec:**
- Background: `tooltip_bg` (matches tooltip opacity)
- Border: `border_subtle` at 0.5px
- Corner radius: `RADIUS_MD` (8px)
- Padding: 8px horizontal, 6px vertical
- Series swatch: 10×10px rounded rect with `RADIUS_SM` (4px)
- Label font: `FONT_SM` (11.5px), color `text_secondary`
- Row height: 20px
- Max visible: 8 rows, then scrollable

### 4.4 Tooltip Style

**Modern tooltip spec:**

```
┌──────────────────────┐
│  X: 2.437 s          │   ← Mono font, text_secondary
│  Y: 0.728            │   ← Mono font, text_primary, bold
│  dY/dx: 0.312        │   ← Mono font, text_secondary
└──────────────────────┘
```

- Background: `tooltip_bg` (near-opaque glass)
- Border: `tooltip_border` at 0.5px
- Corner radius: `RADIUS_MD` (8px)
- Padding: 10px horizontal, 8px vertical
- Arrow: 6px triangular pointer toward data point
- Connection line: thin dashed line from tooltip to data point in `crosshair` color @ 40%
- Entrance: Fade in 50ms when cursor enters snap radius (8px from nearest point)
- Exit: Fade out 100ms when cursor leaves snap radius
- Night theme enhancement: subtle 2px outer glow in `accent_glow` color

### 4.5 Cursor Crosshair Style

- Color: `crosshair` (accent @ 60-70% alpha)
- Width: 1px (not 2px — precision tool feel)
- Style: solid, full-span (edge to edge of plot area)
- Intersection dot: 4px circle at data point with 1px stroke in `accent`
- Night theme: crosshair lines have 1px `accent_glow` bloom (additive blend)

### 4.6 Annotation Appearance

- Background: `annotation_bg` (glass, rounded)
- Border: `border_subtle`
- Corner radius: `RADIUS_MD`
- Connection: thin solid line from annotation to data point
- Arrow: small triangle at target end
- Font: `FONT_SM`, color `text_secondary`
- Non-interactive opacity: 70% (reduces visual clutter)
- Hover opacity: 100% (becomes fully readable on interaction)

### 4.7 ROI (Region of Interest) Visual Language

- Fill: `roi_fill` (accent @ 10-15% alpha)
- Border: `roi_border` (accent @ 40-50% alpha), 1.5px dashed
- Handles: 6×6px squares at corners and midpoints, filled `accent`, stroked `bg_canvas`
- Active ROI: border becomes solid, alpha increases to 70%
- Night theme: ROI border gets 2px `accent_glow` bloom

---

## 5. Inspector Panel Modernization

### 5.1 Section Spacing Rhythm

**Rhythm system — 4px base:**

```
Section Header (32px height)
├─ 4px gap (SPACE_1)
├─ Content row (28px input height)
├─ 4px gap between rows
├─ Content row
├─ Content row
├─ 8px gap (SPACE_2) — end of section content
12px gap (SECTION_GAP) — between sections
Section Header (32px height)
...
```

**Section header spec:**
- Height: 32px
- Background: `section_header_bg` (white @ 3% in dark, black @ 3% in light)
- Padding: 12px left
- Font: `FONT_XS` (10.5px), ALL CAPS, tracked +0.05em, color `text_secondary`
- Disclosure icon: 10px chevron, rotates 90° with 100ms ease-in-out
- Hover: background lightens by 2% (subtle lift)

### 5.2 Input Sizing Rules

| Control | Width | Height | Notes |
|---------|-------|--------|-------|
| Text input | Fill available | 28px | Full width minus label |
| Number input | 64px | 28px | Fixed width for precision |
| Slider | Fill available | 28px | Track height 4px, thumb 14px |
| Color picker | 28×28px swatch | 28px | Click opens popover |
| Checkbox | 16×16px | 28px row | With label |
| Dropdown | Fill available | 28px | Right-aligned chevron |
| Toggle | 32×18px | 28px row | Pill-shaped with smooth slide |

**Label-value layout:**
```
[  80px label  ] [ ——— remaining width: value input ——— ]
```

All label columns align. This creates a clean scan line for engineers comparing values across sections.

### 5.3 Focus Ring System

- Visible on keyboard navigation only (not mouse focus)
- Color: `focus_ring` (accent color)
- Width: 2px
- Offset: 2px outside element bounds
- Corner radius: element radius + 2px
- Animation: 100ms ease-in on appear, 50ms ease-out on disappear
- Tab index: section headers → inputs within section → next section

### 5.4 Slider Visual Redesign

**Track:**
- Height: 4px
- Background: `bg_tertiary`
- Filled portion: `accent` color
- Corner radius: 2px (pill)

**Thumb:**
- Diameter: 14px
- Fill: `accent`
- Border: 2px `bg_secondary` (creates contrast ring)
- Night theme: 2px `accent_glow` bloom around thumb

**Value label:**
- On drag: floating value label appears above thumb (mono font, `FONT_XS`)
- Font: `FONT_MONO`, color `text_primary`
- Background: `bg_elevated`, corner radius `RADIUS_SM`

### 5.5 Series Color Picker

**Swatch button:** 28×28px rounded rect showing current series color.

**Popover on click:**
- Background: `bg_elevated`
- Border: `border_subtle` at 0.5px
- Corner radius: `RADIUS_LG` (12px)
- Content:
  1. **Hue ring** (160×160px) — circular hue selector
  2. **Saturation/Lightness square** (120×120px) — within hue ring
  3. **Alpha slider** — horizontal, below square
  4. **Hex input** — mono font, 7 chars (#RRGGBB)
  5. **Preset swatches** — current data palette as a row of 10 clickable swatches (16×16px)
- Entrance: scale from 0.95 to 1.0 with 100ms ease-out
- Dismiss: click outside or Escape

### 5.6 Mixed Selection UI State

When multiple series are selected simultaneously:

- Shared values display the common value
- Differing values show "—" (em dash) in the input field
- Editing a field applies the new value to ALL selected series
- Color swatch shows a striped gradient of selected series colors
- Checkbox for shared toggles uses a "mixed" state (horizontal dash instead of check)

---

## 6. Toolbar and Navigation Language

### 6.1 Left Rail Grouping Logic

```
┌──────┐
│  ↖   │  Select (pointer tool)
│  ✋   │  Pan
│  🔍   │  Zoom
├──────┤  ── separator (8px gap + hairline) ──
│  📏   │  Measure
│  📝   │  Annotate
│  ▭   │  ROI
├──────┤  ── separator ──
│  ◉   │  Markers
│  📐   │  Transform
├──────┤  ── separator ──
│  📊   │  Data
│  ⏱   │  Timeline
├──────┤  ── separator ──
│  🐍   │  Python
│  ❓   │  Help
└──────┘
```

**Groups:**
1. **Navigation tools** — spatial interaction (Select, Pan, Zoom)
2. **Analysis tools** — data interrogation (Measure, Annotate, ROI)
3. **Data tools** — manipulation (Markers, Transform)
4. **Panels** — panel visibility toggles (Data, Timeline)
5. **Utilities** — secondary features (Python, Help)

### 6.2 Active Tool State Visualization

| State | Background | Icon Color | Extra |
|-------|-----------|------------|-------|
| Default | Transparent | `text_secondary` @ 55% | — |
| Hover | `bg_tertiary` @ 70% | `text_primary` | — |
| Active (current tool) | `accent_subtle` (accent @ 10%) | `accent` | Night: 2px left border in `accent` |
| Pressed | `accent_muted` (accent @ 25%) | `accent` | — |

**Night theme active indicator:** A 2px accent-colored bar on the left edge of the active tool, with optional `accent_glow` bloom.

### 6.3 Icon Brightness System

Instead of fixed icon colors, icons respond to context:

```cpp
float icon_alpha(bool active, bool hovered, bool disabled)
{
    if (disabled) return 0.25f;
    if (active) return 1.0f;
    if (hovered) return 0.85f;
    return 0.55f;   // Default: subdued
}
```

Active icons: `accent` color at 100%.
Inactive icons: `text_primary` at 55%.
Hovered icons: `text_primary` at 85%.
Disabled icons: `text_primary` at 25%.

### 6.4 Dock Tab Styling

**Figure tab bar spec:**

| Element | Value |
|---------|-------|
| Tab height | 36px |
| Tab padding | 12px horizontal |
| Active tab bg | `bg_secondary` (panel surface) |
| Active tab text | `text_primary` |
| Active tab indicator | 2px bottom border in `accent` |
| Inactive tab bg | Transparent |
| Inactive tab text | `text_secondary` |
| Hover tab bg | `bg_tertiary` @ 50% |
| Close button | `text_tertiary`, `text_primary` on hover, 16px |
| Tab separator | 1px `border_subtle` between tabs |

### 6.5 Window Chrome Minimalism

- Title bar: removed (command bar serves as title bar with drag-to-move)
- Window borders: 0px (borderless on supported platforms, 1px `border_subtle` on X11)
- Resize handles: standard OS handles, no custom styling
- Close/minimize/maximize: OS-native buttons (preserve platform conventions)
- Command bar doubles as window drag region

---

## 7. Motion & Interaction Polish

### 7.1 Hover Transitions

```cpp
// Hover lift timing
constexpr float HOVER_FADE_IN  = 0.08f;  // 80ms ease-out
constexpr float HOVER_FADE_OUT = 0.12f;  // 120ms ease-in

// Implementation: lerp between base and hover color
float hover_t = smooth_hover_state(dt, is_hovered);  // 0.0 → 1.0
Color current = base_color.lerp(hover_color, hover_t);
```

All hover transitions use interruptible interpolation — if the cursor moves away mid-transition, the color reverses smoothly from its current intermediate value.

### 7.2 Panel Fade/Slide Timing

| Action | Duration | Easing | Behavior |
|--------|----------|--------|----------|
| Inspector open | 150ms | ease-out | Width expands from 0 to INSPECTOR_WIDTH |
| Inspector close | 120ms | ease-in | Width contracts to 0 |
| Nav rail expand | 150ms | ease-out | Width: 48px → 200px |
| Nav rail collapse | 120ms | ease-in | Width: 200px → 48px |
| Section expand | 150ms | ease-in-out | Height expands with alpha 0→1 |
| Section collapse | 100ms | ease-in | Height contracts with alpha 1→0 |

**Rule:** Close/collapse is always faster than open/expand. This prevents the UI from feeling sluggish when the user is trying to dismiss things.

### 7.3 Plot Zoom Smoothing

```cpp
// Zoom interpolation using exponential decay
constexpr float ZOOM_SMOOTHING = 12.0f;  // Higher = snappier

void update_zoom(float dt)
{
    float t = 1.0f - std::exp(-ZOOM_SMOOTHING * dt);
    current_axis_min = std::lerp(current_axis_min, target_axis_min, t);
    current_axis_max = std::lerp(current_axis_max, target_axis_max, t);
}
```

- Scroll wheel zoom: target changes instantly, view interpolates over ~120ms
- Box zoom: 200ms ease-out transition to selected region
- Double-click reset: 250ms ease-in-out to auto-fit limits

### 7.4 Tooltip Entrance Timing

- Appear delay: 50ms (nearly instant — data exploration is time-sensitive)
- Fade in: 50ms ease-out
- Fade out: 100ms ease-in (slightly slower to prevent flickering)
- Position: smooth tracking of nearest data point (no jumping)
- Hysteresis: tooltip stays visible for 100ms after cursor leaves snap radius (prevents flicker when crossing sparse data)

### 7.5 Timeline Scrub Feedback

- Cursor following: 0ms (direct manipulation, no interpolation)
- Playback state change: 0ms (instant play/pause, no transition)
- Keyframe marker hover: 80ms accent highlight
- Range selection: immediate visual feedback, no animation
- Timeline zoom: same exponential decay as plot zoom (120ms)

### 7.6 Performance Budget

| Category | Budget | Notes |
|----------|--------|-------|
| All UI animations combined | < 0.5ms per frame | Must not impact 60 FPS |
| Theme transition | < 0.1ms per frame | Simple lerp across ~30 float fields |
| Hover state tracking | < 0.01ms per widget | Bool + float interpolation |
| Panel resize | < 0.05ms per frame | Single float interpolation |

**Implementation rule:** All transitions use simple linear interpolation with easing curves applied as time remapping. No physics simulations, no spring models, no particle effects.

---

## 8. Rendering Constraints

### 8.1 Mandatory Performance Guarantees

All visual upgrades **must** satisfy:

| Constraint | Target | Measurement |
|-----------|--------|-------------|
| Frame rate | ≥ 60 FPS with 1M points | `build/tests/bench_render_throughput` |
| UI frame time | ≤ 2.0ms for all ImGui drawing | Measured via ImGui metrics window |
| GPU memory delta | ≤ 5MB additional for visual upgrades | VMA stats comparison before/after |
| Per-frame allocations | 0 (zero) | ASan heap profiling in CI |
| Shader compilation | 0 new pipelines (reuse existing) | Vulkan pipeline cache count |

### 8.2 What's Allowed

| Technique | Cost | Where |
|-----------|------|-------|
| Color constants in push constants | 0 — existing mechanism | Grid, axis, crosshair rendering |
| Alpha blending (standard over) | 0 — already enabled | Panel transparency |
| UV-mapped color gradients | 0 — vertex attribute | Only if baked into vertex buffer |
| Theme color interpolation on CPU | ~0.01ms | Theme transition frames only (~30 frames per transition) |
| Glow via additive blend line overdraw | ~0.05ms | Night theme only — 1 extra draw call for glowing element |

### 8.3 What's Forbidden

| Technique | Why |
|-----------|-----|
| Gaussian blur (backdrop) | Requires extra render pass + framebuffer readback |
| Per-pixel shadow maps | Excessive GPU cost for UI decorations |
| MSAA on UI overlay | ImGui uses its own AA; MSAA doubles fill rate for no benefit |
| Custom fragment shader for panel backgrounds | Pipeline proliferation; use vertex colors |
| Texture-based gradients for UI | Texture memory + binding cost; use vertex color interpolation |
| Post-process glow (bloom pass) | Full-screen render pass; use additive blend on specific elements only |

### 8.4 Night Theme Glow Implementation

The Night theme's soft glow effect is implemented WITHOUT a bloom post-process:

```
For each glowing element (active tool indicator, selected slider thumb):
1. Draw the element normally (standard blend)
2. Draw a slightly larger copy (scale 1.15x) at accent_glow alpha (0.4)
   using additive blending
```

This costs exactly 1 additional ImGui draw call per glowing element. With ~5 glowing elements visible at any time, the total cost is <0.1ms.

For the crosshair glow:
- Draw the crosshair line twice:
  1. First pass: 3px width, `accent_glow` @ 20% alpha (soft bloom)
  2. Second pass: 1px width, `crosshair` color (sharp center)
- Total: 2 draw calls vs. 1 — negligible.

---

## 9. Implementation Roadmap

### Phase 1 — Design Tokens + Theme Engine (Est. 2 weeks)

**Scope:**
1. Extend `ThemeColors` struct with new fields
2. Update `interpolate_colors()` for new fields
3. Add Night theme definition
4. Update Dark theme colors to final spec
5. Update Light theme colors to final spec
6. Add `bg_canvas` to renderer (separate plot background from window background)
7. Add `grid_major` / `grid_minor` to renderer grid drawing
8. Add `theme_version` counter for GPU-safe propagation
9. Add theme name to workspace serialization
10. Add new design tokens to `design_tokens.hpp`

**Files modified:**
- [src/ui/theme/theme.hpp](src/ui/theme/theme.hpp)
- [src/ui/theme/theme.cpp](src/ui/theme/theme.cpp)
- [src/ui/theme/design_tokens.hpp](src/ui/theme/design_tokens.hpp)
- [src/render/renderer.cpp](src/render/renderer.cpp) (grid/axis color reads)
- [src/ui/app/session_runtime.cpp](src/ui/app/session_runtime.cpp) (workspace theme persistence)

**Risks:**
- Adding fields to `ThemeColors` requires updating `interpolate_colors()` which individually lerps each field — fragile if a field is missed.
  - *Mitigation:* Static assertion or compile-time check that `sizeof(ThemeColors)` matches expected size after modification.
- `grid_line` backward compatibility — existing workspaces that reference `grid_line` must still work.
  - *Mitigation:* Keep `grid_line` as an alias; `grid_major` is the new primary. Deserialize `grid_line` into `grid_major` if `grid_major` is absent.

**Dependencies:**
- None — this phase has no external dependencies.

**Definition of Done:**
- [ ] Night / Dark / Light themes render correctly with all new color fields
- [ ] Theme switching transitions smoothly with no flicker
- [ ] Grid shows major/minor lines with different alpha levels
- [ ] Plot canvas uses `bg_canvas` (distinct from window background)
- [ ] Workspace file persists and restores theme selection
- [ ] `ctest --test-dir build -LE gpu` passes
- [ ] No per-frame allocations introduced (ASan clean)

**Performance Validation Plan:**
1. Run `bench_render_throughput` before and after changes — must not regress >2%
2. Profile `apply_to_imgui()` — must complete in <0.1ms
3. Profile `interpolate_colors()` — must complete in <0.01ms

---

### Phase 2 — Plot Surface + Inspector Modernization (Est. 3 weeks)

**Scope:**
1. Implement two-tier grid rendering (major + minor lines with zoom-dependent alpha)
2. Redesign tooltip appearance (glass background, arrow pointer, connection line)
3. Redesign legend panel mode (glass panel, compact series list)
4. Inspector section header redesign (32px, ALL CAPS, disclosure chevron animation)
5. Inspector label-value layout (80px fixed label column)
6. Slider visual redesign (4px track, 14px thumb, accent fill)
7. Color picker popover (hue ring, S/L square, hex input, palette swatches)
8. Focus ring system (keyboard-only visibility)
9. Nav rail active tool indicator (accent bar + glow for Night)
10. Tab bar redesign (36px, 2px bottom accent indicator)

**Files modified:**
- [src/render/renderer.cpp](src/render/renderer.cpp) (grid rendering)
- [src/ui/overlay/tooltip.hpp](src/ui/overlay/tooltip.hpp) / `.cpp`
- [src/ui/overlay/legend_interaction.cpp](src/ui/overlay/legend_interaction.cpp)
- [src/ui/overlay/inspector.cpp](src/ui/overlay/inspector.cpp)
- [src/ui/widgets/](src/ui/widgets/) (slider, color picker, section header)
- [src/ui/imgui/imgui_integration.cpp](src/ui/imgui/imgui_integration.cpp) (nav rail, tab bar)
- [src/ui/figures/tab_bar.cpp](src/ui/figures/tab_bar.cpp)

**Risks:**
- Inspector layout changes may break existing panel width assumptions.
  - *Mitigation:* Test at INSPECTOR_WIDTH_MIN (240px) and INSPECTOR_WIDTH_MAX (480px).
- Color picker popover positioning near screen edges.
  - *Mitigation:* Clamp popover position to viewport bounds.
- Two-tier grid may cause visual density issues at extreme zoom levels.
  - *Mitigation:* Minor grid fades out below a density threshold (< 50px between lines).

**Dependencies:**
- Phase 1 complete (theme tokens and color fields must exist)

**Definition of Done:**
- [ ] Grid shows distinct major/minor lines in all three themes
- [ ] Minor grid fades in/out proportional to zoom level
- [ ] Tooltip renders with glass background and connection line
- [ ] Legend in "panel" mode renders with glass background
- [ ] Inspector sections use new header style with smooth expand/collapse
- [ ] All inputs use 80px label column alignment
- [ ] Slider has 4px track with accent fill
- [ ] Color picker popover functional with hue ring + hex input
- [ ] Focus ring visible only on keyboard navigation
- [ ] Tab bar has 2px accent indicator on active tab
- [ ] `ctest --test-dir build -LE gpu` passes
- [ ] Golden tests updated with new baselines

**Performance Validation Plan:**
1. Profile grid rendering with major+minor — must not exceed 0.3ms additional vs. current
2. Profile inspector draw — must stay within 2ms UI budget
3. Measure tooltip rendering — must complete in <0.1ms
4. Test at 1M datapoints with tooltip active — must maintain 60 FPS

---

### Phase 3 — Interaction Polish + Glow / Accent Refinement (Est. 2 weeks)

**Scope:**
1. Hover transition system (80ms ease-out, interruptible)
2. Night theme glow effects (additive overdraw for active elements)
3. Crosshair glow (two-pass rendering for Night theme)
4. Panel open/close timing refinement (close faster than open)
5. Plot zoom smoothing (exponential decay)
6. Tooltip entrance/exit hysteresis
7. Section expand/collapse animation polish
8. ROI visual styling (dashed border, handles, glow)
9. Annotation appearance (glass, connection line, fade on idle)
10. Theme picker in Settings menu (preview thumbnails)

**Files modified:**
- [src/ui/animation/transition_engine.cpp](src/ui/animation/transition_engine.cpp)
- [src/ui/imgui/imgui_integration.cpp](src/ui/imgui/imgui_integration.cpp) (hover states)
- [src/render/renderer.cpp](src/render/renderer.cpp) (crosshair glow, ROI rendering)
- [src/ui/overlay/tooltip.hpp](src/ui/overlay/tooltip.hpp) / `.cpp` (hysteresis)
- [src/ui/overlay/inspector.cpp](src/ui/overlay/inspector.cpp) (timing polish)
- [src/ui/layout/layout_manager.hpp](src/ui/layout/layout_manager.hpp) (panel timing)
- [src/ui/input/input_handler.cpp](src/ui/input/input_handler.cpp) (zoom smoothing)

**Risks:**
- Glow additive blending may interact badly with semi-transparent panels.
  - *Mitigation:* Render glow elements after panel backgrounds but before panel content.
- Zoom smoothing may introduce visual latency perception.
  - *Mitigation:* Smoothing factor is tunable; default 12.0 gives ~120ms convergence.

**Dependencies:**
- Phase 2 complete (requires base rendering infrastructure)

**Definition of Done:**
- [ ] All hover states have smooth 80ms transitions
- [ ] Night theme shows soft glow on active tools, sliders, and crosshair
- [ ] Glow uses additive overdraw (no bloom shader)
- [ ] Panel close animation is 20% faster than open
- [ ] Zoom transitions use exponential decay smoothing
- [ ] Tooltip has 100ms hysteresis before hiding
- [ ] ROI shows dashed border with drag handles
- [ ] Annotations fade to 70% when not hovered
- [ ] Theme picker shows small preview thumbnails
- [ ] `ctest --test-dir build -LE gpu` passes
- [ ] Frame time < 16.67ms at 1M points with all effects active
- [ ] No per-frame allocations (ASan clean)

**Performance Validation Plan:**
1. Profile glow draw calls — must add <0.1ms total
2. Measure hover transition CPU cost — must be <0.01ms per widget
3. Full frame time test: 1920×1080, 4 subplots, 250K points each, Night theme with all glow effects
4. Compare frame time before/after Phase 3 — must not regress >5%

---

## 10. GitHub Issue Drafts

### Issue 1: Theme Engine — Night Theme + Extended Color Tokens

**Title:** `feat: Add Night theme and extend ThemeColors with canvas, grid, glow tokens`

**Description:**
Extend the theming system with a new "Night" theme inspired by our Vision reference. This requires adding new color fields to `ThemeColors` (bg_canvas, grid_major, grid_minor, accent_glow, glow_intensity, focus_ring, etc.) and defining three refined themes: Night (deep dark with cyan glow), Dark (productivity-focused), Light (scientific publication).

**Acceptance Criteria:**
- [ ] `ThemeColors` has all new fields: `bg_canvas`, `grid_major`, `grid_minor`, `accent_glow`, `glow_intensity`, `focus_ring`, `scrollbar_thumb`, `scrollbar_track`, `section_header_bg`, `input_bg`, `hover_highlight`, `annotation_bg`, `roi_fill`, `roi_border`
- [ ] Night theme registered with Vision-inspired colors
- [ ] Dark theme preserved with minor refinements
- [ ] Light theme refined for publication readability
- [ ] `interpolate_colors()` handles all new fields
- [ ] Theme transition between Night/Dark/Light is smooth
- [ ] Workspace serialization includes `theme` field
- [ ] JSON export/import includes new fields with backward compatibility

**Screenshots Checklist:**
- [ ] Night theme: full window with plot, inspector, nav rail
- [ ] Dark theme: same view (compare contrast, grid visibility)
- [ ] Light theme: same view (verify readability in bright environment)
- [ ] Theme transition: 3-frame sequence showing mid-transition
- [ ] Night vs Dark comparison: side-by-side crop of plot area

**Performance Check Steps:**
1. `bench_render_throughput` — must not regress >2%
2. Profile `apply_to_imgui()` — confirm <0.1ms
3. Profile `interpolate_colors()` — confirm <0.01ms
4. Memory: no new per-frame allocations

---

### Issue 2: Plot Style Overhaul — Two-Tier Grid + Canvas Separation

**Title:** `feat: Two-tier gridlines (major/minor) and separate canvas background`

**Description:**
Implement a two-layer grid system with distinct major and minor gridline alpha. Separate the plot canvas background (`bg_canvas`) from the window background (`bg_primary`). Minor gridlines should fade based on zoom level.

**Acceptance Criteria:**
- [ ] Major gridlines render at tick label positions with `grid_major` color
- [ ] Minor gridlines render at subdivisions with `grid_minor` color
- [ ] Minor gridlines fade out when line spacing < 50px
- [ ] Minor gridlines fade in when line spacing > 50px
- [ ] Plot area uses `bg_canvas` (not `bg_primary`)
- [ ] `bg_canvas` is visually distinct from panel backgrounds
- [ ] Grid alpha values are theme-driven (no hardcoded alphas in renderer)

**Screenshots Checklist:**
- [ ] Night theme: plot showing major/minor grid distinction
- [ ] Zoom sequence: 3 screenshots showing minor grid fade behavior
- [ ] Night theme: bg_canvas vs bg_primary contrast visible at panel edges
- [ ] Light theme: grid visibility on white background

**Performance Check Steps:**
1. Profile grid rendering with 100 major + 500 minor lines — must add <0.3ms
2. `bench_render_throughput` — must not regress >3%
3. Compare draw call count before/after — document delta

---

### Issue 3: Inspector Redesign — Section Headers + Label Alignment + Slider Style

**Title:** `feat: Inspector panel visual modernization (headers, alignment, sliders)`

**Description:**
Redesign inspector panel layout for premium scientific tool appearance: consistent 32px section headers with ALL CAPS, 80px label column alignment, 4px track sliders with accent fill, and smooth section expand/collapse animations.

**Acceptance Criteria:**
- [ ] Section headers are 32px height with `section_header_bg`
- [ ] Header text is ALL CAPS, FONT_XS, letter-spacing +0.05em
- [ ] Disclosure chevron rotates with 100ms animation
- [ ] All label-value rows use 80px fixed label column
- [ ] Slider track is 4px with `accent` fill for active portion
- [ ] Slider thumb is 14px with 2px contrast border
- [ ] Section expand/collapse animation is 150ms
- [ ] Layout works correctly at 240px and 480px inspector widths

**Screenshots Checklist:**
- [ ] Inspector at 320px with 3+ sections expanded (Night theme)
- [ ] Inspector at 240px minimum width (verify no clipping)
- [ ] Slider thumb detail (close-up, 2x zoom)
- [ ] Section header with disclosure chevron (open and closed states)
- [ ] Keyboard focus ring on slider input

**Performance Check Steps:**
1. Profile inspector draw at 320px width — must stay <2ms
2. Profile section expand animation — must add <0.05ms per frame
3. No layout recalculation thrashing (measure consistency over 60 frames)

---

### Issue 4: Toolbar Visual Update — Active State + Icon Brightness + Rail Grouping

**Title:** `feat: Nav rail visual update with active indicators and icon brightness system`

**Description:**
Update the left navigation rail with grouped tool sections, dynamic icon brightness system, and Night-theme-specific accent bar + glow on the active tool.

**Acceptance Criteria:**
- [ ] Tools grouped with separator hairlines between groups
- [ ] Default icon opacity: 55% of `text_primary`
- [ ] Hover icon opacity: 85% of `text_primary`
- [ ] Active tool: icon in `accent` color with `accent_subtle` background
- [ ] Night theme: 2px left accent bar on active tool
- [ ] Night theme: `accent_glow` bloom on active tool (additive blend)
- [ ] Touch targets are 32×32px minimum (even for 20px icons)

**Screenshots Checklist:**
- [ ] Navy rail with all tool groups visible (Night theme)
- [ ] Active tool highlight comparison: Night vs Dark vs Light
- [ ] Hover state transition (screenshot mid-hover if possible)
- [ ] Expanded rail (200px) with text labels

**Performance Check Steps:**
1. Profile nav rail draw — must stay <0.3ms
2. Night theme glow: confirm additive draw adds <0.05ms
3. No additional GPU pipeline creation for glow

---

### Issue 5: Tooltip + Legend Modernization — Glass Style + Connection Lines

**Title:** `feat: Glass-style tooltips with connection lines and panel legend mode`

**Description:**
Redesign data tooltips with glass background, triangular pointer, and data-point connection line. Implement panel legend mode with glass background and compact series list.

**Acceptance Criteria:**
- [ ] Tooltip background uses `tooltip_bg` (near-opaque glass)
- [ ] Tooltip has 8px rounded corners
- [ ] Tooltip shows arrow/pointer toward data point
- [ ] Thin dashed connection line from tooltip to data point
- [ ] Night theme: tooltip has subtle `accent_glow` outer glow
- [ ] Tooltip entrance: 50ms fade-in after snap radius entry
- [ ] Tooltip exit: 100ms fade-out with 100ms hysteresis
- [ ] Values displayed in mono font for alignment
- [ ] Legend panel mode: glass background with series swatches
- [ ] Legend max 8 visible rows, scrollable beyond

**Screenshots Checklist:**
- [ ] Tooltip on data point (Night theme, showing pointer + connection line)
- [ ] Tooltip on data point (Light theme)
- [ ] Legend panel with 5 series (Night theme)
- [ ] Legend panel with 10 series (scrollable state)
- [ ] Tooltip entrance/exit timing GIF or frame sequence

**Performance Check Steps:**
1. Profile tooltip render — must complete <0.1ms
2. Legend panel with 20 series — must render <0.2ms
3. Test with 1M points + tooltip active — must maintain 60 FPS
4. No allocations in tooltip render path

---

### Issue 6: Motion Polish — Hover Transitions + Zoom Smoothing + Panel Timing

**Title:** `feat: Interaction polish — hover transitions, zoom smoothing, panel timing`

**Description:**
Implement smooth, interruptible hover transitions (80ms), exponential-decay zoom smoothing (120ms), refined panel open/close timing (close faster than open), and section expand/collapse animation polish.

**Acceptance Criteria:**
- [ ] Hover state transitions use 80ms ease-out (interruptible)
- [ ] Hover-out transitions use 120ms ease-in (interruptible)
- [ ] Scroll-wheel zoom uses exponential decay smoothing (factor 12.0)
- [ ] Box zoom uses 200ms ease-out transition
- [ ] Double-click reset uses 250ms ease-in-out
- [ ] Inspector open: 150ms, close: 120ms
- [ ] Nav rail expand: 150ms, close: 120ms
- [ ] Section expand: 150ms, collapse: 100ms
- [ ] All transitions are interruptible (no stuck states)
- [ ] Timeline scrub: 0ms (direct manipulation)

**Screenshots Checklist:**
- [ ] Zoom transition sequence (3 frames: start, mid, end)
- [ ] Panel close vs open timing comparison
- [ ] Section expand/collapse sequence
- [ ] Hover state on nav rail tool (frame sequence)

**Performance Check Steps:**
1. Profile all hover transitions — total budget <0.01ms per widget
2. Profile zoom smoothing — must add <0.01ms per frame
3. Measure total animation cost per frame with 10 active transitions — must stay <0.5ms
4. No frame drops during rapid zoom in/out (stress test)

---

### Issue 7: Light Theme Readability Tuning

**Title:** `fix: Light theme readability — contrast ratios, grid visibility, axis labels`

**Description:**
Fine-tune the Light theme for maximum readability in bright environments and scientific publication output. Verify WCAG AA contrast ratios for all text, tune grid alpha for paper-like appearance, and ensure data series maintain sufficient contrast on white canvas.

**Acceptance Criteria:**
- [ ] All text meets WCAG AA contrast ratio (≥4.5:1 for normal text, ≥3:1 for large text)
- [ ] Grid lines visible but calm on white canvas (12% black for major, 6% for minor)
- [ ] Axis spines visible at 60% opacity
- [ ] All 10 Tableau palette colors maintain ≥3:1 contrast against `bg_canvas`
- [ ] Tooltip readable with strong border contrast
- [ ] Inspector text readable with proper `text_primary`/`text_secondary` contrast
- [ ] Focus ring visible against light backgrounds
- [ ] Data series can be distinguished at 2px line width

**Screenshots Checklist:**
- [ ] Full window Light theme with 4 series
- [ ] Light theme plot area close-up (grid, axes, labels, data)
- [ ] Inspector panel readability (Light theme)
- [ ] Tooltip on Light theme
- [ ] Contrast ratio measurements (automated or manual table)

**Performance Check Steps:**
1. Profile Light theme rendering — must match Dark theme ±5%
2. Verify no additional GPU work for Light theme
3. Test headless PNG export in Light theme — verify consistent rendering

---

*End of Visual System Redesign Specification*
