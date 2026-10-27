#pragma once

// Builds the SVG exporter's chrome colours from the active theme, so a vector
// export matches the on-screen / PNG look (background, grid, axes, text).

#include <spectra/export.hpp>

#include "theme.hpp"

namespace spectra::ui
{

inline SvgStyle svg_style_from_theme(const ThemeColors& tc)
{
    auto to_color = [](const ui::Color& c) { return spectra::Color{c.r, c.g, c.b, c.a}; };

    SvgStyle s;
    s.background     = to_color(tc.bg_canvas);
    s.background.a   = 1.0f;
    s.grid_major     = to_color(tc.grid_major);
    s.grid_minor     = to_color(tc.grid_minor);
    s.axis_line      = to_color(tc.axis_line);
    s.tick_label     = to_color(tc.tick_label);
    s.text_primary   = to_color(tc.text_primary);
    s.text_secondary = to_color(tc.text_secondary);
    s.legend_bg      = to_color(tc.bg_elevated);
    s.legend_border  = to_color(tc.border_subtle);
    return s;
}

}   // namespace spectra::ui
