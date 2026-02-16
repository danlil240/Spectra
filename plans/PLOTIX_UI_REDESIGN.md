# Spectra UI Redesign — 2026 Next-Generation Visualization Platform
 
**Document Version:** 1.0  
**Date:** 2026-02-13  
**Status:** Architectural Proposal  
 
---
 
## Table of Contents
 
1. [UI Audit](#1-ui-audit)
2. [2026 UX Philosophy](#2-2026-ux-philosophy)
3. [Layout Architecture](#3-layout-architecture)
4. [Interaction Model](#4-interaction-model)
5. [Visual System](#5-visual-system)
6. [Differentiator Features](#6-differentiator-features)
7. [Multi-Agent Execution Plan](#7-multi-agent-execution-plan)
8. [90-Day Phased Roadmap](#8-90-day-phased-roadmap)
9. [Technical Risks & Mitigation](#9-technical-risks--mitigation)
10. [Roadmap Update Protocol (MANDATORY)](#10-roadmap-update-protocol-mandatory)
11. [Build Coordination Protocol (MANDATORY)](#11-build-coordination-protocol-mandatory)
 
---
 
## 1. UI Audit
 
### 1.1 Current Architecture Summary
 
The UI is built on **ImGui** (immediate mode) rendered as a Vulkan overlay on top of GPU-accelerated plot content. The window system uses **GLFW**. The layout is computed in `src/core/layout.cpp` with a simple grid-based subplot system.
 
**Current layout zones:**
- **Top menubar** (`draw_menubar`) — 52px fixed height, floating rounded window with brand, toolbar buttons (H/P/Z), File/View/Tools menus, and status readout
- **Left icon bar** (`draw_icon_bar`) — 56px wide, contains Fig/Ser/Ax section buttons and FPS counter
- **Expandable side panel** (`draw_panel`) — 300px wide, slides in with smoothstep animation, contains Figure/Series/Axes property editors
- **Main canvas** — remaining space, Vulkan-rendered plot content
- **No bottom bar, no right panel, no floating overlays, no command palette**
 
**Key files:**
- `src/ui/imgui_integration.cpp` (809 lines) — All UI drawing, styling, fonts
- `src/ui/imgui_integration.hpp` (89 lines) — UI class definition
- `src/ui/app.cpp` (646 lines) — Main loop, resize handling, ImGui frame management
- `src/ui/input.cpp` (316 lines) — Mouse/keyboard interaction, pan/zoom/box-zoom
- `src/ui/input.hpp` (137 lines) — Input handler with hit-testing
- `src/ui/glfw_adapter.cpp` (152 lines) — Window creation and event callbacks
- `src/core/layout.cpp` (44 lines) — Simple grid subplot layout
- `src/render/renderer.cpp` (653 lines) — GPU rendering pipeline
 
### 1.2 Strengths
 
- **GPU-accelerated core** — Vulkan backend with proper pipeline abstraction (`Backend` → `VulkanBackend`), SSBO-based series data, push constants for per-draw parameters. Solid foundation.
- **Clean separation** — Backend/Renderer/UI/Input are distinct modules. The `Backend` abstract class allows future Metal/WebGPU ports.
- **Smooth panel animation** — The side panel uses smoothstep easing with alpha fade. Shows intent for fluid UI.
- **Modern color palette** — The 2026 color constants show awareness of contemporary design (accent blues, subtle dividers, semantic colors for success/warning/error).
- **Multi-axes hit-testing** — `InputHandler` already supports clicking into different subplot regions. Good foundation for multi-panel interaction.
- **Font system** — Embedded Inter font at 5 size variants. Professional typeface choice.
- **Headless + windowed modes** — Architecture supports both interactive and batch rendering.
 
### 1.3 Weaknesses
 
- **Monolithic UI file** — All UI drawing logic (809 lines) lives in a single `imgui_integration.cpp`. Menubar, icon bar, panel sections, toolbar buttons, popup menus — all in one file with no component abstraction.
- **Hardcoded layout geometry** — Magic numbers everywhere: `kIconBarWidth = 56`, `kPanelWidth = 300`, `kMenubarHeight = 52`, `ImGui::SetCursorPosX(130.0f)`, `ImGui::SetCursorPosX(280.0f)`, `ImGui::SetCursorPosX(300.0f)`. No responsive layout system.
- **No right inspector panel** — Properties are crammed into the left panel. No contextual editing. No separation between navigation and inspection.
- **No bottom status bar** — Cursor coordinates, data readout, zoom level, and performance metrics have no dedicated home. FPS is buried in the icon bar and duplicated in the menubar.
- **ASCII icon placeholders** — Icon bar uses text labels ("Fig", "Ser", "Ax", "H", "P", "Z") instead of actual icons. Looks amateur.
- **No keyboard productivity** — No command palette, no customizable shortcuts beyond R/G/S/A. No mode switching UI. No search.
- **No hover tooltips on data** — The `CursorReadout` struct exists in `input.hpp` but is never rendered as a tooltip overlay. Data interaction is invisible.
- **No crosshair mode** — Despite having cursor tracking, there's no visual crosshair rendered on the plot canvas.
- **Light-only theme** — All colors are light-mode. No dark theme. No theme switching. The `kBgSidebar` is `{0.97, 0.98, 1.00}` — pure light.
- **No docking/splitting** — Single figure fills the canvas. No multi-figure tabs, no split views, no dockable panels.
- **Resize instability** — The `app.cpp` main loop has extensive resize loop detection, throttling, and hard limits (`max_total_recreations = 20`). This is a symptom of architectural fragility, not a feature.
- **No undo/redo** — Property changes in the panel are immediate with no history.
- **No data transformation UI** — No smoothing, FFT, normalization, or filtering controls.
- **No legend interaction** — Legend is rendered by the GPU renderer but not interactive (no drag, no click-to-toggle).
- **Toolbar mode state is fragile** — `InteractionMode` is shared between `ImGuiIntegration` and `InputHandler` via getter/setter, creating coupling.
 
### 1.4 Structural Limitations
 
- **ImGui immediate mode ceiling** — ImGui is excellent for debug UIs but fights against retained-state patterns needed for docking, drag-and-drop, rich tooltips, and complex layout. The current code already pushes/pops 5+ style vars per widget.
- **Single-figure assumption** — `App::run()` drives `figures_[0]` only. Multi-figure support is declared but not implemented in the UI.
- **Layout is not UI-aware** — `compute_subplot_layout()` doesn't account for UI chrome. The menubar offset is hacked in `app.cpp` with manual viewport adjustment.
- **No component model** — Every UI element is a raw ImGui draw call. No reusable widget abstractions (e.g., `PropertyRow`, `ColorField`, `SliderField`, `SectionPanel`).
- **No event bus** — UI state changes (mode switch, reset view, panel toggle) are communicated via bool flags on `ImGuiIntegration`. This doesn't scale.
 
### 1.5 UX Bottlenecks
 
- **Discoverability is near zero** — New users see "Fig", "Ser", "Ax" buttons with no explanation. Toolbar buttons "H", "P", "Z" require prior knowledge.
- **No onboarding** — No welcome state, no empty-state guidance, no contextual hints.
- **Panel content is flat** — All properties are listed vertically with no grouping hierarchy, no collapsible sections, no search/filter.
- **No visual feedback for interactions** — Pan and zoom have no animated transitions. Box zoom has no visual rectangle overlay rendered on canvas.
- **Export is TODO** — All export menu items are stubs (`/* TODO */`).
 
---
 
## 2. 2026 UX Philosophy
 
### Core Design Principles
 
Every UI decision in Spectra must pass through these 7 principles:
 
#### P1 — Canvas Supremacy
The plot canvas is the product. Every pixel of UI chrome must justify its existence. Default state: maximum canvas, minimal chrome. Panels appear on demand, recede when not needed. The canvas must never feel cramped.
 
#### P2 — Command-First, Menu-Second
Power users reach for keyboards. Every action must be accessible via a command palette (Ctrl+K). Menus exist for discoverability, not as the primary interaction path. Shortcuts are first-class citizens, displayed inline on every menu item and tooltip.
 
#### P3 — Contextual Intelligence
UI elements appear where and when they're needed. Right-click a series → context menu with that series' properties. Hover a data point → rich tooltip. Select a region → floating mini-toolbar. No modal dialogs. No settings pages. Context is king.
 
#### P4 — Fluid Motion
Every state change is animated. Panel slides, zoom transitions, tooltip fades, selection highlights — all use GPU-accelerated easing curves. 60fps minimum. No jarring jumps. The UI must feel like it has physics.
 
#### P5 — Dark-First, Theme-Adaptive
Dark mode is the default for professional visualization work. Light mode is a supported alternative. High-contrast mode for accessibility. All themes use the same design token system. Theme switching is instant (no restart).
 
#### P6 — Progressive Disclosure
Beginners see a clean, simple interface. Power users unlock density. The UI adapts: compact mode for experts, spacious mode for newcomers. Advanced features are one click deeper, never hidden, never cluttering the default view.
 
#### P7 — Extensible Architecture
Every UI component is a module. Panels are plugins. Themes are data files. Shortcuts are configurable. The UI system must support third-party extensions without modifying core code.
 
---
 
## 3. Layout Architecture
 
### 3.1 Zone Map
 
```
┌─────────────────────────────────────────────────────────────────┐
│  COMMAND BAR (40px)  [≡] Spectra  [search...]  [⌘K]  [◑] [─][□][×] │
├────┬────────────────────────────────────────────────────┬───────┤
│ N  │                                                    │  I    │
│ A  │                                                    │  N    │
│ V  │              MAIN CANVAS                           │  S    │
│    │           (plot viewport)                           │  P    │
│ R  │                                                    │  E    │
│ A  │        ┌──────────────────┐                        │  C    │
│ I  │        │  Floating Tools  │                        │  T    │
│ L  │        │  [🔍][✋][📐][📏] │                        │  O    │
│    │        └──────────────────┘                        │  R    │
│ 48 │                                                    │       │
│ px │              [Hover Tooltip]                       │  320  │
│    │                                                    │  px   │
├────┴────────────────────────────────────────────────────┴───────┤
│  STATUS BAR (28px)  X: 3.142  Y: 0.001  │ Zoom: 100%  │ 60fps │
└─────────────────────────────────────────────────────────────────┘
```
 
### 3.2 Zone Definitions
 
#### Command Bar (Top, 40px)
- **Left:** Hamburger menu (≡) → app-level actions. Brand mark "Spectra" (clickable → home/reset).
- **Center:** Global search / command palette trigger. Typing activates fuzzy search across: series names, axis labels, commands, settings, export options.
- **Right:** Theme toggle (◑), window controls (minimize/maximize/close on Linux/Windows; hidden on macOS).
- **Style:** Translucent background with subtle blur. No border. Blends with canvas.
 
#### Navigation Rail (Left, 48px)
- **Purpose:** Switch between workspace views. Replaces the current icon bar.
- **Icons (top-aligned):**
  - 📊 **Figures** — Figure list, multi-figure tabs
  - 📈 **Series** — Series browser with visibility toggles
  - 🎯 **Axes** — Axis configuration
  - 🔧 **Tools** — Data transforms, filters, analysis
  - 📁 **Data** — Data source browser (future)
- **Icons (bottom-aligned):**
  - ⚙️ **Settings** — Preferences, themes, shortcuts
  - ❓ **Help** — Docs, shortcuts cheatsheet
- **Behavior:** Click opens corresponding panel. Click again closes. Active icon has accent pill indicator.
- **Style:** Minimal, icon-only. Tooltip on hover shows label + shortcut.
 
#### Inspector Panel (Right, 320px, collapsible)
- **Purpose:** Contextual property editor. Shows properties of whatever is selected.
- **Sections (collapsible with smooth animation):**
  - **Selection context header** — "Line Series: sin(x)" with color swatch
  - **Appearance** — Color, width/size, opacity, style (solid/dashed/dotted)
  - **Data** — Point count, range, statistics (min/max/mean/std)
  - **Transform** — Smoothing, offset, scale, FFT (expandable)
  - **Axis Binding** — Which axes this series belongs to
- **Behavior:** Updates contextually. Click a series → shows series props. Click axes → shows axis props. Click canvas background → shows figure props.
- **Style:** Clean card-based sections with subtle elevation. Smooth expand/collapse.
 
#### Main Canvas (Center, fills remaining space)
- **Purpose:** The plot viewport. Maximum area.
- **Features:**
  - Renders all axes/series via Vulkan
  - Supports split view (horizontal/vertical) for multi-figure comparison
  - Tab bar at top when multiple figures are open
  - Minimap in corner (optional, for large datasets)
- **Behavior:** All mouse interactions (pan, zoom, select) happen here. UI chrome never overlaps active plot area.
 
#### Floating Tool Overlay (Bottom-center of canvas, auto-hide)
- **Purpose:** Quick-access interaction tools without leaving the canvas.
- **Tools:**
  - 🔍 **Zoom** (scroll zoom, box zoom)
  - ✋ **Pan** (drag to pan)
  - 📐 **Measure** (distance/angle between points)
  - 📏 **Crosshair** (toggle crosshair cursor)
  - 📌 **Pin** (pin data markers)
  - 🏷️ **Annotate** (add text annotations)
- **Behavior:** Appears on canvas hover (bottom edge), fades after 2s of inactivity. Always accessible via keyboard shortcuts.
- **Style:** Pill-shaped, frosted glass background, subtle shadow. Compact.
 
#### Status Bar (Bottom, 28px)
- **Left:** Cursor data readout — `X: 3.142  Y: 0.001` (updates in real-time)
- **Center:** Context info — selected series name, point count, zoom level
- **Right:** Performance — `60 fps` | `GPU: 2.1ms` | render mode indicator
- **Style:** Minimal text, monospace for numbers. Subtle top border.
 
### 3.3 Resizable Docking System
 
**Phase 2 feature.** Built on a tree-based dock layout:
 
```
DockNode (root)
├── DockNode (left rail) — fixed 48px
├── DockNode (center)
│   ├── DockNode (canvas) — fills
│   └── DockNode (bottom panel) — optional, for data table / console
└── DockNode (right inspector) — 320px, resizable
```
 
- Panels can be dragged to dock positions (left, right, bottom, floating).
- Split canvas horizontally or vertically for multi-figure comparison.
- Dock state is serializable (workspace save/load).
- ImGui's docking branch provides the foundation; we wrap it with our own `DockManager` for state persistence and custom rendering.
 
### 3.4 Multi-Figure Support
 
- **Tab bar** at top of canvas area when `figures_.size() > 1`.
- Each tab shows figure title (editable) and close button.
- Drag tabs to reorder. Drag tab out to create split view.
- `Ctrl+T` creates new figure. `Ctrl+W` closes current.
- Figure list in Navigation Rail shows all figures with thumbnails.
 
### 3.5 Multi-Viewport (Split View)
 
- Right-click tab → "Split Right" / "Split Down".
- Each split pane is an independent viewport with its own axes.
- Linked cursors across split panes (shared crosshair X position).
- Resizable splitter with drag handle.
 
---
 
## 4. Interaction Model
 
### 4.1 Zoom & Pan
 
| Action | Behavior | Animation |
|--------|----------|-----------|
| Scroll wheel | Zoom toward cursor, 10% per tick | Smooth 150ms ease-out |
| Pinch (trackpad) | Continuous zoom toward centroid | Real-time, no animation needed |
| Left-drag | Pan (translate view) | Inertial: release continues with deceleration over 300ms |
| Right-drag | Box zoom selection | Animated rectangle overlay with dimension labels |
| Double-click | Auto-fit to data bounds | Animated 300ms ease-in-out transition |
| `Home` key | Reset to original view | Animated 300ms transition |
| `Ctrl+0` | Zoom to 100% (1:1 pixel mapping) | Animated 200ms transition |
 
**Inertial pan implementation:**
```
on_mouse_release:
    velocity = (current_pos - prev_pos) / dt
    start inertial animation:
        each frame: position += velocity * dt
        velocity *= 0.92  // friction
        stop when |velocity| < threshold
```
 
**Animated zoom implementation:**
```
on_zoom_trigger:
    target_xlim = compute_new_limits()
    start animation(current_xlim → target_xlim, 150ms, ease_out)
    each frame: xlim = lerp(start, target, eased_t)
```
 
### 4.2 Data Interaction
 
#### Rich Hover Tooltips
- Trigger: Mouse within 8px of a data point (nearest-point snapping).
- Content: Series name, X value, Y value, formatted with axis units.
- Style: Floating card with subtle shadow, accent-colored left border matching series color. Appears with 50ms fade-in.
- Position: Above cursor, flips below if near top edge. Never obscures the hovered point.
 
#### Crosshair Mode
- Toggle: `C` key or toolbar button.
- Renders: Vertical + horizontal dashed lines through cursor position, clipped to plot area.
- Labels: X/Y values displayed at axis intersections.
- Multi-axes: Crosshair spans all subplots vertically (shared X cursor).
 
#### Data Markers (Pin)
- Click a data point while in Pin mode → persistent marker appears.
- Marker shows: colored dot + label with coordinates.
- Drag marker to reposition label. Right-click to delete.
- Markers survive zoom/pan (anchored to data coordinates).
 
#### Region Selection
- `Shift+drag` selects a rectangular region.
- On release: floating mini-toolbar appears with options:
  - **Zoom to selection**
  - **Export selection** (CSV of points in region)
  - **Statistics** (min, max, mean, std of selected points)
  - **Copy coordinates**
 
#### Legend Interaction
- Click legend entry → toggle series visibility (with fade animation).
- Double-click legend entry → solo that series (hide all others).
- Drag legend → reposition anywhere in plot area.
- Right-click legend → context menu (edit label, change color, remove).
 
#### Inline Label Editing
- Double-click axis title → inline text editor appears.
- Double-click axis label → edit.
- Double-click legend entry label → edit.
- `Enter` confirms, `Escape` cancels. Smooth transition.
 
### 4.3 Keyboard UX
 
#### Command Palette (`Ctrl+K`)
- Fuzzy search across all commands, settings, series, figures.
- Categories: Commands, Series, Axes, Settings, Export, Help.
- Recent commands shown by default.
- Each result shows: icon, label, shortcut (if any), category badge.
- Arrow keys navigate, Enter executes, Escape closes.
- Implementation: `CommandRegistry` singleton with `register_command(id, label, callback, shortcut, category)`.
 
#### Default Shortcuts
 
| Shortcut | Action |
|----------|--------|
| `Ctrl+K` | Command palette |
| `R` | Reset view (auto-fit all) |
| `A` | Auto-fit active axes |
| `G` | Toggle grid |
| `C` | Toggle crosshair |
| `B` | Toggle border |
| `L` | Toggle legend |
| `1-9` | Switch to figure tab N |
| `Ctrl+T` | New figure |
| `Ctrl+W` | Close figure |
| `Ctrl+S` | Export PNG |
| `Ctrl+Shift+S` | Export SVG |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Space` | Toggle play/pause (animation) |
| `[` / `]` | Step animation frame back/forward |
| `F` | Toggle fullscreen canvas (hide all panels) |
| `Escape` | Cancel current action / close panel |
| `Tab` | Cycle through series selection |
 
#### Mode System
Three modes, shown in status bar:
- **Navigate** (default) — Pan, zoom, hover tooltips
- **Inspect** — Crosshair, data readout, click-to-pin
- **Annotate** — Add text labels, arrows, shapes
 
Switch via toolbar buttons or `1`/`2`/`3` keys.
 
### 4.4 Visual Intelligence
 
- **Smart snap** — When hovering near a data point, cursor snaps to nearest point with visual indicator (enlarged dot + coordinates).
- **Auto axis formatting** — Detect data magnitude and choose appropriate tick format (scientific notation for large/small, SI prefixes, time formatting for timestamps).
- **Smart legend placement** — Algorithm finds the plot quadrant with least data density and places legend there.
- **Auto contrast** — When series colors are too similar, suggest alternative colors. When background changes, adjust grid/text colors automatically.
 
---
 
## 5. Visual System
 
### 5.1 Design Tokens
 
#### Spacing Scale (base: 4px)
```
--space-0:   0px
--space-1:   4px    (tight)
--space-2:   8px    (compact)
--space-3:  12px    (default)
--space-4:  16px    (comfortable)
--space-5:  20px    (spacious)
--space-6:  24px    (section gap)
--space-8:  32px    (panel padding)
--space-10: 40px    (zone gap)
```
 
#### Radius Scale
```
--radius-sm:   4px   (buttons, inputs)
--radius-md:   8px   (cards, panels)
--radius-lg:  12px   (modals, floating panels)
--radius-xl:  16px   (main panels)
--radius-pill: 999px (pill buttons, badges)
```
 
#### Font Scale (Inter)
```
--font-xs:    11px   (status bar, badges)
--font-sm:    12px   (secondary text, labels)
--font-base:  14px   (body text, controls)
--font-md:    15px   (menu items)
--font-lg:    16px   (section headers)
--font-xl:    18px   (panel titles)
--font-2xl:   20px   (page titles)
--font-mono:  13px   (data readout, coordinates — JetBrains Mono or similar)
```
 
#### Font Weights
```
--weight-regular:  400  (body text)
--weight-medium:   500  (labels, buttons)
--weight-semibold: 600  (headers, emphasis)
--weight-bold:     700  (titles, brand)
```
 
### 5.2 Theme Engine
 
#### Architecture
```cpp
struct ThemeColors {
    // Surfaces
    Color bg_primary;        // Main canvas background
    Color bg_secondary;      // Panel backgrounds
    Color bg_tertiary;       // Card/section backgrounds
    Color bg_elevated;       // Floating elements (tooltips, popups)
    Color bg_overlay;        // Modal overlay (semi-transparent)
 
    // Text
    Color text_primary;      // Main text
    Color text_secondary;    // Labels, descriptions
    Color text_tertiary;     // Placeholders, disabled
    Color text_inverse;      // Text on accent backgrounds
 
    // Borders
    Color border_default;    // Standard borders
    Color border_subtle;     // Subtle dividers
    Color border_strong;     // Focused elements
 
    // Interactive
    Color accent;            // Primary accent (buttons, links, active states)
    Color accent_hover;      // Accent hover state
    Color accent_muted;      // Accent backgrounds (selected items)
    Color accent_subtle;     // Very subtle accent tint
 
    // Semantic
    Color success;
    Color warning;
    Color error;
    Color info;
 
    // Plot-specific
    Color grid_line;
    Color axis_line;
    Color tick_label;
    Color crosshair;
    Color selection_fill;
    Color selection_border;
    Color tooltip_bg;
    Color tooltip_border;
};
 
struct Theme {
    std::string name;
    ThemeColors colors;
    float       opacity_panel;       // Panel background opacity (for blur effect)
    float       opacity_tooltip;     // Tooltip background opacity
    float       shadow_intensity;    // Shadow alpha multiplier
    float       border_width;        // Default border width
    bool        use_blur;            // Enable backdrop blur effects
};
```
 
#### Dark Theme (Default)
```
bg_primary:     #0D1117   (GitHub dark-style deep navy)
bg_secondary:   #161B22   (Panel background)
bg_tertiary:    #1C2128   (Card background)
bg_elevated:    #2D333B   (Floating elements)
text_primary:   #E6EDF3   (High contrast white)
text_secondary: #8B949E   (Muted gray)
accent:         #58A6FF   (Bright blue)
border_default: #30363D   (Subtle border)
grid_line:      #21262D   (Very subtle grid)
```
 
#### Light Theme
```
bg_primary:     #FFFFFF
bg_secondary:   #F6F8FA
bg_tertiary:    #F0F2F5
bg_elevated:    #FFFFFF
text_primary:   #1F2328
text_secondary: #656D76
accent:         #0969DA
border_default: #D0D7DE
grid_line:      #E8ECF0
```
 
#### High Contrast Theme
```
bg_primary:     #000000
text_primary:   #FFFFFF
accent:         #FFD700
border_default: #FFFFFF
```
 
#### Theme Switching
- Instant swap via `ThemeManager::set_theme(name)`.
- All ImGui colors updated in one pass.
- Plot renderer reads `ThemeColors` for grid/axis/text colors.
- Animated transition: 200ms cross-fade between old and new colors.
 
### 5.3 Depth System (Elevation)
 
| Level | Use | Shadow |
|-------|-----|--------|
| 0 | Canvas, status bar | None |
| 1 | Navigation rail, panels | `0 1px 3px rgba(0,0,0,0.12)` |
| 2 | Floating toolbar, tooltips | `0 4px 12px rgba(0,0,0,0.15)` |
| 3 | Command palette, modals | `0 8px 24px rgba(0,0,0,0.20)` |
| 4 | Drag previews | `0 12px 32px rgba(0,0,0,0.25)` |
 
Implementation: Custom ImGui draw commands that render shadow quads behind elevated elements.
 
### 5.4 Micro-Animations
 
| Element | Trigger | Animation |
|---------|---------|-----------|
| Panel open/close | Click nav icon | Slide + fade, 200ms ease-out |
| Tooltip appear | Hover data point | Fade-in 100ms, scale 0.95→1.0 |
| Tooltip disappear | Leave data point | Fade-out 150ms |
| Button hover | Mouse enter | Background color transition 100ms |
| Series toggle | Click legend | Opacity 1.0→0.0 over 200ms |
| Zoom transition | Scroll/double-click | Axis limits lerp 150ms ease-out |
| Command palette | Ctrl+K | Scale 0.98→1.0 + fade, 150ms |
| Tab switch | Click tab | Cross-fade canvas 100ms |
| Theme switch | Toggle | Color cross-fade 200ms |
| Selection rect | Drag | Real-time with dashed border animation |
 
All animations use the existing `ease` namespace functions. New additions needed:
- `ease::spring(t, damping, stiffness)` — For bouncy UI elements
- `ease::cubic_bezier(t, p1, p2)` — Custom curves
 
### 5.5 Icon System
 
**Approach:** Embed a minimal icon font (subset of Phosphor Icons or Lucide) compiled into the binary, similar to how Inter is currently embedded.
 
**Required icons (minimum set):**
```
Navigation:     chart-line, scatter-chart, axes, wrench, folder, settings, help
Toolbar:        zoom-in, hand, ruler, crosshair, pin, type
Actions:        export, save, copy, undo, redo, search, filter
Status:         check, warning, error, info
UI:             chevron-right, chevron-down, close, menu, maximize, minimize
Series:         eye, eye-off, palette, line-width
```
 
**Implementation:** `IconFont` class parallel to current font loading. Icons referenced by enum:
```cpp
enum class Icon : uint16_t {
    ChartLine, ScatterChart, Axes, Wrench, Folder,
    Settings, Help, ZoomIn, Hand, Ruler, Crosshair,
    Pin, Type, Export, Save, Copy, Undo, Redo,
    Search, Filter, Check, Warning, Error, Info,
    ChevronRight, ChevronDown, Close, Menu,
    Eye, EyeOff, Palette, LineWidth,
    // ...
};
 
void draw_icon(Icon icon, float size, const Color& color);
```
 
### 5.6 Color System for Plot Data
 
#### Default Palette (10 colors, perceptually uniform)
```
#4E79A7  (steel blue)
#F28E2B  (orange)
#E15759  (red)
#76B7B2  (teal)
#59A14F  (green)
#EDC948  (gold)
#B07AA1  (purple)
#FF9DA7  (pink)
#9C755F  (brown)
#BAB0AC  (gray)
```
 
#### Colorblind-Safe Palette (8 colors, Okabe-Ito)
```
#E69F00  (orange)
#56B4E9  (sky blue)
#009E73  (bluish green)
#F0E442  (yellow)
#0072B2  (blue)
#D55E00  (vermillion)
#CC79A7  (reddish purple)
#000000  (black)
```
 
#### Sequential Palettes
- **Viridis** (default for heatmaps)
- **Plasma**
- **Inferno**
- **Cividis** (colorblind-safe)
 
Palette selection via `ThemeManager::set_data_palette(name)`.
 
### 5.7 Pro vs Beginner Mode
 
| Aspect | Beginner | Pro |
|--------|----------|-----|
| Panel padding | Spacious (20px) | Compact (12px) |
| Font size | 14px base | 12px base |
| Tooltips | Verbose with descriptions | Terse, shortcut-only |
| Toolbar | Always visible | Auto-hide |
| Status bar | Simplified | Full metrics |
| Shortcuts | Shown in menus | Assumed known |
| Animations | Slightly slower (300ms) | Snappy (150ms) |
 
Toggle via Settings or `Ctrl+Shift+P` → "Toggle Pro Mode".
 
---
 
## 6. Differentiator Features
 
### 6.1 Time-Travel Scrubbing
For streaming/animated plots: a timeline scrubber at the bottom of the canvas.
- Drag to scrub through recorded frames.
- Click to set playback position.
- Shows frame number, timestamp, and miniature sparkline of data activity.
- Implementation: Ring buffer of frame snapshots (axis limits + series data hashes). Scrubbing replays from snapshot.
 
### 6.2 Inline Equation Rendering
- Axis labels and annotations support LaTeX-like math: `$\sin(\omega t)$`.
- Rendered via a lightweight math layout engine (no external dependency).
- GPU-rendered as textured quads from a glyph atlas.
 
### 6.3 Multi-Axis Linking
- Link X-axes across subplots: zooming one zooms all linked axes.
- Link Y-axes for synchronized vertical scaling.
- UI: Right-click axis → "Link to..." → select target axis.
- Visual indicator: colored link icon on linked axes.
 
### 6.4 Shared Cursor Across Subplots
- When crosshair is active, vertical line appears at same X position in all subplots.
- Each subplot shows its own Y value at the shared X.
- Essential for multi-signal analysis (e.g., voltage + current + temperature).
 
### 6.5 Data Transformation Panel
Accessible from Navigation Rail → Tools:
- **Smoothing:** Moving average, Savitzky-Golay, Gaussian kernel
- **FFT:** Frequency spectrum view (creates new series)
- **Normalize:** Min-max, Z-score, percentage
- **Derivative:** First/second derivative
- **Resample:** Interpolate to uniform spacing
- **Filter:** Low-pass, high-pass, band-pass (Butterworth)
 
Each transform creates a derived series (non-destructive). Original data preserved.
 
### 6.6 Snapshot Comparison Mode
- Take a "snapshot" of current view state.
- Make changes (zoom, filter, transform).
- Toggle comparison: side-by-side or overlay with opacity slider.
- Useful for before/after analysis.
 
### 6.7 Recording Timeline Editor
For animated plots:
- Visual timeline with keyframes.
- Drag keyframes to adjust timing.
- Preview animation in real-time.
- Export to MP4/GIF with custom resolution and frame rate.
 
### 6.8 Theme Export/Import
- Themes are JSON files.
- Export current theme → share with team.
- Import theme → apply instantly.
- Community theme gallery (future).
 
### 6.9 Workspace Save/Load
- Save: All figures, series configurations, panel states, zoom levels, theme → single `.spectra` file (JSON + binary data blobs).
- Load: Restore entire workspace state.
- Auto-save on exit (optional).
 
### 6.10 Plugin Architecture (Phase 3)
- Plugins are shared libraries (`.so`/`.dll`/`.dylib`) loaded at runtime.
- Plugin API:
  - Register new series types (e.g., candlestick, waterfall)
  - Register new panel sections
  - Register new export formats
  - Register new data transforms
  - Register new commands
- Plugin manifest: `plugin.json` with name, version, entry point.
 
---
 
## 7. Multi-Agent Execution Plan
 
### Agent A — Layout & Docking System
 
**Scope:** Replace hardcoded layout with a flexible, responsive zone system. Implement panel resizing, docking foundations, and multi-figure tabs.
 
**Files/Modules:**
- NEW: `src/ui/layout_manager.hpp/.cpp` — Zone-based layout engine
- NEW: `src/ui/dock_node.hpp/.cpp` — Dock tree data structure
- NEW: `src/ui/tab_bar.hpp/.cpp` — Figure tab bar widget
- MODIFY: `src/ui/imgui_integration.hpp/.cpp` — Replace hardcoded positions with layout queries
- MODIFY: `src/ui/app.cpp` — Multi-figure loop, tab switching
- MODIFY: `src/core/layout.cpp` — UI-aware subplot layout (account for chrome)
 
**API Contract:**
```cpp
class LayoutManager {
public:
    void update(float window_width, float window_height);
 
    Rect command_bar_rect() const;
    Rect nav_rail_rect() const;
    Rect canvas_rect() const;
    Rect inspector_rect() const;
    Rect status_bar_rect() const;
    Rect floating_toolbar_rect() const;
 
    void set_inspector_visible(bool v);
    void set_inspector_width(float w);
    void set_nav_rail_width(float w);
 
    bool is_inspector_visible() const;
    float inspector_width() const;
};
```
 
**Deliverables:**
1. `LayoutManager` computing all zone rects from window size
2. Resizable inspector panel (drag left edge)
3. Collapsible navigation rail
4. Figure tab bar with add/close/reorder
5. Status bar zone
 
**Acceptance Criteria:**
- All UI zones respond correctly to window resize (no hardcoded pixel positions)
- Inspector panel resizes smoothly between 240px and 480px
- Navigation rail collapses to icon-only (48px) and expands to icon+label (200px)
- Tab bar supports 1-20 figures without overflow (scrollable)
- Layout recomputes in < 0.1ms per frame
 
**Demo Scenario:** Open 3 figures, switch between tabs, resize inspector, collapse nav rail, resize window from 1280×720 to 2560×1440 — all zones adapt fluidly.
 
---
 
### Agent B — Interaction Engine
 
**Scope:** Overhaul zoom, pan, and gesture handling. Add inertial physics, animated transitions, and trackpad support.
 
**Files/Modules:**
- MODIFY: `src/ui/input.hpp/.cpp` — Add inertial pan, animated zoom, gesture state machine
- NEW: `src/ui/gesture_recognizer.hpp/.cpp` — Trackpad pinch/rotate detection
- NEW: `src/ui/animation_controller.hpp/.cpp` — Manages active UI animations (zoom transitions, pan inertia)
- MODIFY: `src/anim/easing.cpp` — Add spring, cubic-bezier easing functions
 
**API Contract:**
```cpp
class AnimationController {
public:
    using AnimId = uint32_t;
 
    AnimId animate_axis_limits(Axes& axes,
                               AxisLimits target_x, AxisLimits target_y,
                               float duration_sec, EasingFn easing);
    void cancel(AnimId id);
    void update(float dt);  // Called each frame
    bool has_active_animations() const;
};
 
class GestureRecognizer {
public:
    void on_scroll(double dx, double dy, bool is_trackpad);
    void on_pinch(float scale, float cx, float cy);
 
    bool is_pinching() const;
    float pinch_scale() const;
};
```
 
**Deliverables:**
1. Smooth animated zoom (scroll → 150ms ease-out transition)
2. Inertial pan (release drag → deceleration over 300ms)
3. Double-click auto-fit with animated transition
4. Box zoom with animated rectangle overlay
5. Trackpad pinch-to-zoom support
 
**Acceptance Criteria:**
- Zoom animation completes in exactly 150ms with no frame drops
- Inertial pan feels natural (friction coefficient tunable)
- Double-click auto-fit animates smoothly to data bounds
- Box zoom rectangle renders as dashed overlay during drag
- All animations cancelable by new input (no animation queue buildup)
 
**Demo Scenario:** Zoom into a dense region with scroll wheel (smooth animation), pan with inertia (release and watch it glide), double-click to auto-fit (animated snap), box-zoom a region (animated rectangle → zoom transition).
 
---
 
### Agent C — Inspector & Property Editing System
 
**Scope:** Build the right-side contextual property editor with collapsible sections, reusable widgets, and context-aware content.
 
**Files/Modules:**
- NEW: `src/ui/inspector.hpp/.cpp` — Inspector panel with section management
- NEW: `src/ui/widgets.hpp/.cpp` — Reusable UI components (PropertyRow, ColorField, SliderField, etc.)
- NEW: `src/ui/selection_context.hpp/.cpp` — Tracks what's currently selected (series, axes, figure)
- MODIFY: `src/ui/imgui_integration.cpp` — Remove `draw_section_*` methods, delegate to Inspector
 
**API Contract:**
```cpp
enum class SelectionType { None, Figure, Axes, Series };
 
struct SelectionContext {
    SelectionType type = SelectionType::None;
    Figure* figure = nullptr;
    Axes*   axes   = nullptr;
    Series* series = nullptr;
};
 
class Inspector {
public:
    void set_context(const SelectionContext& ctx);
    void draw(const Rect& bounds);  // Renders within given rect
 
private:
    void draw_figure_properties(Figure& fig);
    void draw_axes_properties(Axes& ax);
    void draw_series_properties(Series& s);
};
 
// Reusable widgets
namespace widgets {
    bool color_field(const char* label, Color& color);
    bool slider_field(const char* label, float& value, float min, float max);
    bool drag_field(const char* label, float& value, float speed, const char* fmt);
    bool checkbox_field(const char* label, bool& value);
    bool combo_field(const char* label, int& current, const char* const* items, int count);
    bool section_header(const char* label, bool* open);  // Collapsible
    void separator();
    void info_row(const char* label, const char* value);
}
```
 
**Deliverables:**
1. Inspector panel rendering in right zone
2. Context-aware content switching (figure/axes/series)
3. Collapsible sections with smooth animation
4. Reusable widget library (10+ widget types)
5. Series statistics display (min, max, mean, std, count)
6. Undo/redo for property changes
 
**Acceptance Criteria:**
- Clicking a series in the canvas selects it and shows its properties in inspector
- Clicking canvas background shows figure properties
- All property changes are reflected immediately in the plot
- Sections collapse/expand with 150ms animation
- Widget library covers all current property types + new ones
 
**Demo Scenario:** Click a line series → inspector shows "Line Series: sin(x)" with color picker, width slider, visibility toggle, data statistics. Change color → plot updates instantly. Collapse "Data" section → smooth animation. Click axes area → inspector switches to axis properties.
 
---
 
### Agent D — Visual System & Theming Engine
 
**Scope:** Implement the design token system, theme engine, dark/light/high-contrast themes, and icon system.
 
**Files/Modules:**
- NEW: `src/ui/theme.hpp/.cpp` — Theme data structures, ThemeManager
- NEW: `src/ui/design_tokens.hpp` — Spacing, radius, font constants
- NEW: `src/ui/icons.hpp/.cpp` — Icon font loading and rendering
- NEW: `third_party/icons/` — Embedded icon font data
- MODIFY: `src/ui/imgui_integration.cpp` — Replace all hardcoded colors with theme queries
- MODIFY: `src/render/renderer.cpp` — Read plot colors from theme
 
**API Contract:**
```cpp
class ThemeManager {
public:
    static ThemeManager& instance();
 
    void set_theme(const std::string& name);  // "dark", "light", "high_contrast"
    void register_theme(const std::string& name, Theme theme);
    const Theme& current() const;
    const ThemeColors& colors() const;
 
    void apply_to_imgui();  // Updates all ImGui style colors
    void apply_to_renderer(Renderer& renderer);  // Updates plot colors
 
    bool export_theme(const std::string& path) const;
    bool import_theme(const std::string& path);
 
    // Animated theme transition
    void transition_to(const std::string& name, float duration_sec = 0.2f);
    void update(float dt);
};
```
 
**Deliverables:**
1. `ThemeManager` with dark, light, and high-contrast themes
2. Design token constants used throughout all UI code
3. Icon font embedded and rendering via `draw_icon()`
4. All hardcoded colors replaced with theme references
5. Theme switching with animated cross-fade
6. Colorblind-safe data palette option
 
**Acceptance Criteria:**
- `Ctrl+Shift+T` toggles dark/light theme instantly
- All UI elements (panels, buttons, text, borders) update on theme change
- Plot grid, axes, text colors update on theme change
- Icons render at all sizes (16px–32px) without aliasing
- High-contrast theme passes WCAG AA contrast ratios
 
**Demo Scenario:** Start in dark theme. Press `Ctrl+Shift+T` → smooth 200ms transition to light theme. All panels, canvas background, grid lines, text labels update. Switch to high-contrast → bold borders, high-contrast text. Switch data palette to colorblind-safe → series colors update.
 
---
 
### Agent E — Data Interaction Layer
 
**Scope:** Implement hover tooltips, crosshair mode, data markers, region selection, and legend interaction.
 
**Files/Modules:**
- NEW: `src/ui/tooltip.hpp/.cpp` — Rich tooltip rendering
- NEW: `src/ui/crosshair.hpp/.cpp` — Crosshair overlay rendering
- NEW: `src/ui/data_marker.hpp/.cpp` — Persistent data markers
- NEW: `src/ui/region_select.hpp/.cpp` — Region selection with mini-toolbar
- MODIFY: `src/ui/input.hpp/.cpp` — Nearest-point snapping, selection state
- MODIFY: `src/render/renderer.cpp` — Render crosshair lines, selection rect, markers
 
**API Contract:**
```cpp
struct NearestPointResult {
    bool found = false;
    const Series* series = nullptr;
    size_t point_index = 0;
    float data_x = 0, data_y = 0;
    float screen_x = 0, screen_y = 0;
    float distance_px = 0;
};
 
class DataInteraction {
public:
    void update(const CursorReadout& cursor, Figure& figure);
 
    NearestPointResult nearest_point() const;
    bool crosshair_active() const;
    void toggle_crosshair();
 
    void add_marker(float data_x, float data_y, const Series* series);
    void remove_marker(size_t index);
    const std::vector<DataMarker>& markers() const;
 
    void draw_overlays(Renderer& renderer, const Rect& viewport);
};
```
 
**Deliverables:**
1. Nearest-point detection (spatial query, < 0.1ms for 100K points)
2. Rich hover tooltip with series name, coordinates, formatted values
3. Crosshair mode with axis-intersection labels
4. Persistent data markers (click to pin, right-click to remove)
5. Region selection with floating action toolbar
6. Legend click-to-toggle and drag-to-reposition
 
**Acceptance Criteria:**
- Tooltip appears within 50ms of hovering near a data point
- Nearest-point snaps to actual data (not interpolated)
- Crosshair renders as dashed lines clipped to plot area
- Markers persist through zoom/pan
- Region selection shows point count and basic statistics
- Legend toggle animates series opacity
 
**Demo Scenario:** Hover over a sine wave → tooltip shows "sin(x): X=3.14, Y=0.00". Toggle crosshair → dashed lines track cursor across all subplots. Click to pin a marker at the peak. Shift-drag to select a region → mini-toolbar shows "42 points, mean=0.71". Click legend entry → series fades out.
 
---

### Agent F — Command Palette & Productivity

**Scope:** Command palette, shortcuts, undo/redo, workspace save/load.

**Files:** NEW `command_palette.*`, `command_registry.*`, `shortcut_manager.*`, `undo_manager.*`, `workspace.*`.

**API:** `CommandRegistry::register_command()`, `search()`, `execute()`. `UndoManager::push/undo/redo`.

**Deliverables:** Command palette (Ctrl+K), 30+ commands, configurable shortcuts, undo/redo, workspace files.

**Acceptance:** Palette opens <16ms. Fuzzy search instant. Undo works for all property changes. Workspace restores full state.

---

### Agent G — Animation & Transition Engine

**Scope:** Unified animation system for all UI transitions.

**Files:** NEW `transition_engine.*`. MODIFY `easing.cpp`, `imgui_integration.cpp`, `input.cpp`.

**API:** `TransitionEngine::animate(float&, end, duration, easing)`, `animate(Color&, ...)`, `animate_limits(Axes&, ...)`.

**Deliverables:** TransitionEngine singleton, float/Color/AxisLimits animation, spring+cubic-bezier easing, migrate all animations.

**Acceptance:** 60fps no drops. Cancelable. No memory leaks. update() <0.05ms for 50 animations.

---

### Agent H — Testing & Performance

**Scope:** Tests, benchmarks, golden images, accessibility.

**Files:** NEW `test_layout_manager.cpp`, `test_theme.cpp`, `test_command_registry.cpp`, `test_transition_engine.cpp`, `test_undo_manager.cpp`, `test_data_interaction.cpp`, `bench_ui.cpp`.

**Deliverables:** >80% coverage, golden image tests, UI frame <2ms benchmark, tooltip <0.1ms benchmark, accessibility audit.

**Acceptance:** All tests pass cross-platform. Golden diff <1%. No memory leaks.

---

## 8. 90-Day Phased Roadmap

### Phase 1 — Modern Foundation (Days 1–30)

| Week | Agents | Deliverables |
|------|--------|-------------|
| 1 | D, A | Design tokens, ThemeManager, dark theme, icons, LayoutManager |
| 2 | A, C | Inspector zone, resizable panels, widget library, inspector shell |
| 3 | B, E | Animated zoom, inertial pan, hover tooltips |
| 4 | D, G, H | Light theme, TransitionEngine, unit tests |

**Exit criteria:** Dark/light themes, inspector panel, animated zoom, hover tooltips, no hardcoded positions, real icons.
**Perf:** UI <2ms, tooltip <0.5ms, theme switch <16ms.

### Phase 2 — Power User Features (Days 31–60)

| Week | Agents | Deliverables |
|------|--------|-------------|
| 5 | F, E | Command palette, crosshair, data markers, legend interaction |
| 6 | A, C | Multi-figure tabs, series statistics, collapsible sections |
| 7 | F, B | Undo/redo, workspace save/load, box zoom, double-click auto-fit |
| 8 | D, E, H | Colorblind palette, region selection, golden tests, benchmarks |

**Exit criteria:** Command palette, undo/redo, multi-figure tabs, crosshair, markers, workspace save/load.
**Perf:** Palette <16ms, nearest-point <0.1ms/100K, undo <1ms.

### Phase 3 — Elite Differentiators (Days 61–90)

| Week | Agents | Deliverables |
|------|--------|-------------|
| 9 | A | Docking system, split view |
| 10 | E, B | Shared cursor across subplots, multi-axis linking, data transforms |
| 11 | G, F | Timeline editor, recording export, theme export/import |
| 12 | H | Full test suite, performance optimization, documentation |

**Exit criteria:** Docking, split view, linked axes, data transforms, timeline editor, plugin-ready architecture.
**Perf:** Docking layout <0.5ms, 100K points interactive at 60fps.

---

## 9. Technical Risks & Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| ImGui docking branch instability | Docking features blocked | Use ImGui docking branch from day 1; fallback to manual panel management |
| Animated zoom causing axis limit race conditions | Visual glitches | TransitionEngine owns limit mutations during animation; input blocked |
| Theme switching performance with many draw calls | Frame spike on switch | Batch all color updates in single pass; pre-compute ImGui style arrays |
| Nearest-point query too slow for 1M+ points | Tooltip lag | Spatial index (grid-based) built on data upload; query O(1) amortized |
| Icon font licensing | Legal | Use MIT-licensed Lucide or Phosphor icons |
| Workspace file format versioning | Load failures | Semantic versioning in file header; migration functions per version |
| Plugin ABI stability | Plugin breakage on update | Stable C ABI for plugin interface; version negotiation on load |
| Resize loop regression | App termination | Keep existing throttling; add integration test for resize stability |
| Cross-platform font rendering differences | Golden test failures | Platform-specific golden baselines; tolerance threshold in diff |
| Memory growth from undo stack | OOM on long sessions | Cap undo stack at 100 entries; compress old entries |

---

## Appendix: File Structure After Redesign

```
src/ui/
├── app.cpp                    (modified — multi-figure, clean loop)
├── command_palette.hpp/.cpp   (NEW)
├── command_registry.hpp/.cpp  (NEW)
├── crosshair.hpp/.cpp         (NEW)
├── data_marker.hpp/.cpp       (NEW)
├── design_tokens.hpp          (NEW)
├── dock_node.hpp/.cpp         (NEW)
├── gesture_recognizer.hpp/.cpp(NEW)
├── glfw_adapter.hpp/.cpp      (existing)
├── icons.hpp/.cpp             (NEW)
├── imgui_integration.hpp/.cpp (modified — delegates to components)
├── input.hpp/.cpp             (modified — animated interactions)
├── inspector.hpp/.cpp         (NEW)
├── layout_manager.hpp/.cpp    (NEW)
├── region_select.hpp/.cpp     (NEW)
├── selection_context.hpp/.cpp (NEW)
├── shortcut_manager.hpp/.cpp  (NEW)
├── tab_bar.hpp/.cpp           (NEW)
├── theme.hpp/.cpp             (NEW)
├── tooltip.hpp/.cpp           (NEW)
├── transition_engine.hpp/.cpp (NEW)
├── undo_manager.hpp/.cpp      (NEW)
├── widgets.hpp/.cpp           (NEW)
└── workspace.hpp/.cpp         (NEW)
```

**Total new files:** ~30 | **Modified files:** ~8 | **Deleted files:** 0

This redesign transforms Spectra from a functional plotting tool into a professional-grade 2026 visualization platform competitive with TradingView, Figma, and VS Code in UX quality.



Weekly Execution Matrix:
Week	Primary Focus	Parallel Work	Dependencies Met
1	D (Theme)	A (Layout)	None
2	A (Layout finish)	C (Inspector)	D done, A zones ready
3	B (Interactions)	E (Data interaction)	A zones, D theme
4	G (Transitions)	H (Testing D/A/C)	All previous
5	F (Command palette)	E (Crosshair/markers)	G animations, A/D
6	A (Multi-figure)	C (Statistics)	Inspector mature
7	F (Undo/redo)	B (Box zoom)	Workspace ready
8	D (Colorblind)	E (Region select)	H full benchmark
9	A (Docking)	H (Plugin prep)	Layout mature
10	E (Linked axes)	B (Multi-axis)	Interaction solid
11	G (Timeline)	F (Export)	Full feature set
12	H (Final polish)	Documentation	Everything tested
Risk-Based Parallelism:
Safe to Parallelize:

D + A (Week 1) — Theme and layout are independent
B + E (Week 3) — Both need layout but don't depend on each other
F + G (Week 4-5) — Can develop command palette while transition engine matures
Must Sequence:

A → C (Layout zones needed for inspector positioning)
D → All (Theme system needed before any UI rendering)
G → F (Transition engine needed for smooth palette animations)
C → Statistics (Inspector needed for statistics panel)
Agent Handoff Points:
Week 1→2: Agent D provides theme API → Agent C uses for inspector styling
Week 2→3: Agent A provides layout zones → Agents B/E use for coordinate mapping
Week 4→5: Agent G provides animation API → Agent F uses for palette transitions
Week 6→7: Agent C provides mature inspector → Agent F integrates workspace save
Week 8→9: Agent H validates all Phase 2 → Agent A begins Phase 3 docking
Decision Gates:
End Week 2: Layout zones stable? → Proceed with interaction work
End Week 4: Animation system working? → Proceed with command palette
End Week 8: All Phase 2 features passing tests? → Proceed to Phase 3
End Week 11: Core features complete? → Final optimization and documentation

---

## 10. Roadmap Update Protocol (MANDATORY)

**File:** `plans/ROADMAP.md`

> **EVERY AGENT MUST update `plans/ROADMAP.md` at the END of their work session.**  
> This is a non-negotiable requirement. The roadmap is the single source of truth for project progress.

### What to Update

1. **Deliverable status** — In the relevant week's table, change your deliverables:
   - `⏳ Not Started` → `🔄 In Progress` → `✅ Done`
   - Add the actual file paths if they differ from what was planned.

2. **File Inventory** — Add any new files you created to the inventory table. Include: file path, your agent letter, week number, and whether it's in `CMakeLists.txt`.

3. **Test table** — Update test counts and pass/fail status for any tests you added or modified.

4. **Known Issues** — Log any bugs found, workarounds applied, or technical debt introduced.

5. **Phase progress** — Update the overall phase completion percentage in the Phase Overview table at the top.

6. **Exit criteria** — Check off `[x]` any criteria that are now satisfied.

7. **Last Updated date** — Update the date at the top of `ROADMAP.md`.

### Status Icons
| Icon | Meaning |
|------|---------|
| `⏳ Not Started` | Work has not begun |
| `🔄 In Progress` | Actively being worked on |
| `✅ Done` | Complete and tested |
| `⚠️ Blocked` | Cannot proceed (explain in Issues table) |
| `🔴 Reverted` | Was done but rolled back |

### Why This Matters
- Agents starting new weeks rely on the roadmap to know what's ready and what's blocked.
- The decision gates (End Week 2, 4, 8, 11) are evaluated from roadmap status.
- Without updates, parallel agents may duplicate work or build on incomplete foundations.

---

## 11. Build Coordination Protocol (MANDATORY)

> **Problem:** Parallel agents break each other's builds by adding files to `CMakeLists.txt` that reference code another agent hasn't finished yet, or by compiling the full project while another agent's work is incomplete.

### Rule 1 — All source files use `if(EXISTS)` guards in CMake

Both `CMakeLists.txt` and `tests/CMakeLists.txt` use **`if(EXISTS)`-guarded loops** for all UI sources and test files. This means:

- **A missing `.cpp` file will NOT break the build.** CMake silently skips it.
- When you create a new `.cpp` file, add its name to the appropriate `foreach()` list in `CMakeLists.txt`. It will be picked up automatically once the file exists on disk.
- **NEVER use a bare `target_sources()` or `add_plotix_test()` call without an `if(EXISTS)` guard** for files that another agent might not have created yet.

**Main CMakeLists.txt pattern (non-ImGui UI sources):**
```cmake
foreach(_ui_src
    src/ui/input.cpp
    src/ui/your_new_file.cpp   # ← just add here
)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_ui_src})
        list(APPEND PLOTIX_UI_SOURCES ${_ui_src})
    endif()
endforeach()
```

**Main CMakeLists.txt pattern (ImGui-dependent sources):**
```cmake
foreach(_imgui_src
    src/ui/theme.cpp
    src/ui/your_new_file.cpp   # ← just add here
)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_imgui_src})
        target_sources(spectra PRIVATE ${_imgui_src})
    endif()
endforeach()
```

**tests/CMakeLists.txt pattern:**
```cmake
set(PLOTIX_UNIT_TESTS
    test_transform
    test_your_new_test          # ← just add here
)

foreach(_test ${PLOTIX_UNIT_TESTS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/${_test}.cpp)
        add_plotix_test(unit ${_test})
    endif()
endforeach()
```

### Rule 2 — New headers must compile standalone

Every new `.hpp` file you create must be **self-contained**: it must compile when included on its own. This means:
- Include all dependencies at the top (no implicit includes from other headers).
- Use forward declarations where possible to minimize coupling.
- Use `#ifdef PLOTIX_USE_IMGUI` guards if the header depends on ImGui types.

This prevents Agent X's header from failing to compile because Agent Y's header (which it transitively included) references a type that Agent Z hasn't defined yet.

### Rule 3 — Compile only your own targets during development

While working on your deliverables, **do NOT build the entire project**. Instead:

```bash
# Build only your specific test target
cmake --build build --target unit_test_your_feature

# Build only the spectra library (to check your .cpp compiles)
cmake --build build --target spectra

# Do NOT run 'cmake --build build' (builds everything including other agents' tests)
# Do NOT run 'ctest' (runs all tests including ones that depend on other agents' work)
```

**At the end of your session**, after all your files are committed, you may run the full build to verify no regressions. If the full build fails due to another agent's incomplete work, **do not fix their files** — just note it in `plans/ROADMAP.md` under Known Issues.

### Rule 4 — Guard cross-agent includes with `#ifdef`

If your code optionally uses another agent's module that may not exist yet:

```cpp
// Good: guarded include
#ifdef PLOTIX_HAS_TRANSITION_ENGINE
#include "ui/transition_engine.hpp"
#endif

// Good: null-check pointer (existing pattern)
if (transition_engine_) {
    transition_engine_->animate(...);
} else {
    // fallback behavior
}
```

The existing codebase already follows this pattern (e.g., `InputHandler` falls back when `TransitionEngine*` is null). **Continue this pattern for all cross-agent dependencies.**

### Rule 5 — Never modify another agent's in-progress files

If you need to modify a file that another parallel agent is also working on:
1. Check `plans/ROADMAP.md` to see if the file is marked `🔄 In Progress` by another agent.
2. If yes, **do not modify it**. Instead, create a separate integration file or defer the change.
3. If you must touch a shared file (e.g., `app.cpp`), coordinate by noting it in the Known Issues table and keeping your changes minimal and additive (append-only).

### Summary

| Rule | What | Why |
|------|------|-----|
| 1 | `if(EXISTS)` guards in CMake | Missing files don't break build |
| 2 | Headers compile standalone | No transitive include failures |
| 3 | Build only your targets | Don't trigger other agents' compile errors |
| 4 | `#ifdef` / null-check cross-agent deps | Graceful degradation when modules are absent |
| 5 | Don't modify other agents' in-progress files | Avoid merge conflicts and broken state |