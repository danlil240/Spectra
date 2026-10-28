#pragma once

// Axis-space mapping for 2D scale types.
//
// "Axis space" is the coordinate system in which an axis is drawn linearly:
// log10(v) for Log10, log2(v) for Log2, the signed square root
// sign(v)·sqrt(|v|) for Sqrt and v itself for Linear.  The renderer, grid,
// tick labels, overlays, SVG export and pan/zoom map data through these
// helpers so every output path agrees on where a value is drawn.
//
// Sqrt uses the signed square root so negative data still maps monotonically
// (and continuously through zero) instead of being clipped or dropped.

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

inline bool is_sqrt_scale(ScaleType s)
{
    return s == ScaleType::Sqrt;
}

/// True for every scale whose axis space differs from data space.
inline bool is_nonlinear_scale(ScaleType s)
{
    return is_log_scale(s) || is_sqrt_scale(s);
}

/// Signed square root sign(v)·sqrt(|v|) (NaN stays NaN).
inline double signed_sqrt(double v)
{
    return std::copysign(std::sqrt(std::abs(v)), v);
}

/// Inverse of signed_sqrt(): sign(a)·a².
inline double signed_square(double a)
{
    return a * std::abs(a);
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
    if (is_sqrt_scale(s))
        return signed_sqrt(v);
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
    if (is_sqrt_scale(s))
        return signed_square(a);
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
    if (is_sqrt_scale(s))
        return {signed_sqrt(lim.min), signed_sqrt(lim.max)};
    if (!is_log_scale(s))
        return lim;
    lim = sanitize_log_limits(s, lim);
    return {to_axis_space(s, lim.min), to_axis_space(s, lim.max)};
}

/// Zoom data-space limits by @p factor about @p anchor (data space), in axis
/// space so a log / sqrt axis zooms by the same screen ratio on both sides of
/// the anchor.
inline AxisLimits zoom_limits_about(ScaleType s, AxisLimits lim, double anchor, double factor)
{
    if (!is_nonlinear_scale(s))
        return {anchor + (lim.min - anchor) * factor, anchor + (lim.max - anchor) * factor};
    const AxisLimits a   = axis_space_limits(s, lim);
    const double     anc = to_axis_space(s, anchor);
    return {from_axis_space(s, anc + (a.min - anc) * factor),
            from_axis_space(s, anc + (a.max - anc) * factor)};
}

/// Limits spanning @p factor times the axis-space span of @p lim, centred on
/// @p centre (data space) in axis space.  A centre that cannot be drawn on the
/// axis (non-positive on a log axis, non-finite) keeps the current view centre.
inline AxisLimits zoom_limits_centered(ScaleType s, AxisLimits lim, double centre, double factor)
{
    const AxisLimits a    = axis_space_limits(s, lim);
    double           c    = (a.min + a.max) * 0.5;
    const bool       draw = std::isfinite(centre) && (!is_log_scale(s) || centre > 0.0);
    if (draw)
        c = to_axis_space(s, centre);
    const double half = (a.max - a.min) * 0.5 * factor;
    return {from_axis_space(s, c - half), from_axis_space(s, c + half)};
}

/// Shift data-space limits by @p fraction of their axis-space span (pan).
inline AxisLimits pan_limits(ScaleType s, AxisLimits lim, double fraction)
{
    if (!is_nonlinear_scale(s))
    {
        const double d = fraction * (lim.max - lim.min);
        return {lim.min + d, lim.max + d};
    }
    const AxisLimits a = axis_space_limits(s, lim);
    const double     d = fraction * (a.max - a.min);
    return {from_axis_space(s, a.min + d), from_axis_space(s, a.max + d)};
}

/// Shift data-space limits by @p delta axis-space units (pan by an absolute
/// amount: data units on a linear axis, decades / octaves on a log axis,
/// square-root units on a sqrt axis).
inline AxisLimits translate_limits(ScaleType s, AxisLimits lim, double delta)
{
    if (!is_nonlinear_scale(s))
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
    const int               e_lo     = static_cast<int>(std::floor(a_lo));
    const int               e_hi     = static_cast<int>(std::ceil(a_hi));
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
