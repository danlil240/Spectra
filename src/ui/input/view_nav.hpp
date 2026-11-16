#pragma once

// Pure helpers for mouse view navigation: which axes a wheel step zooms,
// the axis gutters (tick-label bands) next to an axes, and how a box-zoom
// drag is interpreted.  Toolkit-free so they can be unit-tested directly.

#include <cmath>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <spectra/axes.hpp>

namespace spectra
{

// Modifier bits (GLFW values; SDL3 and the embed surface map onto them).
inline constexpr int VIEW_MOD_SHIFT   = 0x0001;
inline constexpr int VIEW_MOD_CONTROL = 0x0002;
inline constexpr int VIEW_MOD_ALT     = 0x0004;

// Which axes a gesture moves.
enum class ViewAxes : uint8_t
{
    Both,
    X,
    Y,
};

constexpr bool view_axes_has_x(ViewAxes a)
{
    return a != ViewAxes::Y;
}
constexpr bool view_axes_has_y(ViewAxes a)
{
    return a != ViewAxes::X;
}

// Axes a mouse-wheel zoom affects for the given modifiers:
// Shift zooms X only, Ctrl (or Alt) zooms Y only, none (or both) zooms both.
constexpr ViewAxes wheel_zoom_axes(int mods)
{
    const bool shift = (mods & VIEW_MOD_SHIFT) != 0;
    const bool y_mod = (mods & (VIEW_MOD_CONTROL | VIEW_MOD_ALT)) != 0;
    if (shift && !y_mod)
        return ViewAxes::X;
    if (y_mod && !shift)
        return ViewAxes::Y;
    return ViewAxes::Both;
}

// The tick-label band ("gutter") of an axes: below the data area for X, left
// of it for Y.  Wheel and drag there act on that axis only.
enum class AxisGutter : uint8_t
{
    None,
    X,
    Y,
};

// Gutter thicknesses in pixels: the X tick labels sit in roughly the first
// 30 px below the plot, the Y tick labels in roughly 60 px to its left.
inline constexpr float AXIS_GUTTER_X_BAND = 36.0f;
inline constexpr float AXIS_GUTTER_Y_BAND = 64.0f;

// Which gutter of the data area @p vp the point (x, y) is in.  The data area
// itself and the corner below-left of it are not gutters.
inline AxisGutter hit_test_axis_gutter(const Rect& vp,
                                       double      x,
                                       double      y,
                                       float       x_band = AXIS_GUTTER_X_BAND,
                                       float       y_band = AXIS_GUTTER_Y_BAND)
{
    if (vp.w <= 0.0f || vp.h <= 0.0f)
        return AxisGutter::None;
    const double left   = vp.x;
    const double right  = static_cast<double>(vp.x) + vp.w;
    const double top    = vp.y;
    const double bottom = static_cast<double>(vp.y) + vp.h;
    if (x >= left && x <= right && y > bottom && y <= bottom + x_band)
        return AxisGutter::X;
    if (y >= top && y <= bottom && x < left && x >= left - y_band)
        return AxisGutter::Y;
    return AxisGutter::None;
}

// What a released box-zoom drag of @p dx × @p dy screen pixels does.
enum class BoxZoomKind : uint8_t
{
    None,   // too small: an accidental click, ignored
    X,      // thin, nearly horizontal band: zoom X only
    Y,      // thin, nearly vertical band: zoom Y only
    XY,     // a real rectangle: zoom both axes
};

inline constexpr double BOX_ZOOM_MIN_PIXELS  = 5.0;    // shorter drags are clicks
inline constexpr double BOX_ZOOM_THIN_PIXELS = 12.0;   // a band thinner than this...
inline constexpr double BOX_ZOOM_THIN_RATIO  = 4.0;    // ...and this many times longer

// A drag whose short side is at most BOX_ZOOM_MIN_PIXELS, or is under
// BOX_ZOOM_THIN_PIXELS while the long side is BOX_ZOOM_THIN_RATIO times
// longer, zooms only the axis along its long side (it used to be dropped).
inline BoxZoomKind classify_box_zoom(double dx, double dy)
{
    const double ax = std::abs(dx);
    const double ay = std::abs(dy);
    const double lo = std::min(ax, ay);
    const double hi = std::max(ax, ay);
    if (!(hi > BOX_ZOOM_MIN_PIXELS))
        return BoxZoomKind::None;
    const bool thin =
        lo <= BOX_ZOOM_MIN_PIXELS || (lo < BOX_ZOOM_THIN_PIXELS && hi >= BOX_ZOOM_THIN_RATIO * lo);
    if (!thin)
        return BoxZoomKind::XY;
    return ax >= ay ? BoxZoomKind::X : BoxZoomKind::Y;
}

// Keep a zoomed range representable: never narrower than ~16 ulp of its
// magnitude (deep zoom past double precision would collapse the range).
inline AxisLimits clamp_min_span(AxisLimits lim)
{
    const double abs_max   = std::max(std::abs(lim.min), std::abs(lim.max));
    double       min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
    if (min_range < 1e-300)
        min_range = 1e-300;
    if (lim.max - lim.min < min_range)
    {
        const double mid = (lim.min + lim.max) * 0.5;
        lim.min          = mid - min_range * 0.5;
        lim.max          = mid + min_range * 0.5;
    }
    return lim;
}

// Fraction of the axis span one horizontal wheel step pans by.
inline constexpr double WHEEL_PAN_FRACTION = 0.1;

}   // namespace spectra
