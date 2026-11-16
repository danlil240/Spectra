// style.cpp — built-in plot styles and their application to figures and axes.

#include <spectra/style.hpp>

#include <array>
#include <cctype>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

namespace spectra
{

namespace
{

StyleSheet make_default()
{
    StyleSheet s;
    s.name = "default";
    return s;
}

// Publication figure: white background, black ink, no grid, despined, thin
// lines, minor ticks, compact text, colour-blind-safe palette.
StyleSheet make_paper()
{
    StyleSheet s;
    s.name                = "paper";
    s.background_override = true;
    s.background          = colors::white;
    s.palette             = "colorblind";
    s.grid                = false;
    s.minor_grid          = false;
    s.minor_ticks         = true;
    s.despine             = true;
    s.tick_direction      = TickDirection::Out;
    s.tick_length         = 4.0f;
    s.label_size          = 12.0f;
    s.title_size          = 14.0f;
    s.legend_font_size    = 11.0f;
    s.line_width          = 1.25f;
    s.marker_size         = 5.0f;
    return s;
}

// Slides: large text, thick lines, major grid only.
StyleSheet make_talk()
{
    StyleSheet s;
    s.name             = "talk";
    s.grid             = true;
    s.minor_grid       = false;
    s.tick_length      = 7.0f;
    s.grid_width       = 1.5f;
    s.label_size       = 20.0f;
    s.title_size       = 24.0f;
    s.legend_font_size = 18.0f;
    s.line_width       = 3.5f;
    s.marker_size      = 9.0f;
    return s;
}

// Data only: no grid, despined, short ticks.
StyleSheet make_minimal()
{
    StyleSheet s;
    s.name        = "minimal";
    s.grid        = false;
    s.minor_grid  = false;
    s.despine     = true;
    s.tick_length = 3.0f;
    return s;
}

// "paper" on a dark background (dark slides, dark-mode documents).
StyleSheet make_dark_paper()
{
    StyleSheet s = make_paper();
    s.name       = "dark-paper";
    s.background = Color{0.11f, 0.12f, 0.14f, 1.0f};
    return s;
}

const std::array<StyleSheet, 5>& builtin_styles()
{
    // Immutable table, built once.
    static const std::array<StyleSheet, 5> styles = {make_default(),
                                                     make_paper(),
                                                     make_talk(),
                                                     make_minimal(),
                                                     make_dark_paper()};
    return styles;
}

}   // namespace

namespace style
{

const StyleSheet* find(std::string_view name)
{
    for (const auto& s : builtin_styles())
        if (s.name == name)
            return &s;
    return nullptr;
}

std::vector<std::string> available()
{
    std::vector<std::string> out;
    for (const auto& s : builtin_styles())
        out.push_back(s.name);
    return out;
}

std::string display_name(std::string_view name)
{
    std::string out(name);
    for (auto& ch : out)
        if (ch == '-' || ch == '_')
            ch = ' ';
    if (!out.empty())
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

}   // namespace style

void AxesBase::apply_style(const StyleSheet& style)
{
    grid_enabled_        = style.grid;
    minor_grid_enabled_  = style.minor_grid;
    minor_ticks_enabled_ = style.minor_ticks;
    despined_            = style.despine;
    tick_direction_      = style.tick_direction;

    axis_style_.tick_length = style.tick_length;
    axis_style_.grid_width  = style.grid_width;
    axis_style_.label_size  = style.label_size;
    axis_style_.title_size  = style.title_size;

    // Series that still carry the previous default follow the new one; a
    // width / size the user set explicitly is kept.
    for (auto& sp : series_)
    {
        if (!sp)
            continue;
        if (auto* line = dynamic_cast<LineSeries*>(sp.get()))
        {
            if (line->width() == default_line_width_)
                line->width(style.line_width);
            if (line->marker_size() == default_marker_size_)
                line->marker_size(style.marker_size);
        }
        else if (auto* scatter = dynamic_cast<ScatterSeries*>(sp.get()))
        {
            if (scatter->marker_size() == default_marker_size_)
                scatter->marker_size(style.marker_size);
        }
    }
    default_line_width_  = style.line_width;
    default_marker_size_ = style.marker_size;
}

void Figure::apply_style(const StyleSheet& style)
{
    style_sheet_               = style;
    style_.background_override = style.background_override;
    style_.background          = style.background;
    style_.palette             = style.palette;
    legend_.font_size          = style.legend_font_size;
    for_each_axes([&](AxesBase* ax) { ax->apply_style(style); });
}

bool Figure::apply_style(std::string_view name)
{
    const StyleSheet* s = style::find(name);
    if (!s)
        return false;
    apply_style(*s);
    return true;
}

}   // namespace spectra
