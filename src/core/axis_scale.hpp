#pragma once

// Axis-space mapping for 2D scale types.
//
// "Axis space" is the coordinate system in which an axis is drawn linearly:
// log10(v) for Log10, log2(v) for Log2 and v itself for Linear.  The renderer,
// grid, tick labels and pan/zoom map data through these helpers so that a
// log axis is drawn in decades instead of as a squashed linear axis.
//
// Sqrt is deliberately left as identity here: its tick generator places
// ticks in data space for an untransformed axis, and changing that is out of
// scope for the log-scale fix.

#include <cmath>

#include <algorithm>
#include <limits>
#include <spectra/axes.hpp>
#include <vector>

namespace spectra
{

inline bool is_log_scale(ScaleType s)
{
    return s == ScaleType::Log10 || s == ScaleType::Log2;
}

inline double log_scale_base(ScaleType s)
{
    return s == ScaleType::Log2 ? 2.0 : 10.0;
}

/// Lowest axis-space value a non-positive datum is clipped to on a log axis
/// (log of the smallest normal double).  Lines through such points run off
/// the bottom/left of the plot instead of vanishing.
inline double log_axis_floor(ScaleType s)
{
    return std::log(std::numeric_limits<double>::min()) / std::log(log_scale_base(s));
}

/// Map a data value into axis space.  Non-positive values on a log axis are
/// clipped to log_axis_floor(); NaN stays NaN so line breaks are preserved.
inline double to_axis_space(ScaleType s, double v)
{
    if (!is_log_scale(s))
        return v;
    if (std::isnan(v))
        return v;
    if (v <= 0.0)
        return log_axis_floor(s);
    return s == ScaleType::Log2 ? std::log2(v) : std::log10(v);
}

/// Inverse of to_axis_space().
inline double from_axis_space(ScaleType s, double a)
{
    if (!is_log_scale(s))
        return a;
    return std::pow(log_scale_base(s), a);
}

/// Make data-space limits valid for a log axis: a non-positive lower limit is
/// replaced by @p positive_floor (the smallest positive datum) when it lies
/// below the upper limit, otherwise by max / base^3; a non-positive upper limit
/// yields [1, base].  Linear/Sqrt limits are returned unchanged.
inline AxisLimits sanitize_log_limits(ScaleType s, AxisLimits lim, double positive_floor = 0.0)
{
    if (!is_log_scale(s))
        return lim;
    const double base = log_scale_base(s);
    if (!(lim.max > 0.0) || !std::isfinite(lim.max))
        return {1.0, base};
    if (!(lim.min > 0.0) || !std::isfinite(lim.min))
    {
        lim.min = (positive_floor > 0.0 && positive_floor < lim.max)
                      ? positive_floor
                      : lim.max / (base * base * base);
    }
    return lim;
}

/// Data-space limits → axis-space limits (sanitized for log axes).
inline AxisLimits axis_space_limits(ScaleType s, AxisLimits lim)
{
    if (!is_log_scale(s))
        return lim;
    lim = sanitize_log_limits(s, lim);
    return {to_axis_space(s, lim.min), to_axis_space(s, lim.max)};
}

/// Zoom data-space limits by @p factor about @p anchor (data space), in axis
/// space so a log axis zooms by the same ratio on both sides of the anchor.
inline AxisLimits zoom_limits_about(ScaleType s, AxisLimits lim, double anchor, double factor)
{
    if (!is_log_scale(s))
        return {anchor + (lim.min - anchor) * factor, anchor + (lim.max - anchor) * factor};
    const AxisLimits a   = axis_space_limits(s, lim);
    const double     anc = to_axis_space(s, anchor);
    return {from_axis_space(s, anc + (a.min - anc) * factor),
            from_axis_space(s, anc + (a.max - anc) * factor)};
}

/// Shift data-space limits by @p fraction of their axis-space span (pan).
inline AxisLimits pan_limits(ScaleType s, AxisLimits lim, double fraction)
{
    if (!is_log_scale(s))
    {
        const double d = fraction * (lim.max - lim.min);
        return {lim.min + d, lim.max + d};
    }
    const AxisLimits a = axis_space_limits(s, lim);
    const double     d = fraction * (a.max - a.min);
    return {from_axis_space(s, a.min + d), from_axis_space(s, a.max + d)};
}

/// Shift data-space limits by @p delta axis-space units (pan by an absolute
/// amount: data units on a linear axis, decades / octaves on a log axis).
inline AxisLimits translate_limits(ScaleType s, AxisLimits lim, double delta)
{
    if (!is_log_scale(s))
        return {lim.min + delta, lim.max + delta};
    const AxisLimits a = axis_space_limits(s, lim);
    return {from_axis_space(s, a.min + delta), from_axis_space(s, a.max + delta)};
}

/// Minor tick positions (data space) for a log axis within [lo, hi]:
/// 2..9 × 10^k for Log10 and 1.25, 1.5, 1.75 × 2^k for Log2 (each decade /
/// octave split into equal linear steps).  Returns nothing for non-log scales,
/// or when the range spans so many decades / octaves that minor lines would
/// merge into a solid band.
inline std::vector<double> log_minor_ticks(ScaleType s, double lo, double hi)
{
    std::vector<double> out;
    if (!is_log_scale(s) || !(lo > 0.0) || !(hi > lo) || !std::isfinite(hi))
        return out;
    const bool   log2 = s == ScaleType::Log2;
    const double base = log_scale_base(s);
    // Roughly 100 minor lines at most for either base.
    const double max_powers = log2 ? 32.0 : 12.0;
    const double a_lo       = to_axis_space(s, lo);
    const double a_hi       = to_axis_space(s, hi);
    if (a_hi - a_lo > max_powers)
        return out;
    static constexpr double SUBS10[] = {2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    static constexpr double SUBS2[]  = {1.25, 1.5, 1.75};
    const int e_lo = static_cast<int>(std::floor(a_lo));
    const int e_hi = static_cast<int>(std::ceil(a_hi));
    for (int e = e_lo; e <= e_hi; ++e)
    {
        const double power     = std::pow(base, static_cast<double>(e));
        auto         add_minor = [&](double k)
        {
            const double v = k * power;
            if (v >= lo && v <= hi)
                out.push_back(v);
        };
        if (log2)
            for (double k : SUBS2)
                add_minor(k);
        else
            for (double k : SUBS10)
                add_minor(k);
    }
    return out;
}

}   // namespace spectra
