#pragma once

// Screen <-> data mapping for 2D crosshair / cursor overlays.
//
// Goes through axis space (core/axis_scale.hpp) so the mapping matches how the
// renderer draws the axes: a log axis is linear in log(v), not in v.  Used by
// the linked multi-axes crosshair to unproject the cursor on the hovered axes
// and reproject it onto every other subplot.

#include <spectra/axes.hpp>

#include "core/axis_scale.hpp"

namespace spectra::crosshair_mapping
{

/// Axis-space span of @p lim, or 1 when degenerate (avoids division by zero).
inline double axis_span(const AxisLimits& a)
{
    const double r = a.max - a.min;
    return r == 0.0 ? 1.0 : r;
}

/// Fraction along an axis (0 = min limit, 1 = max limit) → data value.
inline double fraction_to_data(ScaleType s, AxisLimits lim, double t)
{
    const AxisLimits a = axis_space_limits(s, lim);
    return from_axis_space(s, a.min + t * axis_span(a));
}

/// Data value → fraction along an axis (0 = min limit, 1 = max limit).
inline double data_to_fraction(ScaleType s, AxisLimits lim, double v)
{
    const AxisLimits a = axis_space_limits(s, lim);
    return (to_axis_space(s, v) - a.min) / axis_span(a);
}

/// Screen x (pixels) inside @p ax's viewport → data x.
inline double screen_to_data_x(const Axes& ax, double sx)
{
    const Rect&  vp = ax.viewport();
    const double w  = vp.w == 0.0f ? 1.0 : static_cast<double>(vp.w);
    return fraction_to_data(ax.xscale(), ax.x_limits(), (sx - vp.x) / w);
}

/// Screen y (pixels, growing downward) inside @p ax's viewport → data y.
inline double screen_to_data_y(const Axes& ax, double sy)
{
    const Rect&  vp = ax.viewport();
    const double h  = vp.h == 0.0f ? 1.0 : static_cast<double>(vp.h);
    return fraction_to_data(ax.yscale(), ax.y_limits(), 1.0 - (sy - vp.y) / h);
}

/// Data x → screen x (pixels) in @p ax's viewport.
inline double data_to_screen_x(const Axes& ax, double x)
{
    const Rect& vp = ax.viewport();
    return vp.x + data_to_fraction(ax.xscale(), ax.x_limits(), x) * vp.w;
}

/// Data y → screen y (pixels, growing downward) in @p ax's viewport.
inline double data_to_screen_y(const Axes& ax, double y)
{
    const Rect& vp = ax.viewport();
    return vp.y + (1.0 - data_to_fraction(ax.yscale(), ax.y_limits(), y)) * vp.h;
}

}   // namespace spectra::crosshair_mapping
