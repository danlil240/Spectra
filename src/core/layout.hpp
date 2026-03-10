#pragma once

#include <spectra/series.hpp>
#include <vector>

namespace spectra
{

// Margins in pixels around each subplot's plot area.
struct Margins
{
    float left   = 60.0f;
    float right  = 40.0f;
    float bottom = 50.0f;
    float top    = 40.0f;
};

// Compute viewport rectangles for a grid of subplots.
// Returns a vector of Rect (one per cell), ordered row-major (row 0 col 0, row 0 col 1, ...).
// figure_width/height are in pixels.
// min_subplot_height: if > 0, each row's cell height is at least this value (plus margins).
// actual_content_height: if non-null, receives the total content height (may exceed figure_height).
std::vector<Rect> compute_subplot_layout(float          figure_width,
                                         float          figure_height,
                                         int            rows,
                                         int            cols,
                                         const Margins& margins               = {},
                                         float          min_subplot_height    = 0.0f,
                                         float*         actual_content_height = nullptr);

// Chrome-aware overload: computes subplot rects inside a content region starting
// at (origin_x, origin_y) in window coordinates.
std::vector<Rect> compute_subplot_layout(float          figure_width,
                                         float          figure_height,
                                         int            rows,
                                         int            cols,
                                         const Margins& margins,
                                         float          origin_x,
                                         float          origin_y,
                                         float          min_subplot_height    = 0.0f,
                                         float*         actual_content_height = nullptr);

}   // namespace spectra
