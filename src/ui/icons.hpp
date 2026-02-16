#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "design_tokens.hpp"
#include "theme.hpp"

struct ImVec4;  // Forward declaration for ImGui
struct ImFont;  // Forward declaration for ImGui

namespace spectra::ui
{

enum class Icon : uint16_t
{
    // Navigation icons
    ChartLine = 0xE001,
    ScatterChart = 0xE002,
    Axes = 0xE003,
    Wrench = 0xE004,
    Folder = 0xE005,
    Settings = 0xE006,
    Help = 0xE007,

    // Toolbar icons
    ZoomIn = 0xE008,
    Hand = 0xE009,
    Ruler = 0xE00A,
    Crosshair = 0xE00B,
    Pin = 0xE00C,
    Type = 0xE00D,

    // Action icons
    Export = 0xE00E,
    Save = 0xE00F,
    Copy = 0xE010,
    Undo = 0xE011,
    Redo = 0xE012,
    Search = 0xE013,
    Filter = 0xE014,

    // Status icons
    Check = 0xE015,
    Warning = 0xE016,
    Error = 0xE017,
    Info = 0xE018,

    // UI icons
    ChevronRight = 0xE019,
    ChevronDown = 0xE01A,
    Close = 0xE01B,
    Menu = 0xE01C,
    Maximize = 0xE01D,
    Minimize = 0xE01E,

    // Series icons
    Eye = 0xE01F,
    EyeOff = 0xE020,
    Palette = 0xE021,
    LineWidth = 0xE022,

    // Additional icons
    Plus = 0xE023,
    Minus = 0xE024,
    Play = 0xE025,
    Pause = 0xE026,
    Stop = 0xE027,
    StepForward = 0xE028,
    StepBackward = 0xE029,

    // Theme icons
    Sun = 0xE02A,
    Moon = 0xE02B,
    Contrast = 0xE02C,

    // Layout icons
    Layout = 0xE02D,
    SplitHorizontal = 0xE02E,
    SplitVertical = 0xE02F,
    Tab = 0xE030,

    // Data icons
    LineChart = 0xE031,
    BarChart = 0xE032,
    PieChart = 0xE033,
    Heatmap = 0xE034,

    // Transform icons
    ArrowUp = 0xE035,
    ArrowDown = 0xE036,
    ArrowLeft = 0xE037,
    ArrowRight = 0xE038,
    Refresh = 0xE039,

    // Misc
    Clock = 0xE03A,
    Calendar = 0xE03B,
    Tag = 0xE03C,
    Link = 0xE03D,
    Unlink = 0xE03E,
    Lock = 0xE03F,
    Unlock = 0xE040,

    // Command palette
    Command = 0xE041,
    Keyboard = 0xE042,
    Shortcut = 0xE043,

    // Workspace
    FolderOpen = 0xE044,
    File = 0xE045,
    FileText = 0xE046,

    // View modes
    Grid = 0xE047,
    List = 0xE048,
    Fullscreen = 0xE049,
    FullscreenExit = 0xE04A,

    // Editing
    Edit = 0xE04B,
    Trash = 0xE04C,
    Duplicate = 0xE04D,

    // Math/analysis
    Function = 0xE04E,
    Integral = 0xE04F,
    Sigma = 0xE050,
    Sqrt = 0xE051,

    // Markers
    Circle = 0xE052,
    Square = 0xE053,
    Triangle = 0xE054,
    Diamond = 0xE055,
    Cross = 0xE056,
    PlusMarker = 0xE057,
    MinusMarker = 0xE058,
    Asterisk = 0xE059,

    // Line styles
    LineSolid = 0xE05A,
    LineDashed = 0xE05B,
    LineDotted = 0xE05C,
    LineDashDot = 0xE05D,

    // Special
    Home = 0xE05E,
    Back = 0xE05F,
    Forward = 0xE060,
    Up = 0xE061,
    Down = 0xE062,

    // End marker
    Last = 0xE063
};

class IconFont
{
   public:
    static IconFont& instance();

    // Initialize the icon font (call once during app startup)
    bool initialize();
    bool is_initialized() const { return initialized_; }

    // Get ImGui font for icons
    ImFont* get_font(float size = tokens::ICON_MD) const;

    // Draw an icon at current cursor position
    void draw(Icon icon, float size = tokens::ICON_MD, const Color& color = {});
    void draw(Icon icon, float size, const ImVec4& color);

    // Get icon as string (for ImGui text rendering)
    const char* get_icon_string(Icon icon) const;

    // Get icon width for a given size
    float get_width(Icon icon, float size) const;

    // Check if icon exists
    bool has_icon(Icon icon) const;

    // Get all available icons (for debugging)
    const std::vector<Icon>& get_all_icons() const;

   private:
    IconFont() = default;

    bool initialized_ = false;
    ImFont* font_16_ = nullptr;
    ImFont* font_20_ = nullptr;
    ImFont* font_24_ = nullptr;
    ImFont* font_32_ = nullptr;

    // Icon to Unicode codepoint mapping
    std::unordered_map<Icon, uint32_t> icon_map_;

    // Unicode codepoint to string mapping
    std::unordered_map<uint32_t, std::string> codepoint_strings_;

    void build_icon_map();
};

// Convenience functions
inline void draw_icon(Icon icon, float size = tokens::ICON_MD, const Color& color = {})
{
    IconFont::instance().draw(icon, size, color);
}

inline void draw_icon(Icon icon, float size, const ImVec4& color)
{
    IconFont::instance().draw(icon, size, color);
}

inline const char* icon_str(Icon icon)
{
    return IconFont::instance().get_icon_string(icon);
}

inline ImFont* icon_font(float size = tokens::ICON_MD)
{
    return IconFont::instance().get_font(size);
}

// Icon helpers for common patterns
inline void draw_nav_icon(Icon icon, bool active = false)
{
    const auto& colors = theme();
    draw_icon(icon, tokens::ICON_LG, active ? colors.accent : colors.text_secondary);
}

inline void draw_toolbar_icon(Icon icon, bool active = false)
{
    const auto& colors = theme();
    draw_icon(icon, tokens::ICON_MD, active ? colors.accent : colors.text_primary);
}

inline void draw_status_icon(Icon icon, const Color& color = {})
{
    const auto& colors = theme();
    draw_icon(icon, tokens::ICON_SM, color.r > 0 ? color : colors.text_secondary);
}

}  // namespace spectra::ui
