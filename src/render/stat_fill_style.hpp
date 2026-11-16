#pragma once

// Colours of the flat stat fills (bars, histograms, box plots, violins) shared
// by the Vulkan renderer and the SVG/PDF exporter, so both draw the same look.

#include <algorithm>
#include <cmath>
#include <spectra/color.hpp>
#include <spectra/series_stats.hpp>

namespace spectra
{

namespace stat_fill_detail
{

inline float linearize(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

}   // namespace stat_fill_detail

// WCAG relative luminance (0 = black, 1 = white) of the opaque RGB of `c`.
inline float stat_relative_luminance(const Color& c)
{
    using stat_fill_detail::linearize;
    return 0.2126f * linearize(c.r) + 0.7152f * linearize(c.g) + 0.0722f * linearize(c.b);
}

// How far the default outline moves from the fill colour.
inline constexpr float STAT_EDGE_DARKEN  = 0.35f;   // towards black
inline constexpr float STAT_EDGE_LIGHTEN = 0.40f;   // towards white

// Default outline of a stat fill: a darker shade of the fill on light canvases
// and a lighter one on dark canvases, so the outline separates adjacent bars
// and still stands out from the background.  A fill too close to the shade's
// end (near-black fill on a light canvas, near-white on a dark one) flips to
// the other side so the outline never vanishes into the fill.
// `bg_luma` is the relative luminance (0..1) of the plot canvas.
inline Color stat_default_edge_color(const Color& fill, float bg_luma)
{
    const float fill_luma = stat_relative_luminance(fill);
    bool        darken    = bg_luma > 0.5f;
    if (darken && fill_luma < 0.03f)
        darken = false;
    else if (!darken && fill_luma > 0.70f)
        darken = true;

    Color out = fill;
    if (darken)
    {
        out.r *= 1.0f - STAT_EDGE_DARKEN;
        out.g *= 1.0f - STAT_EDGE_DARKEN;
        out.b *= 1.0f - STAT_EDGE_DARKEN;
    }
    else
    {
        out.r += (1.0f - out.r) * STAT_EDGE_LIGHTEN;
        out.g += (1.0f - out.g) * STAT_EDGE_LIGHTEN;
        out.b += (1.0f - out.b) * STAT_EDGE_LIGHTEN;
    }
    return out;
}

// Outline colour of a stat series: the explicit edge colour, else the default
// shade of `fill` (the series colour after theme adaptation).
inline Color stat_edge_color(const StatFillStyle& style, const Color& fill, float bg_luma)
{
    if (style.edge_color)
        return *style.edge_color;
    return stat_default_edge_color(fill, bg_luma);
}

// Median line of a box plot: near-black or white, whichever contrasts more with
// the box fill as it appears (fill composited over the canvas).
inline Color stat_median_color(const Color& fill, float fill_alpha, const Color& background)
{
    const float     a = std::clamp(fill_alpha, 0.0f, 1.0f);
    const Color     shown{fill.r * a + background.r * (1.0f - a),
                      fill.g * a + background.g * (1.0f - a),
                      fill.b * a + background.b * (1.0f - a),
                      1.0f};
    const float     l         = stat_relative_luminance(shown);
    constexpr Color DARK_INK  = {0.08f, 0.08f, 0.10f, 1.0f};
    constexpr Color LIGHT_INK = {1.0f, 1.0f, 1.0f, 1.0f};
    const float     dark_luma = stat_relative_luminance(DARK_INK);
    const float     vs_dark   = (l + 0.05f) / (dark_luma + 0.05f);
    const float     vs_light  = 1.05f / (l + 0.05f);
    return vs_dark >= vs_light ? DARK_INK : LIGHT_INK;
}

// Median line width: a little heavier than the outline.
inline float stat_median_width(float edge_width)
{
    return std::max(edge_width + 1.0f, 1.75f);
}

}   // namespace spectra
