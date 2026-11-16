#pragma once

// empty_axes_hint.hpp — when the canvas shows the "drop a file" empty state
// over an axes.  ImGui-only: never part of PNG/SVG/PDF or headless output.

#include <spectra/axes.hpp>

namespace spectra
{

// Smallest axes viewport (pixels) the empty-state hint is drawn in; smaller
// subplots stay clean.
inline constexpr float EMPTY_AXES_HINT_MIN_W = 240.0f;
inline constexpr float EMPTY_AXES_HINT_MIN_H = 150.0f;

// True when @p axes has no series and its viewport is large enough for the
// in-canvas empty state.
inline bool axes_shows_empty_hint(const AxesBase& axes)
{
    if (!axes.series().empty())
        return false;
    const Rect& vp = axes.viewport();
    return vp.w >= EMPTY_AXES_HINT_MIN_W && vp.h >= EMPTY_AXES_HINT_MIN_H;
}

}   // namespace spectra
