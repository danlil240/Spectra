#pragma once

// Resolved sizes of the text drawn around a 2D/3D axes (tick labels, axis
// labels, title), shared by the GPU text path (render_geometry.cpp), the
// vector exporter (vector_scene.cpp: SVG and PDF) and the layout
// (figure_layout.cpp), so all three measure and place the text the same way.
//
// AxisStyle sizes <= 0 fall back to the theme defaults below (the sizes the
// renderer's font atlas is baked at).  Every size and padding is multiplied by
// the axes' text scale (the UI scale setting on screen, 1 for exports).

#include <cmath>
#include <spectra/axes.hpp>

#include "core/label_layout.hpp"

namespace spectra
{

/// Theme font sizes of plot text, in logical pixels (text_renderer.cpp presets).
inline constexpr float THEME_TICK_FONT_PX  = 13.0f;
inline constexpr float THEME_LABEL_FONT_PX = 14.0f;
inline constexpr float THEME_TITLE_FONT_PX = 18.0f;

/// Range a user font size is clamped to (logical pixels).
inline constexpr float AXIS_FONT_MIN_PX = 4.0f;
inline constexpr float AXIS_FONT_MAX_PX = 96.0f;

/// Line height of a plot text line relative to its font size (Inter: ascent -
/// descent + line gap, what TextRenderer::measure_text() reports as height).
inline constexpr float PLOT_TEXT_LINE_FACTOR = 1.25f;

struct AxisTextMetrics
{
    float scale       = 1.0f;                  // text scale (UI scale on screen)
    float tick_px     = THEME_TICK_FONT_PX;    // tick label font size (scaled)
    float label_px    = THEME_LABEL_FONT_PX;   // axis label font size (scaled)
    float title_px    = THEME_TITLE_FONT_PX;   // title font size (scaled)
    float tick_length = 5.0f;                  // tick mark length (scaled)

    /// A logical padding / gap in the scaled space.
    float px(float logical) const { return logical * scale; }

    /// Font size of a plot label tier (core/label_layout).
    float font_px(LabelFont f) const
    {
        switch (f)
        {
            case LabelFont::Tick:
                return tick_px;
            case LabelFont::Label:
                return label_px;
            case LabelFont::Title:
                break;
        }
        return title_px;
    }
};

/// A requested font size, or `theme_px` when it is unset (<= 0 or not finite).
inline float resolve_axis_font_px(float requested, float theme_px)
{
    if (!(requested > 0.0f) || !std::isfinite(requested))
        return theme_px;
    return requested < AXIS_FONT_MIN_PX   ? AXIS_FONT_MIN_PX
           : requested > AXIS_FONT_MAX_PX ? AXIS_FONT_MAX_PX
                                          : requested;
}

/// Sizes of an axes' text for `style`, scaled by `scale` (<= 0 → 1).
inline AxisTextMetrics axis_text_metrics(const AxisStyle& style, float scale = 1.0f)
{
    if (!(scale > 0.0f) || !std::isfinite(scale))
        scale = 1.0f;
    AxisTextMetrics m;
    m.scale       = scale;
    m.tick_px     = resolve_axis_font_px(style.tick_size, THEME_TICK_FONT_PX) * scale;
    m.label_px    = resolve_axis_font_px(style.label_size, THEME_LABEL_FONT_PX) * scale;
    m.title_px    = resolve_axis_font_px(style.title_size, THEME_TITLE_FONT_PX) * scale;
    m.tick_length = (std::isfinite(style.tick_length) ? style.tick_length : 0.0f) * scale;
    return m;
}

/// Metrics of an axes as laid out (its AxisStyle and current text scale).
inline AxisTextMetrics axis_text_metrics(const AxesBase& axes)
{
    return axis_text_metrics(axes.axis_style(), axes.text_scale());
}

/// Whether an AxisStyle colour overrides the theme (alpha > 0).
inline bool axis_color_is_set(const Color& c)
{
    return c.a > 0.0f;
}

}   // namespace spectra
