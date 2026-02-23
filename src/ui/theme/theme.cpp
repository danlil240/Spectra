#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "imgui.h"

namespace spectra::ui
{

ThemeManager& ThemeManager::instance()
{
    static ThemeManager instance;
    if (instance.themes_.empty())
    {
        instance.initialize_default_themes();
        instance.initialize_data_palettes();
        const bool has_imgui_context = (ImGui::GetCurrentContext() != nullptr);
        instance.set_theme(has_imgui_context ? "dark" : "light");
    }
    return instance;
}

void ThemeManager::register_theme(const std::string& name, Theme theme)
{
    theme.name    = name;
    themes_[name] = std::move(theme);
    if (!current_theme_)
    {
        set_theme(name);
    }
}

void ThemeManager::set_theme(const std::string& name)
{
    auto it = themes_.find(name);
    if (it != themes_.end())
    {
        current_theme_name_ = name;
        current_theme_      = &it->second;
        apply_to_imgui();
    }
}

const Theme& ThemeManager::current() const
{
    static Theme fallback;
    return current_theme_ ? *current_theme_ : fallback;
}

const ThemeColors& ThemeManager::colors() const
{
    if (display_colors_valid_)
        return display_colors_;
    return current().colors;
}

const std::string& ThemeManager::current_theme_name() const
{
    return current_theme_name_;
}

void ThemeManager::set_data_palette(const std::string& palette_name)
{
    auto it = data_palettes_.find(palette_name);
    if (it != data_palettes_.end())
    {
        current_data_palette_name_ = palette_name;
        if (current_theme_)
        {
            current_theme_->data_palette = it->second;
        }
        display_palette_valid_ = false;
        palette_transitioning_ = false;
    }
}

void ThemeManager::register_data_palette(const std::string& name, DataPalette palette)
{
    palette.name         = name;
    data_palettes_[name] = std::move(palette);
    palette_names_dirty_ = true;
}

const DataPalette& ThemeManager::current_data_palette() const
{
    static DataPalette fallback;
    if (display_palette_valid_)
        return display_palette_;
    if (current_theme_)
    {
        return current_theme_->data_palette;
    }
    return fallback;
}

const DataPalette& ThemeManager::get_data_palette(const std::string& name) const
{
    static DataPalette fallback;
    auto               it = data_palettes_.find(name);
    if (it != data_palettes_.end())
        return it->second;
    return fallback;
}

const std::vector<std::string>& ThemeManager::available_data_palettes() const
{
    if (palette_names_dirty_)
    {
        palette_names_cache_.clear();
        for (const auto& pair : data_palettes_)
        {
            palette_names_cache_.push_back(pair.first);
        }
        std::sort(palette_names_cache_.begin(), palette_names_cache_.end());
        palette_names_dirty_ = false;
    }
    return palette_names_cache_;
}

void ThemeManager::transition_palette(const std::string& palette_name, float duration_sec)
{
    auto it = data_palettes_.find(palette_name);
    if (it == data_palettes_.end())
        return;
    if (duration_sec <= 0.0f)
    {
        set_data_palette(palette_name);
        return;
    }

    const auto& current_pal         = current_data_palette();
    palette_start_colors_           = current_pal.colors;
    palette_target_colors_          = it->second.colors;
    palette_transition_target_name_ = palette_name;
    palette_transition_time_        = 0.0f;
    palette_transition_duration_    = duration_sec;
    palette_transitioning_          = true;

    // Initialize display palette from target metadata
    display_palette_        = it->second;
    display_palette_.colors = palette_start_colors_;
    display_palette_valid_  = true;
}

bool ThemeManager::is_palette_transitioning() const
{
    return palette_transitioning_;
}

void ThemeManager::apply_to_imgui()
{
    if (!current_theme_ || ImGui::GetCurrentContext() == nullptr)
        return;

    auto&       style  = ImGui::GetStyle();
    const auto& colors = current_theme_->colors;

    // ── Modern 2026 styling ──────────────────────────────────────────────
    style.AntiAliasedLines       = true;
    style.AntiAliasedFill        = true;
    style.AntiAliasedLinesUseTex = true;

    // Window styling — generous rounding, subtle borders
    style.WindowPadding    = ImVec2(tokens::SPACE_4, tokens::SPACE_4);
    style.WindowRounding   = tokens::RADIUS_LG;
    style.WindowBorderSize = 0.5f;
    style.WindowMinSize    = ImVec2(32, 32);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    // Frame styling — pill-like inputs and controls
    style.FramePadding    = ImVec2(tokens::SPACE_3, tokens::SPACE_2 + 2.0f);
    style.FrameRounding   = tokens::RADIUS_MD;
    style.FrameBorderSize = 0.0f;

    // Item spacing — breathing room
    style.ItemSpacing      = ImVec2(tokens::SPACE_3, tokens::SPACE_2 + 2.0f);
    style.ItemInnerSpacing = ImVec2(tokens::SPACE_2, tokens::SPACE_2);

    // Indent and separator
    style.IndentSpacing           = tokens::SPACE_6;
    style.SeparatorTextBorderSize = 0.5f;
    style.SeparatorTextAlign      = ImVec2(0.5f, 0.5f);
    style.SeparatorTextPadding    = ImVec2(tokens::SPACE_5, tokens::SPACE_2);

    // Scrollbar — thin, pill-shaped, modern
    style.ScrollbarSize     = 6.0f;
    style.ScrollbarRounding = tokens::RADIUS_PILL;

    // Grab — rounded slider handles
    style.GrabMinSize  = tokens::SPACE_4;
    style.GrabRounding = tokens::RADIUS_PILL;

    // Tab — rounded top corners
    style.TabRounding               = tokens::RADIUS_MD;
    style.TabBorderSize             = 0.0f;
    style.TabMinWidthForCloseButton = 0.0f;
    style.TabBarBorderSize          = 0.5f;

    // Popup — elevated, rounded
    style.PopupRounding   = tokens::RADIUS_LG;
    style.PopupBorderSize = 0.5f;

    // Child window
    style.ChildRounding   = tokens::RADIUS_MD;
    style.ChildBorderSize = 0.0f;

    // Button
    style.ButtonTextAlign     = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    // Tooltip — modern rounded
    style.HoverStationaryDelay = 0.3f;
    style.HoverDelayShort      = 0.15f;
    style.HoverDelayNormal     = 0.4f;

    // Display safe area padding
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);

    // Apply colors
    ImVec4* imgui_colors = style.Colors;

    // Window and background
    imgui_colors[ImGuiCol_WindowBg] = ImVec4(colors.bg_secondary.r,
                                             colors.bg_secondary.g,
                                             colors.bg_secondary.b,
                                             current_theme_->opacity_panel);
    imgui_colors[ImGuiCol_ChildBg] =
        ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, 1.0f);
    imgui_colors[ImGuiCol_PopupBg] = ImVec4(colors.bg_elevated.r,
                                            colors.bg_elevated.g,
                                            colors.bg_elevated.b,
                                            current_theme_->opacity_tooltip);
    imgui_colors[ImGuiCol_Border] =
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    // Text
    imgui_colors[ImGuiCol_Text] =
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f);
    imgui_colors[ImGuiCol_TextDisabled] =
        ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 1.0f);

    // Frame backgrounds
    imgui_colors[ImGuiCol_FrameBg] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_FrameBgHovered] =
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_FrameBgActive] =
        ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);

    // Titles
    imgui_colors[ImGuiCol_TitleBg] =
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f);
    imgui_colors[ImGuiCol_TitleBgActive] =
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f);
    imgui_colors[ImGuiCol_TitleBgCollapsed] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);

    // Menu
    imgui_colors[ImGuiCol_MenuBarBg] = ImVec4(colors.bg_secondary.r,
                                              colors.bg_secondary.g,
                                              colors.bg_secondary.b,
                                              current_theme_->opacity_panel);

    // Scrollbar
    imgui_colors[ImGuiCol_ScrollbarBg] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrab] =
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrabHovered] =
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrabActive] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);

    // Check mark
    imgui_colors[ImGuiCol_CheckMark] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);

    // Buttons
    imgui_colors[ImGuiCol_Button] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_ButtonHovered] =
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_ButtonActive] =
        ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);

    // Header
    imgui_colors[ImGuiCol_Header] =
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_HeaderHovered] =
        ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);
    imgui_colors[ImGuiCol_HeaderActive] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);

    // Separator
    imgui_colors[ImGuiCol_Separator] =
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_SeparatorHovered] =
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_SeparatorActive] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);

    // Resize grip
    imgui_colors[ImGuiCol_ResizeGrip] =
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_ResizeGripHovered] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_ResizeGripActive] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);

    // Tabs
    imgui_colors[ImGuiCol_Tab] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_TabHovered] =
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_TabSelected] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_TabDimmed] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_TabDimmedSelected] =
        ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 1.0f);

    // Plot lines (for ImGui plot widgets)
    imgui_colors[ImGuiCol_PlotLines] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_PlotLinesHovered] =
        ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f);
    imgui_colors[ImGuiCol_PlotHistogram] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_PlotHistogramHovered] =
        ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f);

    // Table headers
    imgui_colors[ImGuiCol_TableHeaderBg] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f);
    imgui_colors[ImGuiCol_TableBorderStrong] =
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 1.0f);
    imgui_colors[ImGuiCol_TableBorderLight] =
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 1.0f);
    imgui_colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    imgui_colors[ImGuiCol_TableRowBgAlt] =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.5f);

    // Drag and drop
    imgui_colors[ImGuiCol_DragDropTarget] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);

    // Navigation
    imgui_colors[ImGuiCol_NavHighlight] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_NavWindowingHighlight] =
        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    imgui_colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.3f);

    // Modal
    imgui_colors[ImGuiCol_ModalWindowDimBg] =
        ImVec4(colors.bg_overlay.r, colors.bg_overlay.g, colors.bg_overlay.b, 0.5f);
}

void ThemeManager::apply_to_renderer(Renderer& renderer)
{
    if (!current_theme_)
        return;

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

    (void)renderer;   // Suppress unused parameter warning
}

void ThemeManager::transition_to(const std::string& name, float duration_sec)
{
    auto it = themes_.find(name);
    if (it != themes_.end() && current_theme_)
    {
        transitioning_           = true;
        transition_time_         = 0.0f;
        transition_duration_     = duration_sec;
        transition_start_colors_ = colors();   // Use display colors (may already be mid-transition)
        transition_target_colors_ = it->second.colors;
        transition_target_name_   = name;
        display_colors_valid_     = true;
        display_colors_           = transition_start_colors_;
    }
}

void ThemeManager::update(float dt)
{
    // Update theme transition
    if (transitioning_)
    {
        transition_time_ += dt;
        float t = std::min(transition_time_ / transition_duration_, 1.0f);

        // Use ease-in-out curve
        t = t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        display_colors_ =
            interpolate_colors(transition_start_colors_, transition_target_colors_, t);
        display_colors_valid_ = true;
        apply_to_imgui();

        if (transition_time_ >= transition_duration_)
        {
            transitioning_        = false;
            display_colors_valid_ = false;
            // Switch to target theme (stored data is pristine)
            set_theme(transition_target_name_);
        }
    }

    // Update palette transition
    if (palette_transitioning_)
    {
        palette_transition_time_ += dt;
        float t = std::min(palette_transition_time_ / palette_transition_duration_, 1.0f);
        t       = t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        // Interpolate palette colors
        size_t count = std::min(palette_start_colors_.size(), palette_target_colors_.size());
        display_palette_.colors.resize(
            std::max(palette_start_colors_.size(), palette_target_colors_.size()));
        for (size_t i = 0; i < count; ++i)
        {
            display_palette_.colors[i] =
                palette_start_colors_[i].lerp(palette_target_colors_[i], t);
        }
        // Colors beyond the shorter palette fade in/out
        for (size_t i = count; i < palette_target_colors_.size(); ++i)
        {
            display_palette_.colors[i] = palette_target_colors_[i].with_alpha(t);
        }
        display_palette_valid_ = true;

        if (palette_transition_time_ >= palette_transition_duration_)
        {
            palette_transitioning_ = false;
            display_palette_valid_ = false;
            set_data_palette(palette_transition_target_name_);
        }
    }
}

bool ThemeManager::is_transitioning() const
{
    return transitioning_;
}

Color ThemeManager::get_color(const std::string& color_name) const
{
    // Simple color name lookup for convenience
    if (!current_theme_)
        return Color();

    const auto& c = current_theme_->colors;
    if (color_name == "accent")
        return c.accent;
    if (color_name == "accent_hover")
        return c.accent_hover;
    if (color_name == "text_primary")
        return c.text_primary;
    if (color_name == "text_secondary")
        return c.text_secondary;
    if (color_name == "bg_primary")
        return c.bg_primary;
    if (color_name == "bg_secondary")
        return c.bg_secondary;
    if (color_name == "bg_tertiary")
        return c.bg_tertiary;
    if (color_name == "border_default")
        return c.border_default;
    if (color_name == "border_subtle")
        return c.border_subtle;
    if (color_name == "success")
        return c.success;
    if (color_name == "warning")
        return c.warning;
    if (color_name == "error")
        return c.error;
    if (color_name == "info")
        return c.info;

    return Color();   // Return transparent if not found
}

Color ThemeManager::lerp_color(const std::string& color_name, const Color& target, float t) const
{
    return get_color(color_name).lerp(target, t);
}

// ─── JSON helpers (minimal, no external deps) ────────────────────────────────

namespace
{

std::string color_to_json(const Color& c)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[%.6f, %.6f, %.6f, %.6f]", c.r, c.g, c.b, c.a);
    return buf;
}

std::string escape_json_string(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char ch : s)
    {
        if (ch == '"')
            out += "\\\"";
        else if (ch == '\\')
            out += "\\\\";
        else if (ch == '\n')
            out += "\\n";
        else
            out += ch;
    }
    out += '"';
    return out;
}

std::string theme_colors_to_json(const ThemeColors& c, int indent = 4)
{
    std::string        pad(indent, ' ');
    std::ostringstream os;
    os << "{\n";
#define TC_FIELD(name) \
    os << pad << "  " << escape_json_string(#name) << ": " << color_to_json(c.name)
    TC_FIELD(bg_primary) << ",\n";
    TC_FIELD(bg_secondary) << ",\n";
    TC_FIELD(bg_tertiary) << ",\n";
    TC_FIELD(bg_elevated) << ",\n";
    TC_FIELD(bg_overlay) << ",\n";
    TC_FIELD(text_primary) << ",\n";
    TC_FIELD(text_secondary) << ",\n";
    TC_FIELD(text_tertiary) << ",\n";
    TC_FIELD(text_inverse) << ",\n";
    TC_FIELD(border_default) << ",\n";
    TC_FIELD(border_subtle) << ",\n";
    TC_FIELD(border_strong) << ",\n";
    TC_FIELD(accent) << ",\n";
    TC_FIELD(accent_hover) << ",\n";
    TC_FIELD(accent_muted) << ",\n";
    TC_FIELD(accent_subtle) << ",\n";
    TC_FIELD(success) << ",\n";
    TC_FIELD(warning) << ",\n";
    TC_FIELD(error) << ",\n";
    TC_FIELD(info) << ",\n";
    TC_FIELD(grid_line) << ",\n";
    TC_FIELD(axis_line) << ",\n";
    TC_FIELD(tick_label) << ",\n";
    TC_FIELD(crosshair) << ",\n";
    TC_FIELD(selection_fill) << ",\n";
    TC_FIELD(selection_border) << ",\n";
    TC_FIELD(tooltip_bg) << ",\n";
    TC_FIELD(tooltip_border) << "\n";
#undef TC_FIELD
    os << pad << "}";
    return os.str();
}

bool parse_float_array(const std::string& s, size_t& pos, float* out, int count)
{
    while (pos < s.size() && s[pos] != '[')
        ++pos;
    if (pos >= s.size())
        return false;
    ++pos;
    for (int i = 0; i < count; ++i)
    {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == ','))
            ++pos;
        char* end = nullptr;
        out[i]    = std::strtof(s.c_str() + pos, &end);
        pos       = static_cast<size_t>(end - s.c_str());
    }
    while (pos < s.size() && s[pos] != ']')
        ++pos;
    if (pos < s.size())
        ++pos;
    return true;
}

Color parse_color_array(const std::string& s, size_t& pos)
{
    float v[4] = {0, 0, 0, 1};
    parse_float_array(s, pos, v, 4);
    return Color(v[0], v[1], v[2], v[3]);
}

std::string extract_string_value(const std::string& s, size_t pos)
{
    size_t q1 = s.find('"', pos);
    if (q1 == std::string::npos)
        return "";
    size_t q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return "";
    return s.substr(q1 + 1, q2 - q1 - 1);
}

bool parse_theme_colors_from_json(const std::string& json, ThemeColors& out)
{
#define PARSE_TC(name)                             \
    {                                              \
        size_t p = json.find("\"" #name "\"");     \
        if (p != std::string::npos)                \
        {                                          \
            p += strlen("\"" #name "\"");          \
            out.name = parse_color_array(json, p); \
        }                                          \
    }
    PARSE_TC(bg_primary);
    PARSE_TC(bg_secondary);
    PARSE_TC(bg_tertiary);
    PARSE_TC(bg_elevated);
    PARSE_TC(bg_overlay);
    PARSE_TC(text_primary);
    PARSE_TC(text_secondary);
    PARSE_TC(text_tertiary);
    PARSE_TC(text_inverse);
    PARSE_TC(border_default);
    PARSE_TC(border_subtle);
    PARSE_TC(border_strong);
    PARSE_TC(accent);
    PARSE_TC(accent_hover);
    PARSE_TC(accent_muted);
    PARSE_TC(accent_subtle);
    PARSE_TC(success);
    PARSE_TC(warning);
    PARSE_TC(error);
    PARSE_TC(info);
    PARSE_TC(grid_line);
    PARSE_TC(axis_line);
    PARSE_TC(tick_label);
    PARSE_TC(crosshair);
    PARSE_TC(selection_fill);
    PARSE_TC(selection_border);
    PARSE_TC(tooltip_bg);
    PARSE_TC(tooltip_border);
#undef PARSE_TC
    return true;
}

}   // anonymous namespace

bool ThemeManager::export_theme(const std::string& path) const
{
    if (!current_theme_)
        return false;

    std::ofstream f(path);
    if (!f.is_open())
        return false;

    const auto& t = *current_theme_;
    f << "{\n";
    f << "  \"name\": " << escape_json_string(t.name) << ",\n";
    f << "  \"version\": 1,\n";
    f << "  \"colors\": " << theme_colors_to_json(t.colors, 2) << ",\n";
    f << "  \"opacity_panel\": " << t.opacity_panel << ",\n";
    f << "  \"opacity_tooltip\": " << t.opacity_tooltip << ",\n";
    f << "  \"shadow_intensity\": " << t.shadow_intensity << ",\n";
    f << "  \"border_width\": " << t.border_width << ",\n";
    f << "  \"animation_speed\": " << t.animation_speed << ",\n";
    f << "  \"enable_animations\": " << (t.enable_animations ? "true" : "false") << ",\n";
    f << "  \"use_blur\": " << (t.use_blur ? "true" : "false") << ",\n";

    f << "  \"data_palette\": {\n";
    f << "    \"name\": " << escape_json_string(t.data_palette.name) << ",\n";
    f << "    \"colorblind_safe\": " << (t.data_palette.colorblind_safe ? "true" : "false")
      << ",\n";
    f << "    \"colors\": [\n";
    for (size_t i = 0; i < t.data_palette.colors.size(); ++i)
    {
        f << "      " << color_to_json(t.data_palette.colors[i]);
        if (i + 1 < t.data_palette.colors.size())
            f << ",";
        f << "\n";
    }
    f << "    ]\n";
    f << "  }\n";
    f << "}\n";

    return f.good();
}

bool ThemeManager::import_theme(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;

    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (json.empty())
        return false;

    size_t name_pos = json.find("\"name\"");
    if (name_pos == std::string::npos)
        return false;
    std::string name = extract_string_value(json, name_pos + 6);
    if (name.empty())
        return false;

    Theme theme;
    theme.name = name;

    size_t colors_pos = json.find("\"colors\"");
    if (colors_pos != std::string::npos)
    {
        size_t brace = json.find('{', colors_pos + 8);
        if (brace != std::string::npos)
        {
            int    depth = 1;
            size_t end   = brace + 1;
            while (end < json.size() && depth > 0)
            {
                if (json[end] == '{')
                    ++depth;
                else if (json[end] == '}')
                    --depth;
                ++end;
            }
            std::string colors_json = json.substr(brace, end - brace);
            parse_theme_colors_from_json(colors_json, theme.colors);
        }
    }

    auto parse_float = [&](const char* key, float& out)
    {
        std::string needle = std::string("\"") + key + "\"";
        size_t      p      = json.find(needle);
        if (p != std::string::npos)
        {
            p = json.find(':', p);
            if (p != std::string::npos)
            {
                char* end = nullptr;
                float v   = std::strtof(json.c_str() + p + 1, &end);
                if (end != json.c_str() + p + 1)
                    out = v;
            }
        }
    };
    auto parse_bool = [&](const char* key, bool& out)
    {
        std::string needle = std::string("\"") + key + "\"";
        size_t      p      = json.find(needle);
        if (p != std::string::npos)
        {
            size_t t_pos = json.find("true", p);
            size_t f_pos = json.find("false", p);
            if (t_pos != std::string::npos && (f_pos == std::string::npos || t_pos < f_pos))
                out = true;
            else
                out = false;
        }
    };

    parse_float("opacity_panel", theme.opacity_panel);
    parse_float("opacity_tooltip", theme.opacity_tooltip);
    parse_float("shadow_intensity", theme.shadow_intensity);
    parse_float("border_width", theme.border_width);
    parse_float("animation_speed", theme.animation_speed);
    parse_bool("enable_animations", theme.enable_animations);
    parse_bool("use_blur", theme.use_blur);

    register_theme(name, std::move(theme));
    return true;
}

void ThemeManager::save_current_as_default()
{
    if (default_theme_path_.empty())
    {
        const char* home = std::getenv("HOME");
        if (!home)
            home = std::getenv("USERPROFILE");
        if (home)
        {
            default_theme_path_ = std::string(home) + "/.spectra/default_theme.json";
        }
    }
    if (!default_theme_path_.empty())
    {
        auto dir = std::filesystem::path(default_theme_path_).parent_path();
        std::filesystem::create_directories(dir);
        export_theme(default_theme_path_);
    }
}

void ThemeManager::load_default()
{
    if (default_theme_path_.empty())
    {
        const char* home = std::getenv("HOME");
        if (!home)
            home = std::getenv("USERPROFILE");
        if (home)
        {
            default_theme_path_ = std::string(home) + "/.spectra/default_theme.json";
        }
    }
    if (!default_theme_path_.empty() && std::filesystem::exists(default_theme_path_))
    {
        if (import_theme(default_theme_path_))
        {
            std::ifstream lf(default_theme_path_);
            std::string   json((std::istreambuf_iterator<char>(lf)),
                             std::istreambuf_iterator<char>());
            size_t        np = json.find("\"name\"");
            if (np != std::string::npos)
            {
                std::string n = extract_string_value(json, np + 6);
                if (!n.empty())
                    set_theme(n);
            }
        }
    }
}

void ThemeManager::initialize_default_themes()
{
    // Dark theme (default)
    Theme dark;
    dark.name   = "dark";
    dark.colors = {// Surfaces
                   .bg_primary   = Color::from_hex(0x0D1117),
                   .bg_secondary = Color::from_hex(0x161B22),
                   .bg_tertiary  = Color::from_hex(0x1C2128),
                   .bg_elevated  = Color::from_hex(0x2D333B),
                   .bg_overlay   = Color::from_hex(0x80000000),

                   // Text
                   .text_primary   = Color::from_hex(0xE6EDF3),
                   .text_secondary = Color::from_hex(0x8B949E),
                   .text_tertiary  = Color::from_hex(0x484F58),
                   .text_inverse   = Color::from_hex(0x0D1117),

                   // Borders
                   .border_default = Color::from_hex(0x30363D),
                   .border_subtle  = Color::from_hex(0x21262D),
                   .border_strong  = Color::from_hex(0x6E7681),

                   // Interactive
                   .accent        = Color::from_hex(0x58A6FF),
                   .accent_hover  = Color::from_hex(0x79C0FF),
                   .accent_muted  = Color::from_hex(0x4D1F6FEB),
                   .accent_subtle = Color::from_hex(0x1A1F6FEB),

                   // Semantic
                   .success = Color::from_hex(0x3FB950),
                   .warning = Color::from_hex(0xD29922),
                   .error   = Color::from_hex(0xF85149),
                   .info    = Color::from_hex(0x58A6FF),

                   // Plot-specific
                   .grid_line        = Color(1.0f, 1.0f, 1.0f, 0.15f),
                   .axis_line        = Color(0.55f, 0.58f, 0.63f, 0.65f),
                   .tick_label       = Color::from_hex(0x8B949E),
                   .crosshair        = Color::from_hex(0xB358A6FF),
                   .selection_fill   = Color::from_hex(0x3358A6FF),
                   .selection_border = Color::from_hex(0x58A6FF),
                   .tooltip_bg       = Color::from_hex(0x2D333B),
                   .tooltip_border   = Color::from_hex(0x30363D)};
    register_theme("dark", dark);

    // Light theme
    Theme light;
    light.name   = "light";
    light.colors = {// Surfaces
                    .bg_primary   = Color::from_hex(0xFFFFFF),
                    .bg_secondary = Color::from_hex(0xF6F8FA),
                    .bg_tertiary  = Color::from_hex(0xF0F2F5),
                    .bg_elevated  = Color::from_hex(0xFFFFFF),
                    .bg_overlay   = Color::from_hex(0x4D000000),

                    // Text
                    .text_primary   = Color::from_hex(0x1F2328),
                    .text_secondary = Color::from_hex(0x656D76),
                    .text_tertiary  = Color::from_hex(0x6E7781),
                    .text_inverse   = Color::from_hex(0xFFFFFF),

                    // Borders
                    .border_default = Color::from_hex(0xD0D7DE),
                    .border_subtle  = Color::from_hex(0xE8ECF0),
                    .border_strong  = Color::from_hex(0x8C959F),

                    // Interactive
                    .accent        = Color::from_hex(0x0969DA),
                    .accent_hover  = Color::from_hex(0x0860CA),
                    .accent_muted  = Color::from_hex(0x260969DA),
                    .accent_subtle = Color::from_hex(0x0D0969DA),

                    // Semantic
                    .success = Color::from_hex(0x1A7F37),
                    .warning = Color::from_hex(0x9A6700),
                    .error   = Color::from_hex(0xD1242F),
                    .info    = Color::from_hex(0x0969DA),

                    // Plot-specific
                    .grid_line        = Color(0.0f, 0.0f, 0.0f, 0.12f),
                    .axis_line        = Color(0.30f, 0.33f, 0.38f, 0.70f),
                    .tick_label       = Color::from_hex(0x656D76),
                    .crosshair        = Color::from_hex(0xB30969DA),
                    .selection_fill   = Color::from_hex(0x260969DA),
                    .selection_border = Color::from_hex(0x0969DA),
                    .tooltip_bg       = Color::from_hex(0xFFFFFF),
                    .tooltip_border   = Color::from_hex(0xD0D7DE)};
    register_theme("light", light);

    // High contrast theme
    Theme high_contrast;
    high_contrast.name   = "high_contrast";
    high_contrast.colors = {// Surfaces
                            .bg_primary   = Color::from_hex(0x000000),
                            .bg_secondary = Color::from_hex(0x1C1C1C),
                            .bg_tertiary  = Color::from_hex(0x2D2D2D),
                            .bg_elevated  = Color::from_hex(0x3D3D3D),
                            .bg_overlay   = Color::from_hex(0xCC000000),

                            // Text
                            .text_primary   = Color::from_hex(0xFFFFFF),
                            .text_secondary = Color::from_hex(0xE0E0E0),
                            .text_tertiary  = Color::from_hex(0xB0B0B0),
                            .text_inverse   = Color::from_hex(0x000000),

                            // Borders
                            .border_default = Color::from_hex(0xFFFFFF),
                            .border_subtle  = Color::from_hex(0xCCCCCC),
                            .border_strong  = Color::from_hex(0xFFFFFF),

                            // Interactive
                            .accent        = Color::from_hex(0xFFD700),
                            .accent_hover  = Color::from_hex(0xFFED4E),
                            .accent_muted  = Color::from_hex(0x4DFFD700),
                            .accent_subtle = Color::from_hex(0x1AFFD700),

                            // Semantic
                            .success = Color::from_hex(0x00FF00),
                            .warning = Color::from_hex(0xFFFF00),
                            .error   = Color::from_hex(0xFF0000),
                            .info    = Color::from_hex(0xFFD700),

                            // Plot-specific
                            .grid_line        = Color::from_hex(0x666666),
                            .axis_line        = Color::from_hex(0xFFFFFF),
                            .tick_label       = Color::from_hex(0xFFFFFF),
                            .crosshair        = Color::from_hex(0xCCFFD700),
                            .selection_fill   = Color::from_hex(0x4DFFD700),
                            .selection_border = Color::from_hex(0xFFD700),
                            .tooltip_bg       = Color::from_hex(0x1C1C1C),
                            .tooltip_border   = Color::from_hex(0xFFFFFF)};
    register_theme("high_contrast", high_contrast);
}

void ThemeManager::initialize_data_palettes()
{
    // Default palette (Tableau 10 — perceptually uniform)
    DataPalette default_palette;
    default_palette.name            = "default";
    default_palette.description     = "Tableau 10 — perceptually balanced for general use";
    default_palette.colorblind_safe = false;
    default_palette.colors          = {
                 Color::from_hex(0x4E79A7),   // steel blue
                 Color::from_hex(0xF28E2B),   // orange
                 Color::from_hex(0xE15759),   // red
                 Color::from_hex(0x76B7B2),   // teal
                 Color::from_hex(0x59A14F),   // green
                 Color::from_hex(0xEDC948),   // gold
                 Color::from_hex(0xB07AA1),   // purple
                 Color::from_hex(0xFF9DA7),   // pink
                 Color::from_hex(0x9C755F),   // brown
                 Color::from_hex(0xBAB0AC)    // gray
    };
    data_palettes_["default"] = default_palette;

    // Okabe-Ito — the gold standard for colorblind-safe palettes
    DataPalette okabe_ito;
    okabe_ito.name            = "colorblind";
    okabe_ito.description     = "Okabe-Ito — universally safe for all CVD types";
    okabe_ito.colorblind_safe = true;
    okabe_ito.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia};
    okabe_ito.colors          = {
                 Color::from_hex(0xE69F00),   // orange
                 Color::from_hex(0x56B4E9),   // sky blue
                 Color::from_hex(0x009E73),   // bluish green
                 Color::from_hex(0xF0E442),   // yellow
                 Color::from_hex(0x0072B2),   // blue
                 Color::from_hex(0xD55E00),   // vermillion
                 Color::from_hex(0xCC79A7),   // reddish purple
                 Color::from_hex(0x000000)    // black
    };
    data_palettes_["colorblind"] = okabe_ito;

    // Tol Bright — Paul Tol's bright qualitative scheme
    DataPalette tol_bright;
    tol_bright.name            = "tol_bright";
    tol_bright.description     = "Paul Tol Bright — vivid, CVD-safe qualitative palette";
    tol_bright.colorblind_safe = true;
    tol_bright.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia};
    tol_bright.colors          = {
                 Color::from_hex(0x4477AA),   // blue
                 Color::from_hex(0xEE6677),   // red
                 Color::from_hex(0x228833),   // green
                 Color::from_hex(0xCCBB44),   // yellow
                 Color::from_hex(0x66CCEE),   // cyan
                 Color::from_hex(0xAA3377),   // purple
                 Color::from_hex(0xBBBBBB)    // grey
    };
    data_palettes_["tol_bright"] = tol_bright;

    // Tol Muted — Paul Tol's muted qualitative scheme
    DataPalette tol_muted;
    tol_muted.name            = "tol_muted";
    tol_muted.description     = "Paul Tol Muted — softer tones, CVD-safe";
    tol_muted.colorblind_safe = true;
    tol_muted.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia};
    tol_muted.colors          = {
                 Color::from_hex(0x332288),   // indigo
                 Color::from_hex(0x88CCEE),   // cyan
                 Color::from_hex(0x44AA99),   // teal
                 Color::from_hex(0x117733),   // green
                 Color::from_hex(0x999933),   // olive
                 Color::from_hex(0xDDCC77),   // sand
                 Color::from_hex(0xCC6677),   // rose
                 Color::from_hex(0x882255),   // wine
                 Color::from_hex(0xAA4499)    // purple
    };
    data_palettes_["tol_muted"] = tol_muted;

    // IBM Design — accessible palette from IBM's design system
    DataPalette ibm;
    ibm.name            = "ibm";
    ibm.description     = "IBM Design Language — enterprise-grade accessible palette";
    ibm.colorblind_safe = true;
    ibm.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia};
    ibm.colors          = {
                 Color::from_hex(0x648FFF),   // ultramarine
                 Color::from_hex(0x785EF0),   // indigo
                 Color::from_hex(0xDC267F),   // magenta
                 Color::from_hex(0xFE6100),   // orange
                 Color::from_hex(0xFFB000),   // gold
    };
    data_palettes_["ibm"] = ibm;

    // Wong — Bang Wong's Nature Methods palette
    DataPalette wong;
    wong.name            = "wong";
    wong.description     = "Bang Wong (Nature Methods) — optimized for scientific figures";
    wong.colorblind_safe = true;
    wong.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia};
    wong.colors          = {
                 Color::from_hex(0x000000),   // black
                 Color::from_hex(0xE69F00),   // orange
                 Color::from_hex(0x56B4E9),   // sky blue
                 Color::from_hex(0x009E73),   // bluish green
                 Color::from_hex(0xF0E442),   // yellow
                 Color::from_hex(0x0072B2),   // blue
                 Color::from_hex(0xD55E00),   // vermillion
                 Color::from_hex(0xCC79A7)    // reddish purple
    };
    data_palettes_["wong"] = wong;

    // Viridis-inspired discrete palette (perceptually uniform, CVD-safe)
    DataPalette viridis;
    viridis.name            = "viridis";
    viridis.description     = "Viridis-inspired discrete — perceptually uniform, print-safe";
    viridis.colorblind_safe = true;
    viridis.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia};
    viridis.colors          = {
                 Color::from_hex(0x440154),   // deep purple
                 Color::from_hex(0x482878),   // purple
                 Color::from_hex(0x3E4989),   // blue-purple
                 Color::from_hex(0x31688E),   // blue
                 Color::from_hex(0x26828E),   // teal-blue
                 Color::from_hex(0x1F9E89),   // teal
                 Color::from_hex(0x35B779),   // green
                 Color::from_hex(0x6DCD59),   // lime
                 Color::from_hex(0xB4DE2C),   // yellow-green
                 Color::from_hex(0xFDE725)    // yellow
    };
    data_palettes_["viridis"] = viridis;

    // High-contrast monochrome (for achromatopsia / grayscale printing)
    DataPalette mono;
    mono.name            = "monochrome";
    mono.description     = "Monochrome — grayscale-safe, works for total color blindness";
    mono.colorblind_safe = true;
    mono.safe_for        = {CVDType::Protanopia,
                            CVDType::Deuteranopia,
                            CVDType::Tritanopia,
                            CVDType::Achromatopsia};
    mono.colors          = {
                 Color::from_hex(0x000000),   // black
                 Color::from_hex(0x404040),   // dark gray
                 Color::from_hex(0x808080),   // mid gray
                 Color::from_hex(0xB0B0B0),   // light gray
                 Color::from_hex(0xD0D0D0),   // very light gray
    };
    data_palettes_["monochrome"] = mono;

    palette_names_dirty_       = true;
    current_data_palette_name_ = "default";
}

// ─── CVD Simulation ──────────────────────────────────────────────────────────
// Uses Brettel/Viénot/Mollon (1997) simulation matrices for dichromacy.
// These are the standard 3x3 linear-RGB transformation matrices.

Color simulate_cvd(const Color& c, CVDType type)
{
    if (type == CVDType::None)
        return c;

    // Work in linear RGB
    Color lin = c.to_linear();
    float r = lin.r, g = lin.g, b = lin.b;
    float out_r, out_g, out_b;

    switch (type)
    {
        case CVDType::Protanopia:
            // Viénot et al. 1999 protanopia simulation
            out_r = 0.152286f * r + 1.052583f * g - 0.204868f * b;
            out_g = 0.114503f * r + 0.786281f * g + 0.099216f * b;
            out_b = -0.003882f * r - 0.048116f * g + 1.051998f * b;
            break;
        case CVDType::Deuteranopia:
            // Viénot et al. 1999 deuteranopia simulation
            out_r = 0.367322f * r + 0.860646f * g - 0.227968f * b;
            out_g = 0.280085f * r + 0.672501f * g + 0.047413f * b;
            out_b = -0.011820f * r + 0.042940f * g + 0.968881f * b;
            break;
        case CVDType::Tritanopia:
            // Brettel et al. 1997 tritanopia simulation
            out_r = 1.255528f * r - 0.076749f * g - 0.178779f * b;
            out_g = -0.078411f * r + 0.930809f * g + 0.147602f * b;
            out_b = 0.004733f * r + 0.691367f * g + 0.303900f * b;
            break;
        case CVDType::Achromatopsia:
        {
            // Total color blindness — convert to luminance
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            out_r = out_g = out_b = lum;
            break;
        }
        default:
            return c;
    }

    // Clamp and convert back to sRGB
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    return Color(clamp01(out_r), clamp01(out_g), clamp01(out_b), c.a).to_srgb();
}

ThemeColors ThemeManager::interpolate_colors(const ThemeColors& start,
                                             const ThemeColors& end,
                                             float              t) const
{
    ThemeColors result;
    result.bg_primary   = start.bg_primary.lerp(end.bg_primary, t);
    result.bg_secondary = start.bg_secondary.lerp(end.bg_secondary, t);
    result.bg_tertiary  = start.bg_tertiary.lerp(end.bg_tertiary, t);
    result.bg_elevated  = start.bg_elevated.lerp(end.bg_elevated, t);
    result.bg_overlay   = start.bg_overlay.lerp(end.bg_overlay, t);

    result.text_primary   = start.text_primary.lerp(end.text_primary, t);
    result.text_secondary = start.text_secondary.lerp(end.text_secondary, t);
    result.text_tertiary  = start.text_tertiary.lerp(end.text_tertiary, t);
    result.text_inverse   = start.text_inverse.lerp(end.text_inverse, t);

    result.border_default = start.border_default.lerp(end.border_default, t);
    result.border_subtle  = start.border_subtle.lerp(end.border_subtle, t);
    result.border_strong  = start.border_strong.lerp(end.border_strong, t);

    result.accent        = start.accent.lerp(end.accent, t);
    result.accent_hover  = start.accent_hover.lerp(end.accent_hover, t);
    result.accent_muted  = start.accent_muted.lerp(end.accent_muted, t);
    result.accent_subtle = start.accent_subtle.lerp(end.accent_subtle, t);

    result.success = start.success.lerp(end.success, t);
    result.warning = start.warning.lerp(end.warning, t);
    result.error   = start.error.lerp(end.error, t);
    result.info    = start.info.lerp(end.info, t);

    result.grid_line        = start.grid_line.lerp(end.grid_line, t);
    result.axis_line        = start.axis_line.lerp(end.axis_line, t);
    result.tick_label       = start.tick_label.lerp(end.tick_label, t);
    result.crosshair        = start.crosshair.lerp(end.crosshair, t);
    result.selection_fill   = start.selection_fill.lerp(end.selection_fill, t);
    result.selection_border = start.selection_border.lerp(end.selection_border, t);
    result.tooltip_bg       = start.tooltip_bg.lerp(end.tooltip_bg, t);
    result.tooltip_border   = start.tooltip_border.lerp(end.tooltip_border, t);

    return result;
}

}   // namespace spectra::ui
