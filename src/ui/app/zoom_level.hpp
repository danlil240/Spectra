#pragma once

#include <cmath>
#include <span>

namespace spectra
{

// Coordinates at or beyond this magnitude are reference-line sentinels, not
// data: Axes::axhline()/axvline() and the Python hline()/vline() helpers draw
// their lines out to +-1e12 so they span any view.
inline constexpr float ZOOM_EXTENT_SENTINEL = 1e11f;

// Grow [lo, hi] to cover the finite, non-sentinel values of @p values.
// Used for the status-bar zoom: an infinite reference line must not count as
// data extent (it made a plain plot read "Zoom: 1.40e+13%").
inline void accumulate_zoom_extent(std::span<const float> values, float& lo, float& hi)
{
    for (float v : values)
    {
        if (!std::isfinite(v) || std::fabs(v) >= ZOOM_EXTENT_SENTINEL)
            continue;
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
}

// Fraction of the data range that autoscale adds on each side of an axis in
// the Fit/Padded modes (mirrors finalize_extent() in src/core/axes.cpp).
inline constexpr double AUTOSCALE_PAD_FRACTION = 0.05;

// Zoom level reported by the status bar ("Zoom: N%"), as a factor.
//
// The view counts as unzoomed (1.0) whenever it frames the data the way
// autoscale does: anywhere from a tight fit (view == data extent) up to the
// padded fit (data extent plus @p pad_fraction on each side).  Without this
// band a freshly autoscaled plot read "Zoom: 91%" because the 5% padding made
// the view wider than the raw data.  Zooming in past the tight fit gives a
// factor > 1, zooming out past the padded fit a factor < 1; the function is
// continuous at both edges.
//
// Returns 1.0 when either range is not positive (nothing to compare).
inline double zoom_level_for_ranges(double data_range,
                                    double view_range,
                                    double pad_fraction = AUTOSCALE_PAD_FRACTION)
{
    if (!(data_range > 0.0) || !(view_range > 0.0))
        return 1.0;
    if (view_range < data_range)
        return data_range / view_range;
    const double padded_range = data_range * (1.0 + 2.0 * pad_fraction);
    if (view_range > padded_range)
        return padded_range / view_range;
    return 1.0;
}

}   // namespace spectra
