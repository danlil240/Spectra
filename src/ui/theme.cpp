#include "theme.hpp"
#include "imgui.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace plotix::ui {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    if (instance.themes_.empty()) {
        instance.initialize_default_themes();
        instance.initialize_data_palettes();
        const bool has_imgui_context = (ImGui::GetCurrentContext() != nullptr);
        instance.set_theme(has_imgui_context ? "dark" : "light");
    }
    return instance;
}

void ThemeManager::register_theme(const std::string& name, Theme theme) {
    theme.name = name;
    themes_[name] = std::move(theme);
    if (!current_theme_) {
        set_theme(name);
    }
}

void ThemeManager::set_theme(const std::string& name) {
    auto it = themes_.find(name);
    if (it != themes_.end()) {
        current_theme_name_ = name;
        current_theme_ = &it->second;
        apply_to_imgui();
    }
}

const Theme& ThemeManager::current() const {
    static Theme fallback;
    return current_theme_ ? *current_theme_ : fallback;
}

const ThemeColors& ThemeManager::colors() const {
    return current().colors;
}

const std::string& ThemeManager::current_theme_name() const {
    return current_theme_name_;
}

void ThemeManager::set_data_palette(const std::string& palette_name) {
    auto it = data_palettes_.find(palette_name);
    if (it != data_palettes_.end()) {
        current_data_palette_name_ = palette_name;
        if (current_theme_) {
            current_theme_->data_palette = it->second;
        }
    }
}

const DataPalette& ThemeManager::current_data_palette() const {
    static DataPalette fallback;
    if (current_theme_) {
        return current_theme_->data_palette;
    }
    return fallback;
}

const std::vector<std::string>& ThemeManager::available_data_palettes() const {
    static std::vector<std::string> names;
    if (names.empty()) {
        for (const auto& pair : data_palettes_) {
            names.push_back(pair.first);
        }
    }
    return names;
}

void ThemeManager::apply_to_imgui() {
    if (!current_theme_ || ImGui::GetCurrentContext() == nullptr) return;
    
    auto& style = ImGui::GetStyle();
    const auto& colors = current_theme_->colors;
    
    // Window styling
    style.WindowPadding = ImVec2(tokens::SPACE_4, tokens::SPACE_4);
    style.WindowRounding = tokens::RADIUS_MD;
    style.WindowBorderSize = current_theme_->border_width;
    style.WindowMinSize = ImVec2(200, 100);
    
    // Frame styling
    style.FramePadding = ImVec2(tokens::SPACE_3, tokens::SPACE_2);
    style.FrameRounding = tokens::RADIUS_SM;
    style.FrameBorderSize = 0.0f;
    
    // Item spacing
    style.ItemSpacing = ImVec2(tokens::SPACE_3, tokens::SPACE_3);
    style.ItemInnerSpacing = ImVec2(tokens::SPACE_2, tokens::SPACE_2);
    
    // Indent and separator
    style.IndentSpacing = tokens::SPACE_6;
    style.SeparatorTextBorderSize = current_theme_->border_width;
    style.SeparatorTextAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextPadding = ImVec2(tokens::SPACE_4, tokens::SPACE_2);
    
    // Scrollbar
    style.ScrollbarSize = tokens::SPACE_3;
    style.ScrollbarRounding = tokens::RADIUS_PILL;
    
    // Grab
    style.GrabMinSize = tokens::SPACE_3;
    style.GrabRounding = tokens::RADIUS_SM;
    
    // Tab
    style.TabRounding = tokens::RADIUS_SM;
    style.TabBorderSize = current_theme_->border_width;
    style.TabMinWidthForCloseButton = 0.0f;
    
    // Button
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
    
    // Display safe area padding
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);
    
    // Apply colors
    ImVec4* imgui_colors = style.Colors;
    
    // Window and background
    imgui_colors[ImGuiCol_WindowBg] = ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, current_theme_->opacity_panel);
    imgui_colors[ImGuiCol_ChildBg] = ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, 1.0f);
    imgui_colors[ImGuiCol_PopupBg] = ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, current_theme_->opacity_tooltip);
    imgui_colors[ImGuiCol_Border] = ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    
    // Text
    imgui_colors[ImGuiCol_Text] = ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f);
    imgui_colors[ImGuiCol_TextDisabled] = ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 1.0f);
    
    // Frame backgrounds
    imgui_colors[ImGuiCol_FrameBg] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_FrameBgHovered] = ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_FrameBgActive] = ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);
    
    // Titles
    imgui_colors[ImGuiCol_TitleBg] = ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f);
    imgui_colors[ImGuiCol_TitleBgActive] = ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f);
    imgui_colors[ImGuiCol_TitleBgCollapsed] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    
    // Menu
    imgui_colors[ImGuiCol_MenuBarBg] = ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, current_theme_->opacity_panel);
    
    // Scrollbar
    imgui_colors[ImGuiCol_ScrollbarBg] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrab] = ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    
    // Check mark
    imgui_colors[ImGuiCol_CheckMark] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    
    // Buttons
    imgui_colors[ImGuiCol_Button] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_ButtonHovered] = ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_ButtonActive] = ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);
    
    // Header
    imgui_colors[ImGuiCol_Header] = ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_HeaderHovered] = ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);
    imgui_colors[ImGuiCol_HeaderActive] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    
    // Separator
    imgui_colors[ImGuiCol_Separator] = ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_SeparatorHovered] = ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_SeparatorActive] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    
    // Resize grip
    imgui_colors[ImGuiCol_ResizeGrip] = ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_ResizeGripHovered] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_ResizeGripActive] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    
    // Tabs
    imgui_colors[ImGuiCol_Tab] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_TabHovered] = ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_TabSelected] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_TabDimmed] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_TabDimmedSelected] = ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);
    
    // Plot lines (for ImGui plot widgets)
    imgui_colors[ImGuiCol_PlotLines] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_PlotLinesHovered] = ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f);
    imgui_colors[ImGuiCol_PlotHistogram] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_PlotHistogramHovered] = ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f);
    
    // Table headers
    imgui_colors[ImGuiCol_TableHeaderBg] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_TableBorderStrong] = ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_TableBorderLight] = ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    imgui_colors[ImGuiCol_TableRowBgAlt] = ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.5f);
    
    // Drag and drop
    imgui_colors[ImGuiCol_DragDropTarget] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    
    // Navigation
    imgui_colors[ImGuiCol_NavHighlight] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_NavWindowingHighlight] = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.3f);
    
    // Modal
    imgui_colors[ImGuiCol_ModalWindowDimBg] = ImVec4(colors.bg_overlay.r, colors.bg_overlay.g, colors.bg_overlay.b, 0.5f);
}

void ThemeManager::apply_to_renderer(Renderer& renderer) {
    if (!current_theme_) return;
    
    // Update renderer with theme-aware plot colors
    // This would require the renderer to have methods to set these colors
    // For now, this is a placeholder for the concept
    
    // The renderer would need to be updated to use:
    // - colors.bg_primary for canvas background
    // - colors.grid_line for grid lines
    // - colors.axis_line for axis lines
    // - colors.tick_label for tick labels
    // - colors.crosshair for crosshair
    // - colors.selection_fill/border for selections
    
    (void)renderer; // Suppress unused parameter warning
}

void ThemeManager::transition_to(const std::string& name, float duration_sec) {
    auto it = themes_.find(name);
    if (it != themes_.end() && current_theme_) {
        transitioning_ = true;
        transition_time_ = 0.0f;
        transition_duration_ = duration_sec;
        transition_start_colors_ = current_theme_->colors;
        transition_target_colors_ = it->second.colors;
        transition_target_name_ = name;
    }
}

void ThemeManager::update(float dt) {
    if (!transitioning_ || !current_theme_) return;
    
    transition_time_ += dt;
    float t = std::min(transition_time_ / transition_duration_, 1.0f);
    
    // Use ease-in-out curve
    t = t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    
    current_theme_->colors = interpolate_colors(transition_start_colors_, transition_target_colors_, t);
    apply_to_imgui();
    
    if (transition_time_ >= transition_duration_) {
        transitioning_ = false;
        current_theme_name_ = transition_target_name_;
        current_theme_->colors = transition_target_colors_;
    }
}

bool ThemeManager::is_transitioning() const {
    return transitioning_;
}

Color ThemeManager::get_color(const std::string& color_name) const {
    // Simple color name lookup for convenience
    if (!current_theme_) return Color();
    
    const auto& c = current_theme_->colors;
    if (color_name == "accent") return c.accent;
    if (color_name == "accent_hover") return c.accent_hover;
    if (color_name == "text_primary") return c.text_primary;
    if (color_name == "text_secondary") return c.text_secondary;
    if (color_name == "bg_primary") return c.bg_primary;
    if (color_name == "bg_secondary") return c.bg_secondary;
    if (color_name == "bg_tertiary") return c.bg_tertiary;
    if (color_name == "border_default") return c.border_default;
    if (color_name == "border_subtle") return c.border_subtle;
    if (color_name == "success") return c.success;
    if (color_name == "warning") return c.warning;
    if (color_name == "error") return c.error;
    if (color_name == "info") return c.info;
    
    return Color(); // Return transparent if not found
}

Color ThemeManager::lerp_color(const std::string& color_name, const Color& target, float t) const {
    return get_color(color_name).lerp(target, t);
}

void ThemeManager::initialize_default_themes() {
    // Dark theme (default)
    Theme dark;
    dark.name = "dark";
    dark.colors = {
        // Surfaces
        .bg_primary = Color::from_hex(0x0D1117),
        .bg_secondary = Color::from_hex(0x161B22),
        .bg_tertiary = Color::from_hex(0x1C2128),
        .bg_elevated = Color::from_hex(0x2D333B),
        .bg_overlay = Color::from_hex(0x80000000),
        
        // Text
        .text_primary = Color::from_hex(0xE6EDF3),
        .text_secondary = Color::from_hex(0x8B949E),
        .text_tertiary = Color::from_hex(0x484F58),
        .text_inverse = Color::from_hex(0x0D1117),
        
        // Borders
        .border_default = Color::from_hex(0x30363D),
        .border_subtle = Color::from_hex(0x21262D),
        .border_strong = Color::from_hex(0x6E7681),
        
        // Interactive
        .accent = Color::from_hex(0x58A6FF),
        .accent_hover = Color::from_hex(0x79C0FF),
        .accent_muted = Color::from_hex(0x4D1F6FEB),
        .accent_subtle = Color::from_hex(0x1A1F6FEB),
        
        // Semantic
        .success = Color::from_hex(0x3FB950),
        .warning = Color::from_hex(0xD29922),
        .error = Color::from_hex(0xF85149),
        .info = Color::from_hex(0x58A6FF),
        
        // Plot-specific
        .grid_line = Color::from_hex(0x21262D),
        .axis_line = Color::from_hex(0x30363D),
        .tick_label = Color::from_hex(0x8B949E),
        .crosshair = Color::from_hex(0xB358A6FF),
        .selection_fill = Color::from_hex(0x3358A6FF),
        .selection_border = Color::from_hex(0x58A6FF),
        .tooltip_bg = Color::from_hex(0x2D333B),
        .tooltip_border = Color::from_hex(0x30363D)
    };
    register_theme("dark", dark);
    
    // Light theme
    Theme light;
    light.name = "light";
    light.colors = {
        // Surfaces
        .bg_primary = Color::from_hex(0xFFFFFF),
        .bg_secondary = Color::from_hex(0xF6F8FA),
        .bg_tertiary = Color::from_hex(0xF0F2F5),
        .bg_elevated = Color::from_hex(0xFFFFFF),
        .bg_overlay = Color::from_hex(0x4D000000),
        
        // Text
        .text_primary = Color::from_hex(0x1F2328),
        .text_secondary = Color::from_hex(0x656D76),
        .text_tertiary = Color::from_hex(0x6E7781),
        .text_inverse = Color::from_hex(0xFFFFFF),
        
        // Borders
        .border_default = Color::from_hex(0xD0D7DE),
        .border_subtle = Color::from_hex(0xE8ECF0),
        .border_strong = Color::from_hex(0x8C959F),
        
        // Interactive
        .accent = Color::from_hex(0x0969DA),
        .accent_hover = Color::from_hex(0x0860CA),
        .accent_muted = Color::from_hex(0x260969DA),
        .accent_subtle = Color::from_hex(0x0D0969DA),
        
        // Semantic
        .success = Color::from_hex(0x1A7F37),
        .warning = Color::from_hex(0x9A6700),
        .error = Color::from_hex(0xD1242F),
        .info = Color::from_hex(0x0969DA),
        
        // Plot-specific
        .grid_line = Color::from_hex(0xE8ECF0),
        .axis_line = Color::from_hex(0xD0D7DE),
        .tick_label = Color::from_hex(0x656D76),
        .crosshair = Color::from_hex(0xB30969DA),
        .selection_fill = Color::from_hex(0x260969DA),
        .selection_border = Color::from_hex(0x0969DA),
        .tooltip_bg = Color::from_hex(0xFFFFFF),
        .tooltip_border = Color::from_hex(0xD0D7DE)
    };
    register_theme("light", light);
    
    // High contrast theme
    Theme high_contrast;
    high_contrast.name = "high_contrast";
    high_contrast.colors = {
        // Surfaces
        .bg_primary = Color::from_hex(0x000000),
        .bg_secondary = Color::from_hex(0x1C1C1C),
        .bg_tertiary = Color::from_hex(0x2D2D2D),
        .bg_elevated = Color::from_hex(0x3D3D3D),
        .bg_overlay = Color::from_hex(0xCC000000),
        
        // Text
        .text_primary = Color::from_hex(0xFFFFFF),
        .text_secondary = Color::from_hex(0xE0E0E0),
        .text_tertiary = Color::from_hex(0xB0B0B0),
        .text_inverse = Color::from_hex(0x000000),
        
        // Borders
        .border_default = Color::from_hex(0xFFFFFF),
        .border_subtle = Color::from_hex(0xCCCCCC),
        .border_strong = Color::from_hex(0xFFFFFF),
        
        // Interactive
        .accent = Color::from_hex(0xFFD700),
        .accent_hover = Color::from_hex(0xFFED4E),
        .accent_muted = Color::from_hex(0x4DFFD700),
        .accent_subtle = Color::from_hex(0x1AFFD700),
        
        // Semantic
        .success = Color::from_hex(0x00FF00),
        .warning = Color::from_hex(0xFFFF00),
        .error = Color::from_hex(0xFF0000),
        .info = Color::from_hex(0xFFD700),
        
        // Plot-specific
        .grid_line = Color::from_hex(0x666666),
        .axis_line = Color::from_hex(0xFFFFFF),
        .tick_label = Color::from_hex(0xFFFFFF),
        .crosshair = Color::from_hex(0xCCFFD700),
        .selection_fill = Color::from_hex(0x4DFFD700),
        .selection_border = Color::from_hex(0xFFD700),
        .tooltip_bg = Color::from_hex(0x1C1C1C),
        .tooltip_border = Color::from_hex(0xFFFFFF)
    };
    register_theme("high_contrast", high_contrast);
}

void ThemeManager::initialize_data_palettes() {
    // Default palette (perceptually uniform)
    DataPalette default_palette;
    default_palette.name = "default";
    default_palette.colorblind_safe = false;
    default_palette.colors = {
        Color::from_hex(0x4E79A7),  // steel blue
        Color::from_hex(0xF28E2B),  // orange
        Color::from_hex(0xE15759),  // red
        Color::from_hex(0x76B7B2),  // teal
        Color::from_hex(0x59A14F),  // green
        Color::from_hex(0xEDC948),  // gold
        Color::from_hex(0xB07AA1),  // purple
        Color::from_hex(0xFF9DA7),  // pink
        Color::from_hex(0x9C755F),  // brown
        Color::from_hex(0xBAB0AC)   // gray
    };
    data_palettes_["default"] = default_palette;
    
    // Colorblind-safe palette (Okabe-Ito)
    DataPalette colorblind_palette;
    colorblind_palette.name = "colorblind";
    colorblind_palette.colorblind_safe = true;
    colorblind_palette.colors = {
        Color::from_hex(0xE69F00),  // orange
        Color::from_hex(0x56B4E9),  // sky blue
        Color::from_hex(0x009E73),  // bluish green
        Color::from_hex(0xF0E442),  // yellow
        Color::from_hex(0x0072B2),  // blue
        Color::from_hex(0xD55E00),  // vermillion
        Color::from_hex(0xCC79A7),  // reddish purple
        Color::from_hex(0x000000)   // black
    };
    data_palettes_["colorblind"] = colorblind_palette;
    
    // Set default palette
    current_data_palette_name_ = "default";
}

ThemeColors ThemeManager::interpolate_colors(const ThemeColors& start, const ThemeColors& end, float t) const {
    ThemeColors result;
    result.bg_primary = start.bg_primary.lerp(end.bg_primary, t);
    result.bg_secondary = start.bg_secondary.lerp(end.bg_secondary, t);
    result.bg_tertiary = start.bg_tertiary.lerp(end.bg_tertiary, t);
    result.bg_elevated = start.bg_elevated.lerp(end.bg_elevated, t);
    result.bg_overlay = start.bg_overlay.lerp(end.bg_overlay, t);
    
    result.text_primary = start.text_primary.lerp(end.text_primary, t);
    result.text_secondary = start.text_secondary.lerp(end.text_secondary, t);
    result.text_tertiary = start.text_tertiary.lerp(end.text_tertiary, t);
    result.text_inverse = start.text_inverse.lerp(end.text_inverse, t);
    
    result.border_default = start.border_default.lerp(end.border_default, t);
    result.border_subtle = start.border_subtle.lerp(end.border_subtle, t);
    result.border_strong = start.border_strong.lerp(end.border_strong, t);
    
    result.accent = start.accent.lerp(end.accent, t);
    result.accent_hover = start.accent_hover.lerp(end.accent_hover, t);
    result.accent_muted = start.accent_muted.lerp(end.accent_muted, t);
    result.accent_subtle = start.accent_subtle.lerp(end.accent_subtle, t);
    
    result.success = start.success.lerp(end.success, t);
    result.warning = start.warning.lerp(end.warning, t);
    result.error = start.error.lerp(end.error, t);
    result.info = start.info.lerp(end.info, t);
    
    result.grid_line = start.grid_line.lerp(end.grid_line, t);
    result.axis_line = start.axis_line.lerp(end.axis_line, t);
    result.tick_label = start.tick_label.lerp(end.tick_label, t);
    result.crosshair = start.crosshair.lerp(end.crosshair, t);
    result.selection_fill = start.selection_fill.lerp(end.selection_fill, t);
    result.selection_border = start.selection_border.lerp(end.selection_border, t);
    result.tooltip_bg = start.tooltip_bg.lerp(end.tooltip_bg, t);
    result.tooltip_border = start.tooltip_border.lerp(end.tooltip_border, t);
    
    return result;
}

} // namespace plotix::ui
