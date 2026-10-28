#pragma once

// Shared screen <-> data mapping for 2D overlays.
//
// Every overlay that pins something to data coordinates (crosshair, data
// markers / data tips, region select, box select, annotations) maps through
// this one helper so they all agree with the renderer: positions are linear
// in *axis space* (core/axis_scale.hpp), i.e. log10(v) / log2(v) on a log
// axis, sign(v)·sqrt(|v|) on a sqrt axis and v itself on a linear axis.

#include <spectra/axes.hpp>

#include "core/axis_scale.hpp"

namespace spectra
{

namespace axis_mapping
{

/// Axis-space span of @p a, or 1 when degenerate (avoids division by zero).
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

}   // namespace axis_mapping

/// Screen <-> data mapping of one 2D axes (viewport + limits + scales).
/// Axis-space limits are computed once at construction, so the per-point
/// conversions are cheap enough for hit-testing whole series.
class AxisMapping
{
   public:
    AxisMapping() = default;

    AxisMapping(const Rect& viewport,
                AxisLimits  xlim,
                AxisLimits  ylim,
                ScaleType   xscale = ScaleType::Linear,
                ScaleType   yscale = ScaleType::Linear)
        : viewport_(viewport), xscale_(xscale), yscale_(yscale),
          ax_x_(axis_space_limits(xscale, xlim)), ax_y_(axis_space_limits(yscale, ylim))
    {
    }

    /// Mapping of @p axes as currently laid out (viewport, limits, scales).
    explicit AxisMapping(const Axes& axes)
        : AxisMapping(axes.viewport(),
                      axes.x_limits(),
                      axes.y_limits(),
                      axes.xscale(),
                      axes.yscale())
    {
    }

    const Rect& viewport() const { return viewport_; }
    ScaleType   xscale() const { return xscale_; }
    ScaleType   yscale() const { return yscale_; }

    /// False when @p x cannot be drawn on the x axis (non-positive / NaN on a log axis).
    bool x_drawable(double x) const { return !is_log_scale(xscale_) || x > 0.0; }
    bool y_drawable(double y) const { return !is_log_scale(yscale_) || y > 0.0; }

    /// Data x → fraction of the viewport width (0 = left edge).
    double x_fraction(double x) const
    {
        return (to_axis_space(xscale_, x) - ax_x_.min) / axis_mapping::axis_span(ax_x_);
    }
    /// Data y → fraction of the viewport height (0 = bottom edge).
    double y_fraction(double y) const
    {
        return (to_axis_space(yscale_, y) - ax_y_.min) / axis_mapping::axis_span(ax_y_);
    }

    /// Data x → screen x (pixels).
    double to_screen_x(double x) const
    {
        return static_cast<double>(viewport_.x) + x_fraction(x) * viewport_.w;
    }
    /// Data y → screen y (pixels, growing downward).
    double to_screen_y(double y) const
    {
        return static_cast<double>(viewport_.y) + (1.0 - y_fraction(y)) * viewport_.h;
    }

    /// Screen x (pixels) → data x.
    double to_data_x(double sx) const
    {
        const double w = viewport_.w == 0.0f ? 1.0 : static_cast<double>(viewport_.w);
        const double t = (sx - viewport_.x) / w;
        return from_axis_space(xscale_, ax_x_.min + t * axis_mapping::axis_span(ax_x_));
    }
    /// Screen y (pixels, growing downward) → data y.
    double to_data_y(double sy) const
    {
        const double h = viewport_.h == 0.0f ? 1.0 : static_cast<double>(viewport_.h);
        const double t = 1.0 - (sy - viewport_.y) / h;
        return from_axis_space(yscale_, ax_y_.min + t * axis_mapping::axis_span(ax_y_));
    }

   private:
    Rect       viewport_{};
    ScaleType  xscale_ = ScaleType::Linear;
    ScaleType  yscale_ = ScaleType::Linear;
    AxisLimits ax_x_{};   // axis-space limits
    AxisLimits ax_y_{};
};

}   // namespace spectra
