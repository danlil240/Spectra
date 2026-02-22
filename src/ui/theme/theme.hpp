#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "design_tokens.hpp"

struct ImVec4;   // Forward declaration for ImGui

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
        else
        {
            // RGB format
            return Color(((hex >> 16) & 0xFF) / 255.0f,
                         ((hex >> 8) & 0xFF) / 255.0f,
                         (hex & 0xFF) / 255.0f,
                         1.0f);
        }
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

    // sRGB relative luminance (BT.709)
    constexpr float luminance() const
    {
        // Linearize sRGB components
        auto linearize = [](float c) constexpr->float
        {
            return (c <= 0.04045f)
                       ? c / 12.92f
                       :
                       // Approximate pow((c + 0.055) / 1.055, 2.4) with a polynomial
                       // For constexpr compatibility, use a simple gamma 2.2 approximation
                       ((c + 0.055f) / 1.055f) * ((c + 0.055f) / 1.055f)
                           * ((c + 0.055f) / 1.055f > 0.5f ? 1.16f : 0.87f);
        };
        return 0.2126f * linearize(r) + 0.7152f * linearize(g) + 0.0722f * linearize(b);
    }

    // WCAG 2.1 contrast ratio (1:1 to 21:1)
    constexpr float contrast_ratio(const Color& other) const
    {
        float l1      = luminance();
        float l2      = other.luminance();
        float lighter = (l1 > l2) ? l1 : l2;
        float darker  = (l1 > l2) ? l2 : l1;
        return (lighter + 0.05f) / (darker + 0.05f);
    }

    // Convert sRGB to linear RGB
    Color to_linear() const
    {
        auto lin = [](float c) -> float
        { return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
        return Color(lin(r), lin(g), lin(b), a);
    }

    // Convert linear RGB to sRGB
    Color to_srgb() const
    {
        auto srgb = [](float c) -> float
        { return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; };
        return Color(srgb(r), srgb(g), srgb(b), a);
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
    Color bg_primary;     // Main canvas background
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

struct Theme
{
    std::string name;
    ThemeColors colors;
    DataPalette data_palette;

    // Visual properties
    float opacity_panel    = 0.95f;   // Panel background opacity (for blur effect)
    float opacity_tooltip  = 0.98f;   // Tooltip background opacity
    float shadow_intensity = 1.0f;    // Shadow alpha multiplier
    float border_width     = tokens::BORDER_WIDTH_NORMAL;   // Default border width
    bool  use_blur         = true;                          // Enable backdrop blur effects

    // Animation settings
    float animation_speed   = 1.0f;   // Global animation speed multiplier
    bool  enable_animations = true;   // Master animation toggle
};

class ThemeManager
{
   public:
    static ThemeManager& instance();

    // Theme registration and switching
    void               register_theme(const std::string& name, Theme theme);
    void               set_theme(const std::string& name);
    const Theme&       current() const;
    const ThemeColors& colors() const;
    const std::string& current_theme_name() const;

    // Data palette management
    void               set_data_palette(const std::string& palette_name);
    void               register_data_palette(const std::string& name, DataPalette palette);
    const DataPalette& current_data_palette() const;
    const DataPalette& get_data_palette(const std::string& name) const;
    const std::vector<std::string>& available_data_palettes() const;
    const std::string& current_data_palette_name() const { return current_data_palette_name_; }

    // Animated palette transition
    void transition_palette(const std::string& palette_name,
                            float              duration_sec = tokens::DURATION_SLOW);
    bool is_palette_transitioning() const;

    // Theme application
    void apply_to_imgui();                              // Updates all ImGui style colors
    void apply_to_renderer(class Renderer& renderer);   // Updates plot colors

    // Animated theme transition
    void transition_to(const std::string& name, float duration_sec = tokens::DURATION_SLOW);
    void update(float dt);   // Update ongoing transitions
    bool is_transitioning() const;

    // Theme persistence
    bool export_theme(const std::string& path) const;
    bool import_theme(const std::string& path);
    void save_current_as_default();
    void load_default();

    // Utility
    Color get_color(const std::string& color_name) const;
    Color lerp_color(const std::string& color_name, const Color& target, float t) const;

   private:
    ThemeManager() = default;

    std::unordered_map<std::string, Theme> themes_;
    std::string                            current_theme_name_ = "dark";
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

    // Default theme persistence
    std::string default_theme_path_;

    void        initialize_default_themes();
    void        initialize_data_palettes();
    ThemeColors interpolate_colors(const ThemeColors& start, const ThemeColors& end, float t) const;
};

// Convenience accessors
inline const ThemeColors& theme()
{
    return ThemeManager::instance().colors();
}
inline const DataPalette& data_palette()
{
    return ThemeManager::instance().current_data_palette();
}

}   // namespace spectra::ui
