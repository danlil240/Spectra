#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "design_tokens.hpp"

struct ImVec4; // Forward declaration for ImGui

namespace plotix::ui {

// 32-bit color structure (RGBA)
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    
    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
    
    // From hex (0xRRGGBB or 0xAARRGGBB)
    static constexpr Color from_hex(uint32_t hex) {
        if (hex > 0xFFFFFF) {
            // ARGB format - convert to RGBA
            return Color(
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f,
                ((hex >> 24) & 0xFF) / 255.0f
            );
        } else {
            // RGB format
            return Color(
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f,
                1.0f
            );
        }
    }
    
    constexpr uint32_t to_hex() const {
        return (uint32_t(r * 255.0f) << 24) |
               (uint32_t(g * 255.0f) << 16) |
               (uint32_t(b * 255.0f) << 8) |
               uint32_t(a * 255.0f);
    }
    
    constexpr Color with_alpha(float alpha) const {
        return Color(r, g, b, alpha);
    }
    
    constexpr Color lerp(const Color& other, float t) const {
        return Color(
            r + (other.r - r) * t,
            g + (other.g - g) * t,
            b + (other.b - b) * t,
            a + (other.a - a) * t
        );
    }
};

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

struct DataPalette {
    std::string name;
    std::vector<Color> colors;
    bool colorblind_safe = false;
};

struct Theme {
    std::string name;
    ThemeColors colors;
    DataPalette data_palette;
    
    // Visual properties
    float opacity_panel = 0.95f;       // Panel background opacity (for blur effect)
    float opacity_tooltip = 0.98f;     // Tooltip background opacity
    float shadow_intensity = 1.0f;     // Shadow alpha multiplier
    float border_width = tokens::BORDER_WIDTH_NORMAL;  // Default border width
    bool use_blur = true;              // Enable backdrop blur effects
    
    // Animation settings
    float animation_speed = 1.0f;      // Global animation speed multiplier
    bool enable_animations = true;     // Master animation toggle
};

class ThemeManager {
public:
    static ThemeManager& instance();
    
    // Theme registration and switching
    void register_theme(const std::string& name, Theme theme);
    void set_theme(const std::string& name);
    const Theme& current() const;
    const ThemeColors& colors() const;
    const std::string& current_theme_name() const;
    
    // Data palette management
    void set_data_palette(const std::string& palette_name);
    const DataPalette& current_data_palette() const;
    const std::vector<std::string>& available_data_palettes() const;
    
    // Theme application
    void apply_to_imgui();  // Updates all ImGui style colors
    void apply_to_renderer(class Renderer& renderer);  // Updates plot colors
    
    // Animated theme transition
    void transition_to(const std::string& name, float duration_sec = tokens::DURATION_SLOW);
    void update(float dt);  // Update ongoing transitions
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
    std::string current_theme_name_ = "dark";
    Theme* current_theme_ = nullptr;
    
    std::unordered_map<std::string, DataPalette> data_palettes_;
    std::string current_data_palette_name_ = "default";
    
    // Transition state
    bool transitioning_ = false;
    float transition_time_ = 0.0f;
    float transition_duration_ = 0.0f;
    ThemeColors transition_start_colors_;
    ThemeColors transition_target_colors_;
    std::string transition_target_name_;
    
    void initialize_default_themes();
    void initialize_data_palettes();
    ThemeColors interpolate_colors(const ThemeColors& start, const ThemeColors& end, float t) const;
};

// Convenience accessor
inline const ThemeColors& theme() { return ThemeManager::instance().colors(); }

} // namespace plotix::ui
