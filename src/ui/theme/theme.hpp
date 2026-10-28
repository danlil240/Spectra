#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/theme_api.hpp>
#include <string_view>

#include "design_tokens.hpp"

struct ImVec4;   // Forward declaration for ImGui

namespace spectra::ui
{
struct ThemeGlassSettings;
enum class GlassSurface;
}   // namespace spectra::ui

namespace spectra
{
class EventSystem;
class Figure;

namespace detail
{
// Invokes the registered public theme-change callback (if any) with `snap`.
// Holds the registration lock for the whole call, so it is synchronized with
// register_theme_change_callback().  Implemented in theme_api.cpp.
void invoke_theme_change_callback(const ThemeSnapshot& snap);

// Test-only: called inside invoke_theme_change_callback() after the registered
// pointer is loaded and before it is called (nullptr disables).
void set_theme_callback_dispatch_hook_for_testing(void (*hook)());
}   // namespace detail
}   // namespace spectra

namespace spectra::ui
{

// 32-bit color structure (RGBA)
struct Color
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    // From hex (0xRRGGBB or 0xAARRGGBB)
    static constexpr Color from_hex(uint32_t hex)
    {
        if (hex > 0xFFFFFF)
        {
            // ARGB format - convert to RGBA
            return Color(((hex >> 16) & 0xFF) / 255.0f,
                         ((hex >> 8) & 0xFF) / 255.0f,
                         (hex & 0xFF) / 255.0f,
                         ((hex >> 24) & 0xFF) / 255.0f);
        }

        // RGB format
        return Color(((hex >> 16) & 0xFF) / 255.0f,
                     ((hex >> 8) & 0xFF) / 255.0f,
                     (hex & 0xFF) / 255.0f,
                     1.0f);
    }

    constexpr uint32_t to_hex() const
    {
        return (uint32_t(r * 255.0f) << 24) | (uint32_t(g * 255.0f) << 16)
               | (uint32_t(b * 255.0f) << 8) | uint32_t(a * 255.0f);
    }

    constexpr Color with_alpha(float alpha) const { return Color(r, g, b, alpha); }

    constexpr Color lerp(const Color& other, float t) const
    {
        return Color(r + (other.r - r) * t,
                     g + (other.g - g) * t,
                     b + (other.b - b) * t,
                     a + (other.a - a) * t);
    }

    // IEC 61966-2-1 sRGB transfer functions for a single channel in [0, 1].
    // Not constexpr: std::pow is not usable in constant expressions in C++20.
    static float srgb_to_linear(float c)
    {
        return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    static float linear_to_srgb(float c)
    {
        return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    // WCAG 2.1 relative luminance (0 = black, 1 = white) of the opaque RGB
    // channels. Alpha is ignored; use composite_over() first for translucent
    // colours.
    float luminance() const
    {
        return 0.2126f * srgb_to_linear(r) + 0.7152f * srgb_to_linear(g)
               + 0.0722f * srgb_to_linear(b);
    }

    // WCAG 2.1 contrast ratio (1:1 to 21:1) between two colours treated as
    // opaque. Alpha is ignored; use contrast_ratio_over() for translucent
    // foregrounds.
    float contrast_ratio(const Color& other) const
    {
        float l1      = luminance();
        float l2      = other.luminance();
        float lighter = (l1 > l2) ? l1 : l2;
        float darker  = (l1 > l2) ? l2 : l1;
        return (lighter + 0.05f) / (darker + 0.05f);
    }

    // Source-over composite of this colour onto an opaque `bg`, blended in
    // encoded sRGB like ImGui and the UI pipeline. The result is opaque.
    constexpr Color composite_over(const Color& bg) const
    {
        const float t = (a < 0.0f) ? 0.0f : (a > 1.0f ? 1.0f : a);
        return Color(r * t + bg.r * (1.0f - t),
                     g * t + bg.g * (1.0f - t),
                     b * t + bg.b * (1.0f - t),
                     1.0f);
    }

    // WCAG 2.1 contrast ratio of this (possibly translucent) foreground as
    // displayed over the opaque background `bg`. `bg` alpha is ignored.
    float contrast_ratio_over(const Color& bg) const
    {
        return composite_over(bg).contrast_ratio(bg.with_alpha(1.0f));
    }

    // Convert sRGB to linear RGB
    Color to_linear() const
    {
        return Color(srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), a);
    }

    // Convert linear RGB to sRGB
    Color to_srgb() const
    {
        return Color(linear_to_srgb(r), linear_to_srgb(g), linear_to_srgb(b), a);
    }

    // Convert to HSL (h: 0-360, s: 0-1, l: 0-1)
    struct HSL
    {
        float h, s, l;
    };
    HSL to_hsl() const
    {
        float max_c = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
        float min_c = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
        float l     = (max_c + min_c) * 0.5f;
        if (max_c == min_c)
            return {0.0f, 0.0f, l};
        float d = max_c - min_c;
        float s = (l > 0.5f) ? d / (2.0f - max_c - min_c) : d / (max_c + min_c);
        float h = 0.0f;
        if (max_c == r)
            h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (max_c == g)
            h = (b - r) / d + 2.0f;
        else
            h = (r - g) / d + 4.0f;
        return {h * 60.0f, s, l};
    }

    // Create from HSL
    static Color from_hsl(float h, float s, float l, float a = 1.0f)
    {
        if (s == 0.0f)
            return Color(l, l, l, a);
        auto hue2rgb = [](float p, float q, float t) -> float
        {
            if (t < 0.0f)
                t += 1.0f;
            if (t > 1.0f)
                t -= 1.0f;
            if (t < 1.0f / 6.0f)
                return p + (q - p) * 6.0f * t;
            if (t < 1.0f / 2.0f)
                return q;
            if (t < 2.0f / 3.0f)
                return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
            return p;
        };
        float q  = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
        float p  = 2.0f * l - q;
        float hn = h / 360.0f;
        return Color(hue2rgb(p, q, hn + 1.0f / 3.0f),
                     hue2rgb(p, q, hn),
                     hue2rgb(p, q, hn - 1.0f / 3.0f),
                     a);
    }

    constexpr bool operator==(const Color& o) const
    {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

struct ThemeColors
{
    // Surfaces
    Color bg_canvas;      // Plot area background (deepest layer)
    Color bg_primary;     // Main window background
    Color bg_secondary;   // Panel backgrounds
    Color bg_tertiary;    // Card/section backgrounds
    Color bg_elevated;    // Floating elements (tooltips, popups)
    Color bg_overlay;     // Modal overlay (semi-transparent)

    // Text
    Color text_primary;     // Main text
    Color text_secondary;   // Labels, descriptions
    Color text_tertiary;    // Placeholders, disabled
    Color text_inverse;     // Text on accent backgrounds

    // Borders
    Color border_default;   // Standard borders
    Color border_subtle;    // Subtle dividers
    Color border_strong;    // Focused elements

    // Interactive
    Color accent;          // Primary accent (buttons, links, active states)
    Color accent_hover;    // Accent hover state
    Color accent_muted;    // Accent backgrounds (selected items)
    Color accent_subtle;   // Very subtle accent tint

    // Semantic
    Color success;
    Color warning;
    Color error;
    Color info;
    Color critical;   // Highest severity (fatal / crash); distinct from error

    // Plot-specific
    Color grid_major;   // Major gridlines (labeled tick positions)
    Color grid_minor;   // Minor gridlines (subdivisions, fainter)
    Color grid_line;    // Compat: legacy single grid color
    Color axis_line;
    Color tick_label;
    Color crosshair;
    Color selection_fill;
    Color selection_border;
    Color tooltip_bg;
    Color tooltip_border;

    // Visual effects
    Color accent_glow;             // Glow color for Night theme active elements
    float glow_intensity = 0.0f;   // Glow strength multiplier
    Color focus_ring;              // Keyboard focus ring color
    Color scrollbar_thumb;         // Scrollbar thumb color
    Color scrollbar_track;         // Scrollbar track color
    Color section_header_bg;       // Inspector section header background
    Color input_bg;                // Input field background
    Color hover_highlight;         // Hover highlight tint
    Color annotation_bg;           // Annotation background
    Color roi_fill;                // Region-of-interest fill
    Color roi_border;              // Region-of-interest border
};

// Theme-adaptive series colors.
//
// Pure black ('k' / colors::black) is unreadable on a dark plot background and
// pure white ('w' / colors::white) on a light one.  Like MATLAB's dark mode,
// such a series is drawn in the theme's primary foreground color instead, so
// "k-" means "the default ink color" on every theme.  Any other color —
// including near-blacks such as 0x111111 — is left exactly as requested.
// Alpha is always preserved.
template <typename C>
inline C adapt_series_color_to_theme(const C& c, const ThemeColors& theme)
{
    const float bg_luma =
        0.2126f * theme.bg_canvas.r + 0.7152f * theme.bg_canvas.g + 0.0722f * theme.bg_canvas.b;
    const bool is_black = c.r == 0.0f && c.g == 0.0f && c.b == 0.0f;
    const bool is_white = c.r == 1.0f && c.g == 1.0f && c.b == 1.0f;
    if ((is_black && bg_luma < 0.35f) || (is_white && bg_luma > 0.65f))
    {
        C out = c;
        out.r = theme.text_primary.r;
        out.g = theme.text_primary.g;
        out.b = theme.text_primary.b;
        return out;
    }
    return c;
}

// Color Vision Deficiency types
enum class CVDType
{
    None,           // Normal vision
    Protanopia,     // Red-blind
    Deuteranopia,   // Green-blind
    Tritanopia,     // Blue-blind
    Achromatopsia   // Total color blindness
};

struct DataPalette
{
    std::string          name;
    std::string          description;
    std::vector<Color>   colors;
    bool                 colorblind_safe = false;
    std::vector<CVDType> safe_for;   // Which CVD types this palette is safe for
    // Ordered ramp (viridis, monochrome): the slots' lightness order carries
    // meaning, so visible_data_palette() keeps it monotonic instead of moving
    // each low-contrast slot on its own.
    bool sequential = false;

    // Get color by index (wraps around)
    const Color& operator[](size_t index) const { return colors[index % colors.size()]; }

    // Check if safe for a specific CVD type
    bool is_safe_for(CVDType type) const
    {
        if (type == CVDType::None)
            return true;
        for (auto t : safe_for)
            if (t == type)
                return true;
        return false;
    }
};

// CVD simulation: approximate how a color appears to someone with a given deficiency
Color simulate_cvd(const Color& c, CVDType type);

struct ThemeGlassSettings
{
    float master_intensity = 0.65f;
    float panel_alpha      = 0.62f;
    float plot_alpha       = 0.28f;
    float toolbar_alpha    = 0.58f;
    float glow_strength    = 0.45f;
    bool  blur_enabled     = false;

    static ThemeGlassSettings night_defaults();
    static ThemeGlassSettings dark_defaults();
    static ThemeGlassSettings light_defaults();

    void clamp();
};

enum class GlassSurface
{
    Panel,
    Toolbar,
    Plot,
};

struct Theme
{
    std::string        name;
    std::string        display_name;   // Human-readable name for UI ("High Contrast")
    ThemeColors        colors;
    DataPalette        data_palette;   // Theme's default palette (used when "default" is selected)
    ThemeGlassSettings glass_defaults;

    // Visual properties
    float opacity_panel    = 0.95f;   // Panel background opacity (for blur effect)
    float opacity_tooltip  = 0.98f;   // Tooltip background opacity
    float shadow_intensity = 1.0f;    // Shadow alpha multiplier
    float border_width     = tokens::BORDER_WIDTH_NORMAL;   // Default border width
    bool  use_blur         = true;                          // Enable backdrop blur effects

    // Animation settings
    float animation_speed   = 1.0f;   // Global animation speed multiplier
    bool  enable_animations = true;   // Master animation toggle

    // Color space: when true, theme color values are in sRGB and must be
    // converted to linear before passing to the VK_FORMAT_B8G8R8A8_SRGB
    // framebuffer.  Night theme sets this to true; Dark/Light do not.
    bool linearize_colors = false;
};

class ThemeManager
{
   public:
    // Returns the ThemeManager registered via set_current().
    // Asserts if no instance has been set — every entry point must own
    // a ThemeManager and call set_current() before any access.
    static ThemeManager& instance();

    // Theme registration and switching
    void               register_theme(const std::string& name, Theme theme);
    void               set_theme(const std::string& name);
    const Theme&       current() const;
    const ThemeColors& colors() const;
    const std::string& current_theme_name() const;
    // Human-readable name of a theme id for UI labels ("high_contrast" -> "High Contrast").
    // Uses the registered theme's display_name, falling back to theme_display_name(id).
    std::string display_name(std::string_view theme_id) const;

    // Data palette management.  The selected palette name is global across
    // themes; "default" resolves to the active theme's own default palette
    // (Theme::data_palette), so it suits the theme's background.
    void               set_data_palette(const std::string& palette_name);
    void               register_data_palette(const std::string& name, DataPalette palette);
    const DataPalette& current_data_palette() const;
    const DataPalette& get_data_palette(const std::string& name) const;
    const std::vector<std::string>& available_data_palettes() const;
    const std::string& current_data_palette_name() const { return current_data_palette_name_; }

    // Plot surface of a figure (S10).  A figure whose FigureStyle::background_override
    // is set is drawn on that colour instead of the theme canvas, so every
    // background-dependent choice (format-letter shades, 'k'/'w' remapping, the
    // "default" palette, axis/tick/title/legend ink) must follow that surface.
    // plot_surface_colors() returns the active colours with bg_canvas replaced by
    // the override; when the override's lightness is opposite to the theme
    // canvas, the plot ink tokens come from the built-in theme of that lightness
    // ("light" or "dark").  Without an override — or with one equal to the theme
    // canvas — the result equals colors().
    ThemeColors        plot_surface_colors(const FigureStyle& style) const;
    const DataPalette& plot_surface_data_palette(const FigureStyle& style) const;

    // Palette swatches as the plot actually draws them: palette `name` ("" =
    // the current palette, "default" = the theme's own) passed through
    // visible_data_palette() against the theme canvas, then through the final
    // adapt_series_color_to_theme() ink step (pure black / white slots become
    // the theme foreground), exactly as auto-coloured series reach the screen.
    // Settings and colour-picker swatches draw (and the picker assigns) these
    // final colours.  The raw palette definition stays available through
    // get_data_palette() / current_data_palette(), and the pre-ink auto slot
    // values (what Series::color() holds) through visible_data_palette().
    DataPalette displayed_data_palette(const std::string& name = {}) const;

    // Animated palette transition
    void transition_palette(const std::string& palette_name,
                            float              duration_sec = tokens::DURATION_SLOW);
    bool is_palette_transitioning() const;

    // App UI scale (1.0 = 100%), clamped to [UI_SCALE_MIN, UI_SCALE_MAX].
    // apply_to_imgui() scales ImGui sizes and fonts by it; overlays receive it
    // through ImGui's FontScaleMain.  set_ui_scale() re-applies to the current
    // ImGui context.
    static constexpr float UI_SCALE_MIN = 0.75f;
    static constexpr float UI_SCALE_MAX = 2.0f;
    static float           clamp_ui_scale(float scale);
    void                   set_ui_scale(float scale);
    float                  ui_scale() const { return ui_scale_; }

    // Theme application
    void apply_to_imgui();                              // Updates all ImGui style colors
    void apply_to_renderer(class Renderer& renderer);   // Updates plot colors

    // Animated theme transition
    void transition_to(const std::string& name, float duration_sec = tokens::DURATION_SLOW);
    void update(float dt);   // Update ongoing transitions
    bool is_transitioning() const;

    // Per-export theme (Figure::save_png / save_svg with ExportOptions::theme).
    // push_export_theme() makes colors(), current() and the "default" data palette
    // follow the registered theme `name` until pop_export_theme(), without
    // touching ImGui, theme listeners or the persisted selection; any running
    // theme or palette transition is suspended and resumed afterwards.  Returns
    // false (and changes nothing) for an unknown name or while another export
    // theme is active.  Prefer ScopedExportTheme.
    bool push_export_theme(const std::string& name);
    void pop_export_theme();
    bool export_theme_active() const { return export_override_.active; }

    // Theme persistence
    bool export_theme(const std::string& path) const;
    bool import_theme(const std::string& path);
    void save_current_as_default();
    void load_default();

    // Glass / transparency (user-adjustable; persisted with default theme export)
    const ThemeGlassSettings& glass() const { return glass_settings_; }
    ThemeGlassSettings&       glass_mut() { return glass_settings_; }
    void  set_glass_settings(const ThemeGlassSettings& settings, bool apply = true);
    void  reset_glass_defaults();
    float glass_surface_alpha(GlassSurface surface) const;
    Color glass_resolved_surface(Color base, GlassSurface surface) const;
    Color glass_resolved_plot_background() const;
    float effective_glow_intensity() const;

    // Version counter — incremented on every theme color change.
    // Renderers can compare against their cached version to skip redundant uploads.
    uint32_t theme_version() const { return theme_version_; }

    // Fill a public ThemeSnapshot from this instance's current (display) colors.
    void fill_snapshot(ThemeSnapshot& out) const;

    // Utility
    Color get_color(const std::string& color_name) const;
    Color lerp_color(const std::string& color_name, const Color& target, float t) const;

    // Redirect ThemeManager::instance() to a specific instance.
    // Call with a non-null pointer to make instance() return that object;
    // call with nullptr to clear.  Every entry point (App, agent, embed,
    // Qt runtime) must own a ThemeManager and call set_current() before
    // any subsystem accesses instance().  Calling instance() without a
    // prior set_current() triggers an assertion failure.
    static void set_current(ThemeManager* tm);

    // Publicly constructible so owners (App, agent, embed, Qt) can create
    // their own instances.
    ThemeManager() = default;

    // Set the event system for cross-subsystem notifications.
    void set_event_system(spectra::EventSystem* es) { event_system_ = es; }

    // Ensure themes and palettes are initialized. Safe to call multiple times.
    // ImGuiIntegration calls this before apply_to_imgui() so the injected
    // instance is ready without going through instance().
    void ensure_initialized();

   private:
    std::unordered_map<std::string, Theme> themes_;
    std::string                            current_theme_name_ = "night";
    Theme*                                 current_theme_      = nullptr;

    std::unordered_map<std::string, DataPalette> data_palettes_;
    mutable std::vector<std::string>             palette_names_cache_;
    mutable bool                                 palette_names_dirty_       = true;
    std::string                                  current_data_palette_name_ = "default";

    // Theme transition state (does NOT mutate stored themes)
    bool        transitioning_       = false;
    float       transition_time_     = 0.0f;
    float       transition_duration_ = 0.0f;
    ThemeColors transition_start_colors_;
    ThemeColors transition_target_colors_;
    std::string transition_target_name_;
    ThemeColors display_colors_;   // Current colors for rendering (may be mid-transition)
    bool        display_colors_valid_ = false;

    // Palette transition state
    bool               palette_transitioning_       = false;
    float              palette_transition_time_     = 0.0f;
    float              palette_transition_duration_ = 0.0f;
    std::vector<Color> palette_start_colors_;
    std::vector<Color> palette_target_colors_;
    std::string        palette_transition_target_name_;
    DataPalette        display_palette_;   // Current palette for rendering
    bool               display_palette_valid_ = false;

    // Theme version counter (monotonically increasing)
    uint32_t theme_version_ = 0;

    // Default theme persistence
    std::string default_theme_path_;

    // Event system for cross-subsystem notifications
    spectra::EventSystem* event_system_ = nullptr;

    // Public ThemeChangeCallback bridge (see include/spectra/theme_api.hpp).
    // Invokes the registered callback when the snapshot colors differ from the
    // last snapshot this instance reported; identical snapshots are dropped.
    void          notify_public_theme_change();
    ThemeSnapshot last_notified_snapshot_{};
    bool          last_notified_valid_ = false;

    ThemeGlassSettings glass_settings_;
    ThemeGlassSettings glass_defaults_for(const std::string& theme_name) const;

    // State saved by push_export_theme(), restored by pop_export_theme().
    struct ExportOverride
    {
        bool        active = false;
        std::string theme_name;
        Theme*      theme                 = nullptr;
        bool        transitioning         = false;
        bool        display_colors_valid  = false;
        bool        palette_transitioning = false;
        bool        display_palette_valid = false;
    } export_override_;

    // displayed_data_palette() cache: recomputed only when the source colours,
    // the ramp flag, the canvas or the theme ink change.
    struct DisplayedPaletteCache
    {
        std::vector<Color> src;
        bool               sequential = false;
        Color              canvas;
        Color              ink;   // text_primary used for the final ink step
        DataPalette        result;
        bool               valid = false;
    };
    mutable DisplayedPaletteCache displayed_palette_cache_;

    float ui_scale_ = 1.0f;

    void               initialize_default_themes();
    void               initialize_data_palettes();
    const DataPalette& resolve_data_palette(const std::string& name) const;
    ThemeColors interpolate_colors(const ThemeColors& start, const ThemeColors& end, float t) const;
    // Built-in theme whose ink suits @p style's override surface when that
    // surface's lightness is opposite to the theme canvas; nullptr otherwise.
    const Theme* plot_surface_ink_theme(const FigureStyle& style) const;
};

// RAII guard for ThemeManager::push_export_theme(): renders with theme `name`
// for the guard's lifetime.  An empty name or null manager does nothing; an
// unknown name logs a warning and keeps the active theme.
class ScopedExportTheme
{
   public:
    ScopedExportTheme(ThemeManager* tm, const std::string& name);
    ~ScopedExportTheme();
    ScopedExportTheme(const ScopedExportTheme&)            = delete;
    ScopedExportTheme& operator=(const ScopedExportTheme&) = delete;

    bool active() const { return active_; }

   private:
    ThemeManager* tm_     = nullptr;
    bool          active_ = false;
};

// True when the theme's plot canvas is light (format-letter colours then use
// their light-background shades, see format_colors in spectra/color.hpp).
inline bool is_light_plot_background(const ThemeColors& theme)
{
    const float luma =
        0.2126f * theme.bg_canvas.r + 0.7152f * theme.bg_canvas.g + 0.0722f * theme.bg_canvas.b;
    return luma > 0.5f;
}

// Resolve the colours of auto-coloured series (Series::has_auto_color()) in
// @p figure from @p palette, by palette slot, and of format-letter series
// (Series::color_letter(), e.g. "r--") to the letter's tuned shade for a dark
// or light background.  Explicitly coloured series are left untouched.  An
// empty palette falls back to palette::default_cycle.
// Called by the renderer before drawing a figure and by the UI before building
// overlays (legend), so palette and theme switches recolour existing plots.
void resolve_auto_series_colors(Figure&            figure,
                                const DataPalette& palette,
                                bool               light_background = false);

// As above, but auto-coloured series take their colour from
// visible_data_palette(palette, canvas), so every auto slot stays visible on
// @p canvas, the colour the plot area actually renders with (normally
// effective_figure_background()).  Format-letter and explicit colours are
// handled exactly as above.  The renderer, the UI overlays (legend) and SVG
// export all resolve through this overload, so PNG, SVG and screen agree.
void resolve_auto_series_colors(Figure&               figure,
                                const DataPalette&    palette,
                                bool                  light_background,
                                const spectra::Color& canvas);

// ─── Auto series colour visibility rule ─────────────────────────────────────
// Minimum WCAG contrast (exact sRGB relative luminance) between an
// auto-assigned series colour and the plot canvas: the WCAG 2.1 non-text
// (graphical object) threshold, applied to line, scatter and marker strokes.
inline constexpr float AUTO_SERIES_MIN_CONTRAST = 3.0f;

// Returns @p palette with every slot made visible on @p canvas:
//  * A slot whose contrast against the canvas is already >= @p min_contrast is
//    kept bit-for-bit.
//  * Pure black on a dark canvas and pure white on a light one are kept too:
//    adapt_series_color_to_theme() draws those in the theme foreground.
//  * Any other slot keeps its OKLCh hue and chroma (chroma is reduced only as
//    far as the sRGB gamut requires) and only its OKLab lightness moves away
//    from the canvas (lighter on dark canvases, darker on light ones) until
//    the contrast reaches @p min_contrast.
//  * Distinctness: adjusted slots are placed in order of their original
//    contrast, least deficient first.  A slot that would land within OKLab
//    distance 0.04 of an already placed slot, under normal vision or
//    simulated protanopia, deuteranopia or tritanopia, moves further from the
//    canvas in 0.25 contrast steps until it is that far apart (or as far apart
//    as the two were in the source palette, if that was closer).
// Sequential palettes (DataPalette::sequential, with OKLab lightness strictly
// monotonic over the non-ink slots) are handled as a ramp instead: every
// slot's OKLab lightness is scaled toward the ramp end farthest from the
// canvas (the anchor) by the largest common factor that brings all slots to
// @p min_contrast; if that would leave neighbours closer than the distinctness
// rule above allows, the anchor moves outward just enough to make room.  The
// lightness order of the ramp survives (hue and chroma are kept as above;
// passing slots may move too).
// Alpha is kept.  An empty palette is treated as palette::default_cycle.  Only
// auto series colours (and the palette swatches that preview them, see
// ThemeManager::displayed_data_palette) go through this: explicit RGB colours
// and colormaps (heatmap / surface sampling) are not changed.
DataPalette visible_data_palette(const DataPalette&    palette,
                                 const spectra::Color& canvas,
                                 float                 min_contrast = AUTO_SERIES_MIN_CONTRAST);

// Fallback display name for a theme id without a registered display_name:
// underscores become spaces and each word is capitalised ("my_theme" -> "My Theme").
std::string theme_display_name(std::string_view theme_id);

// Colour a figure's background actually renders with: the figure's own colour when
// FigureStyle::background_override is set, otherwise the theme's canvas colour ("auto").
spectra::Color effective_figure_background(const FigureStyle& style, const ThemeColors& colors);

// Convenience accessors
inline const ThemeColors& theme()
{
    return ThemeManager::instance().colors();
}
inline const DataPalette& data_palette()
{
    return ThemeManager::instance().current_data_palette();
}

// ─── Icon Alpha System ──────────────────────────────────────────────────────
// Returns the appropriate alpha for an icon given its interaction state.
// Active icons use accent color at full alpha; inactive icons are readable
// but deliberately quieter than labels.
inline float icon_alpha(bool active, bool hovered, bool disabled)
{
    if (disabled)
        return 0.34f;
    if (active)
        return 1.0f;
    if (hovered)
        return 0.92f;
    return 0.82f;
}

// ─── Hover Transition Helper ────────────────────────────────────────────────
// Smoothly interpolates a hover state float (0..1) each frame.
// Call once per widget per frame with dt and current is_hovered.
inline float smooth_hover_state(float current_t, float dt, bool is_hovered)
{
    float target = is_hovered ? 1.0f : 0.0f;
    float speed = is_hovered ? (1.0f / tokens::DURATION_HOVER) : (1.0f / 0.12f);   // 120ms fade-out
    float step  = speed * dt;
    if (current_t < target)
        return std::min(current_t + step, target);
    if (current_t > target)
        return std::max(current_t - step, target);
    return current_t;
}

// ─── Shell Semantic Helpers ─────────────────────────────────────────────────
// These helpers centralize the visual language for the app shell so header,
// tab bar, rail, and status bar share one set of rules instead of magic values.

// Readability-preserving alpha floors for shell surfaces and text.
inline float shell_surface_alpha(float requested)
{
    return std::max(requested, 0.55f);
}

inline float shell_text_alpha(float requested)
{
    return std::max(requested, 0.86f);
}

// Common shell interaction-state colors derived from the current theme.
inline Color control_surface_color(const ThemeColors& c, bool active, bool hovered, float t = 1.0f)
{
    if (active)
        return c.bg_tertiary.lerp(c.accent, 0.12f * t);
    if (hovered)
        return c.bg_tertiary.lerp(c.accent, 0.08f * t);
    return c.bg_secondary.lerp(c.bg_tertiary, 0.55f * t);
}

inline Color control_border_color(const ThemeColors& c, bool active, bool hovered, float t = 1.0f)
{
    if (active)
        return c.accent.lerp(c.border_strong, 0.25f * (1.0f - t));
    if (hovered)
        return c.border_default.lerp(c.accent, 0.35f * t);
    return c.border_subtle;
}

inline Color control_text_color(const ThemeColors& c, bool active, bool hovered)
{
    if (active)
        return c.accent_hover;
    if (hovered)
        return c.text_primary;
    return c.text_secondary;
}

// Soft glow color for active/selected shell controls. Intensity is controlled
// by the caller via alpha; this just returns the hue.
inline Color control_glow_color(const ThemeColors& c)
{
    if (c.accent_glow.a > 0.0f)
        return c.accent_glow;
    // Themes without a dedicated glow hue use their normal accent.
    return c.accent;
}

// ─── Contrast-checked UI colours ────────────────────────────────────────────

// True when the theme's app chrome (bg_primary) is light.
inline bool is_light_chrome(const ThemeColors& c)
{
    return c.bg_primary.luminance() > 0.4f;
}

// `fg` moved toward black or white (whichever side of `bg` it can gain contrast
// on) until its WCAG contrast with the opaque `bg` reaches `min_ratio`. Returned
// unchanged when it already does. The result keeps fg's alpha.
Color ensure_contrast(const Color& fg, const Color& bg, float min_ratio);

// Scrim ImGui draws behind modal dialogs (ImGuiCol_ModalWindowDimBg). Light
// chrome gets a stronger, slate-tinted scrim so the dialog stands apart.
Color modal_dim_color(const ThemeColors& c);

// Status-bar performance readouts. Text colours reach 4.5:1 against what they
// sit on (fps text against the pill, gpu text against the bar).
struct StatusPerfColors
{
    Color fps_fill;
    Color fps_border;
    Color fps_text;
    Color gpu_text;
};
StatusPerfColors status_perf_colors(const ThemeColors& c, bool fps_live, float glow_intensity);

// Empty-state / welcome action buttons, resolved against the opaque `backdrop`
// they are drawn on. Fills are opaque. Contract (per fill / hover / active
// state, guarded by UiShellButtons.ActionButtonsReachContrastTargets):
//  * text reaches 4.5:1 (WCAG text) against every fill state;
//  * primary: the filled shape itself is the button edge, a graphical object,
//    so each fill state reaches 3:1 (WCAG 1.4.11 non-text) against the
//    backdrop — not 4.5:1 (Light's accent fill is ~4.15:1 on bg_primary);
//  * secondary: the fill barely differs from the backdrop, so the outline
//    carries the edge and is held to 4.5:1 against the backdrop.
struct ActionButtonColors
{
    Color fill;
    Color hover;
    Color active;
    Color text;
    Color border;   // alpha 0 = no border
};
ActionButtonColors action_button_colors(const ThemeColors& c, bool primary, const Color& backdrop);

}   // namespace spectra::ui
