#pragma once

// Axis-aware formatting of cursor readouts: tooltips, data tips, crosshair
// labels, the compare-at-X readout and the status bar all print coordinates
// through these helpers, so a date-time axis shows "2024-03-05 14:30:12"
// everywhere instead of a raw epoch number.

#include <algorithm>
#include <cmath>
#include <spectra/axes.hpp>
#include <string>

#include "axis_mapping.hpp"
#include "core/datetime_ticks.hpp"
#include "readout_format.hpp"

namespace spectra::ui
{

enum class ReadoutAxis
{
    X,
    Y,
};

/// True when @p axes labels the given axis as date-time (only on a linear scale,
/// matching Axes::compute_x_ticks()).
inline bool axis_is_datetime(const Axes* axes, ReadoutAxis which)
{
    if (!axes)
        return false;
    if (which == ReadoutAxis::X)
        return axes->xaxis_datetime() && axes->xscale() == ScaleType::Linear;
    return axes->yaxis_datetime() && axes->yscale() == ScaleType::Linear;
}

/// Data distance one screen pixel spans on @p axes at the middle of the axis
/// (0 when unknown, e.g. no viewport yet).
inline double axis_pixel_resolution(const Axes& axes, ReadoutAxis which)
{
    const Rect& vp = axes.viewport();
    if (vp.w <= 0.0f || vp.h <= 0.0f)
        return 0.0;
    const AxisMapping m(axes);
    if (which == ReadoutAxis::X)
    {
        const double cx = vp.x + vp.w * 0.5;
        return std::abs(m.to_data_x(cx + 0.5) - m.to_data_x(cx - 0.5));
    }
    const double cy = vp.y + vp.h * 0.5;
    return std::abs(m.to_data_y(cy + 0.5) - m.to_data_y(cy - 0.5));
}

/// A data value for a tooltip / data tip row: date-time on a date-time axis
/// (sub-second digits only when the view resolves them), otherwise
/// format_readout_value() with @p sig_digits significant digits.
inline std::string format_axis_value(const Axes* axes,
                                     ReadoutAxis which,
                                     double      v,
                                     int         sig_digits = 6)
{
    if (axis_is_datetime(axes, which))
        return format_datetime_readout(v, std::min(1.0, axis_pixel_resolution(*axes, which)));
    return format_readout_value(v, sig_digits);
}

/// A cursor coordinate (crosshair label, status bar): as many digits as
/// @p resolution (data per pixel at the cursor, 0 = unknown) resolves; date-time
/// on a date-time axis.  Numeric results are at most @p max_chars wide.
inline std::string format_axis_cursor(const Axes* axes,
                                      ReadoutAxis which,
                                      double      v,
                                      double      resolution,
                                      size_t      max_chars = 12)
{
    if (axis_is_datetime(axes, which))
        return format_datetime_readout(v, resolution);
    return format_compact_value(v, resolution, max_chars);
}

/// Status-bar cursor readout: "X: …  Y: …" (date-time aware, pixel-resolution
/// digits) or "X: —  Y: —" when the cursor is not over a 2D plot.
inline std::string format_cursor_status(const Axes* axes,
                                        bool        valid,
                                        double      data_x,
                                        double      data_y,
                                        double      screen_x,
                                        double      screen_y)
{
    if (!valid)
        return "X: \xE2\x80\x94  Y: \xE2\x80\x94";
    double x_res = 0.0;
    double y_res = 0.0;
    if (axes && axes->viewport().w > 0.0f && axes->viewport().h > 0.0f)
    {
        const AxisMapping m(*axes);
        x_res = std::abs(m.to_data_x(screen_x + 0.5) - m.to_data_x(screen_x - 0.5));
        y_res = std::abs(m.to_data_y(screen_y + 0.5) - m.to_data_y(screen_y - 0.5));
    }
    return "X: " + format_axis_cursor(axes, ReadoutAxis::X, data_x, x_res, 16)
           + "  Y: " + format_axis_cursor(axes, ReadoutAxis::Y, data_y, y_res, 16);
}

}   // namespace spectra::ui
