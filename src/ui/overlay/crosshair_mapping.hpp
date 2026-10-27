#pragma once

// Screen <-> data mapping for 2D crosshair / cursor overlays.
//
// Thin wrappers over the shared overlay mapping (axis_mapping.hpp), which goes
// through axis space (core/axis_scale.hpp) so the mapping matches how the
// renderer draws the axes: a log axis is linear in log(v), not in v.  Used by
// the linked multi-axes crosshair to unproject the cursor on the hovered axes
// and reproject it onto every other subplot.

#include "axis_mapping.hpp"

namespace spectra::crosshair_mapping
{

using axis_mapping::axis_span;
using axis_mapping::data_to_fraction;
using axis_mapping::fraction_to_data;

/// Screen x (pixels) inside @p ax's viewport → data x.
inline double screen_to_data_x(const Axes& ax, double sx)
{
    return AxisMapping(ax).to_data_x(sx);
}

/// Screen y (pixels, growing downward) inside @p ax's viewport → data y.
inline double screen_to_data_y(const Axes& ax, double sy)
{
    return AxisMapping(ax).to_data_y(sy);
}

/// Data x → screen x (pixels) in @p ax's viewport.
inline double data_to_screen_x(const Axes& ax, double x)
{
    return AxisMapping(ax).to_screen_x(x);
}

/// Data y → screen y (pixels, growing downward) in @p ax's viewport.
inline double data_to_screen_y(const Axes& ax, double y)
{
    return AxisMapping(ax).to_screen_y(y);
}

}   // namespace spectra::crosshair_mapping
