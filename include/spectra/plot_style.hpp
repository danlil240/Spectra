#pragma once

#include <cstdint>
#include <optional>
#include <spectra/color.hpp>
#include <string>
#include <string_view>

namespace spectra
{

// ─── Line Styles ─────────────────────────────────────────────────────────────
// Matches MATLAB line style specifiers exactly.

enum class LineStyle : uint8_t
{
    None,        // No line (markers only)
    Solid,       // '-'   ────────────
    Dashed,      // '--'  ── ── ── ──
    Dotted,      // ':'   ··············
    DashDot,     // '-.'  ──·──·──·──
    DashDotDot,  // '-..' ──··──··──··
};

// ─── Marker Styles ───────────────────────────────────────────────────────────
// Matches MATLAB marker specifiers. Superset of MATLAB's set.

enum class MarkerStyle : uint8_t
{
    None,              // No marker
    Point,             // '.'  small dot
    Circle,            // 'o'  ○
    Plus,              // '+'  +
    Cross,             // 'x'  ×
    Star,              // '*'  ✱ (six-pointed)
    Square,            // 's'  □
    Diamond,           // 'd'  ◇
    TriangleUp,        // '^'  △
    TriangleDown,      // 'v'  ▽
    TriangleLeft,      // '<'  ◁
    TriangleRight,     // '>'  ▷
    Pentagon,          // 'p'  ⬠
    Hexagon,           // 'h'  ⬡
    FilledCircle,      // 'O'  ●  (non-MATLAB extension)
    FilledSquare,      // 'S'  ■  (non-MATLAB extension)
    FilledDiamond,     // 'D'  ◆  (non-MATLAB extension)
    FilledTriangleUp,  // 'A'  ▲  (non-MATLAB extension)
};

// ─── Plot Style ──────────────────────────────────────────────────────────────
// Unified style combining line, marker, color, and sizing.

struct PlotStyle
{
    LineStyle line_style = LineStyle::Solid;
    MarkerStyle marker_style = MarkerStyle::None;
    std::optional<Color> color;  // nullopt = use auto-cycle color
    float line_width = 2.0f;
    float marker_size = 6.0f;
    float opacity = 1.0f;

    // Convenience: does this style draw lines?
    bool has_line() const { return line_style != LineStyle::None; }

    // Convenience: does this style draw markers?
    bool has_marker() const { return marker_style != MarkerStyle::None; }
};

// ─── String Conversions ──────────────────────────────────────────────────────

constexpr const char* line_style_name(LineStyle s)
{
    switch (s)
    {
        case LineStyle::None:
            return "None";
        case LineStyle::Solid:
            return "Solid";
        case LineStyle::Dashed:
            return "Dashed";
        case LineStyle::Dotted:
            return "Dotted";
        case LineStyle::DashDot:
            return "Dash-Dot";
        case LineStyle::DashDotDot:
            return "Dash-Dot-Dot";
    }
    return "Unknown";
}

constexpr const char* line_style_symbol(LineStyle s)
{
    switch (s)
    {
        case LineStyle::None:
            return "";
        case LineStyle::Solid:
            return "-";
        case LineStyle::Dashed:
            return "--";
        case LineStyle::Dotted:
            return ":";
        case LineStyle::DashDot:
            return "-.";
        case LineStyle::DashDotDot:
            return "-..";
    }
    return "";
}

constexpr const char* marker_style_name(MarkerStyle s)
{
    switch (s)
    {
        case MarkerStyle::None:
            return "None";
        case MarkerStyle::Point:
            return "Point";
        case MarkerStyle::Circle:
            return "Circle";
        case MarkerStyle::Plus:
            return "Plus";
        case MarkerStyle::Cross:
            return "Cross";
        case MarkerStyle::Star:
            return "Star";
        case MarkerStyle::Square:
            return "Square";
        case MarkerStyle::Diamond:
            return "Diamond";
        case MarkerStyle::TriangleUp:
            return "Triangle Up";
        case MarkerStyle::TriangleDown:
            return "Triangle Down";
        case MarkerStyle::TriangleLeft:
            return "Triangle Left";
        case MarkerStyle::TriangleRight:
            return "Triangle Right";
        case MarkerStyle::Pentagon:
            return "Pentagon";
        case MarkerStyle::Hexagon:
            return "Hexagon";
        case MarkerStyle::FilledCircle:
            return "Filled Circle";
        case MarkerStyle::FilledSquare:
            return "Filled Square";
        case MarkerStyle::FilledDiamond:
            return "Filled Diamond";
        case MarkerStyle::FilledTriangleUp:
            return "Filled Triangle Up";
    }
    return "Unknown";
}

constexpr char marker_style_symbol(MarkerStyle s)
{
    switch (s)
    {
        case MarkerStyle::None:
            return '\0';
        case MarkerStyle::Point:
            return '.';
        case MarkerStyle::Circle:
            return 'o';
        case MarkerStyle::Plus:
            return '+';
        case MarkerStyle::Cross:
            return 'x';
        case MarkerStyle::Star:
            return '*';
        case MarkerStyle::Square:
            return 's';
        case MarkerStyle::Diamond:
            return 'd';
        case MarkerStyle::TriangleUp:
            return '^';
        case MarkerStyle::TriangleDown:
            return 'v';
        case MarkerStyle::TriangleLeft:
            return '<';
        case MarkerStyle::TriangleRight:
            return '>';
        case MarkerStyle::Pentagon:
            return 'p';
        case MarkerStyle::Hexagon:
            return 'h';
        case MarkerStyle::FilledCircle:
            return 'O';
        case MarkerStyle::FilledSquare:
            return 'S';
        case MarkerStyle::FilledDiamond:
            return 'D';
        case MarkerStyle::FilledTriangleUp:
            return 'A';
    }
    return '\0';
}

// Total count of each enum (useful for UI iteration)
constexpr int LINE_STYLE_COUNT = 6;
constexpr int MARKER_STYLE_COUNT = 18;

// Arrays for iteration
constexpr LineStyle ALL_LINE_STYLES[] = {
    LineStyle::None,
    LineStyle::Solid,
    LineStyle::Dashed,
    LineStyle::Dotted,
    LineStyle::DashDot,
    LineStyle::DashDotDot,
};

constexpr MarkerStyle ALL_MARKER_STYLES[] = {
    MarkerStyle::None,
    MarkerStyle::Point,
    MarkerStyle::Circle,
    MarkerStyle::Plus,
    MarkerStyle::Cross,
    MarkerStyle::Star,
    MarkerStyle::Square,
    MarkerStyle::Diamond,
    MarkerStyle::TriangleUp,
    MarkerStyle::TriangleDown,
    MarkerStyle::TriangleLeft,
    MarkerStyle::TriangleRight,
    MarkerStyle::Pentagon,
    MarkerStyle::Hexagon,
    MarkerStyle::FilledCircle,
    MarkerStyle::FilledSquare,
    MarkerStyle::FilledDiamond,
    MarkerStyle::FilledTriangleUp,
};

// ─── Dash Pattern ────────────────────────────────────────────────────────────
// Returns {dash_length, gap_length, dot_length, second_gap_length} in pixels.
// Used by the renderer to generate dashed line geometry.

struct DashPattern
{
    float segments[8]{};  // alternating on/off lengths (max 4 pairs)
    int count = 0;        // number of segments (even number)
    float total = 0.0f;   // sum of all segments (pattern repeat length)
};

inline DashPattern get_dash_pattern(LineStyle style, float line_width = 2.0f)
{
    DashPattern p;
    float w = line_width;
    switch (style)
    {
        case LineStyle::Solid:
        case LineStyle::None:
            break;
        case LineStyle::Dashed:
            // Clean dashes: 8x width on, 4x width off
            p.segments[0] = 8.0f * w;  // dash
            p.segments[1] = 4.0f * w;  // gap
            p.count = 2;
            p.total = 12.0f * w;
            break;
        case LineStyle::Dotted:
            // Round dots: 2x width on (appears as dot with round caps), 4x gap
            p.segments[0] = 2.0f * w;  // dot
            p.segments[1] = 4.0f * w;  // gap
            p.count = 2;
            p.total = 6.0f * w;
            break;
        case LineStyle::DashDot:
            // Dash-dot: 8x dash, 3.5x gap, 2x dot, 3.5x gap
            p.segments[0] = 8.0f * w;  // dash
            p.segments[1] = 3.5f * w;  // gap
            p.segments[2] = 2.0f * w;  // dot
            p.segments[3] = 3.5f * w;  // gap
            p.count = 4;
            p.total = 17.0f * w;
            break;
        case LineStyle::DashDotDot:
            // Dash-dot-dot: 8x dash, 3x gap, 2x dot, 3x gap, 2x dot, 3x gap
            p.segments[0] = 8.0f * w;  // dash
            p.segments[1] = 3.0f * w;  // gap
            p.segments[2] = 2.0f * w;  // dot
            p.segments[3] = 3.0f * w;  // gap
            p.segments[4] = 2.0f * w;  // dot
            p.segments[5] = 3.0f * w;  // gap
            p.count = 6;
            p.total = 21.0f * w;
            break;
    }
    return p;
}

// ─── MATLAB Format String Parser ─────────────────────────────────────────────
// Parses MATLAB-style format strings like "r--o", "b:", "g-.s", "k*", etc.
//
// Format: [color][line_style][marker]
//   Color chars:  r g b c m y k w
//   Line styles:  - -- : -. -..
//   Marker chars: . o + x * s d ^ v < > p h O S D A
//
// Examples:
//   "r"      → red solid line
//   "r--"    → red dashed line
//   "r--o"   → red dashed line with circle markers
//   "bo"     → blue, no line, circle markers
//   ":r"     → red dotted line (order flexible)
//   "k*"     → black star markers, no line
//   "--gs"   → green dashed line with square markers

inline PlotStyle parse_format_string(std::string_view fmt)
{
    PlotStyle style;
    style.line_style = LineStyle::None;  // Will be set to Solid if line specifier found
    bool has_line_spec = false;
    bool has_marker_spec = false;

    size_t i = 0;
    while (i < fmt.size())
    {
        char c = fmt[i];

        // ── Color specifiers ──
        auto try_color = [&]() -> bool
        {
            switch (c)
            {
                case 'r':
                    style.color = colors::red;
                    return true;
                case 'g':
                    style.color = colors::green;
                    return true;
                case 'b':
                    style.color = colors::blue;
                    return true;
                case 'c':
                    style.color = colors::cyan;
                    return true;
                case 'm':
                    style.color = colors::magenta;
                    return true;
                case 'y':
                    style.color = colors::yellow;
                    return true;
                case 'k':
                    style.color = colors::black;
                    return true;
                case 'w':
                    style.color = colors::white;
                    return true;
                default:
                    return false;
            }
        };

        // ── Line style specifiers (must check multi-char first) ──
        auto try_line = [&]() -> bool
        {
            if (c == '-')
            {
                if (i + 1 < fmt.size() && fmt[i + 1] == '-')
                {
                    style.line_style = LineStyle::Dashed;
                    has_line_spec = true;
                    i += 2;
                    return true;
                }
                if (i + 2 < fmt.size() && fmt[i + 1] == '.' && fmt[i + 2] == '.')
                {
                    style.line_style = LineStyle::DashDotDot;
                    has_line_spec = true;
                    i += 3;
                    return true;
                }
                if (i + 1 < fmt.size() && fmt[i + 1] == '.')
                {
                    style.line_style = LineStyle::DashDot;
                    has_line_spec = true;
                    i += 2;
                    return true;
                }
                // Single '-' = solid
                style.line_style = LineStyle::Solid;
                has_line_spec = true;
                i += 1;
                return true;
            }
            if (c == ':')
            {
                style.line_style = LineStyle::Dotted;
                has_line_spec = true;
                i += 1;
                return true;
            }
            return false;
        };

        // ── Marker specifiers ──
        auto try_marker = [&]() -> bool
        {
            switch (c)
            {
                case '.':
                    style.marker_style = MarkerStyle::Point;
                    has_marker_spec = true;
                    return true;
                case 'o':
                    style.marker_style = MarkerStyle::Circle;
                    has_marker_spec = true;
                    return true;
                case '+':
                    style.marker_style = MarkerStyle::Plus;
                    has_marker_spec = true;
                    return true;
                case 'x':
                    style.marker_style = MarkerStyle::Cross;
                    has_marker_spec = true;
                    return true;
                case '*':
                    style.marker_style = MarkerStyle::Star;
                    has_marker_spec = true;
                    return true;
                case 's':
                    style.marker_style = MarkerStyle::Square;
                    has_marker_spec = true;
                    return true;
                case 'd':
                    style.marker_style = MarkerStyle::Diamond;
                    has_marker_spec = true;
                    return true;
                case '^':
                    style.marker_style = MarkerStyle::TriangleUp;
                    has_marker_spec = true;
                    return true;
                case 'v':
                    style.marker_style = MarkerStyle::TriangleDown;
                    has_marker_spec = true;
                    return true;
                case '<':
                    style.marker_style = MarkerStyle::TriangleLeft;
                    has_marker_spec = true;
                    return true;
                case '>':
                    style.marker_style = MarkerStyle::TriangleRight;
                    has_marker_spec = true;
                    return true;
                case 'p':
                    style.marker_style = MarkerStyle::Pentagon;
                    has_marker_spec = true;
                    return true;
                case 'h':
                    style.marker_style = MarkerStyle::Hexagon;
                    has_marker_spec = true;
                    return true;
                case 'O':
                    style.marker_style = MarkerStyle::FilledCircle;
                    has_marker_spec = true;
                    return true;
                case 'S':
                    style.marker_style = MarkerStyle::FilledSquare;
                    has_marker_spec = true;
                    return true;
                case 'D':
                    style.marker_style = MarkerStyle::FilledDiamond;
                    has_marker_spec = true;
                    return true;
                case 'A':
                    style.marker_style = MarkerStyle::FilledTriangleUp;
                    has_marker_spec = true;
                    return true;
                default:
                    return false;
            }
        };

        // Try line style first (handles multi-char like --, -., -..)
        if (try_line())
            continue;
        // Then color
        if (try_color())
        {
            ++i;
            continue;
        }
        // Then marker
        if (try_marker())
        {
            ++i;
            continue;
        }
        // Unknown character — skip
        ++i;
    }

    // MATLAB behavior: if only a color is given (no line or marker spec),
    // default to solid line
    if (!has_line_spec && !has_marker_spec)
    {
        style.line_style = LineStyle::Solid;
    }
    // If only marker specified with no line spec, line stays None (marker-only plot)
    // If only line spec with no marker, marker stays None (line-only plot)

    return style;
}

// Build a MATLAB-compatible format string from a PlotStyle
inline std::string to_format_string(const PlotStyle& style)
{
    std::string result;

    // Color
    if (style.color.has_value())
    {
        const auto& c = *style.color;
        if (c.r >= 0.9f && c.g < 0.1f && c.b < 0.1f)
            result += 'r';
        else if (c.r < 0.1f && c.g >= 0.9f && c.b < 0.1f)
            result += 'g';
        else if (c.r < 0.1f && c.g < 0.1f && c.b >= 0.9f)
            result += 'b';
        else if (c.r < 0.1f && c.g >= 0.9f && c.b >= 0.9f)
            result += 'c';
        else if (c.r >= 0.9f && c.g < 0.1f && c.b >= 0.9f)
            result += 'm';
        else if (c.r >= 0.9f && c.g >= 0.9f && c.b < 0.1f)
            result += 'y';
        else if (c.r < 0.1f && c.g < 0.1f && c.b < 0.1f)
            result += 'k';
        else if (c.r >= 0.9f && c.g >= 0.9f && c.b >= 0.9f)
            result += 'w';
        // Non-standard colors can't be represented in format string
    }

    // Line style
    result += line_style_symbol(style.line_style);

    // Marker
    char ms = marker_style_symbol(style.marker_style);
    if (ms != '\0')
        result += ms;

    return result;
}

}  // namespace spectra
