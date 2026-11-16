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

// Spacing of a subplot grid laid out by compute_grid_layout(): outer margins
// around the whole grid and the gaps between neighbouring plot areas.
struct GridSpacing
{
    Margins            outer;
    std::vector<float> col_gaps;   // cols - 1 entries: gap right of column c (pixels)
    std::vector<float> row_gaps;   // rows - 1 entries: gap below row r (pixels)
};

// Plot areas of a rows x cols grid filling the content region (origin_x,
// origin_y, width, height) minus spacing.outer, separated by spacing's gaps
// (missing entries count as 0).  All plot areas share one width and one
// height.  min_subplot_height and actual_content_height work as in
// compute_subplot_layout(): the rows may grow past the region, which then
// scrolls.  Row-major, row 0 at the top.
std::vector<Rect> compute_grid_layout(float              width,
                                      float              height,
                                      int                rows,
                                      int                cols,
                                      const GridSpacing& spacing,
                                      float              origin_x              = 0.0f,
                                      float              origin_y              = 0.0f,
                                      float              min_subplot_height    = 0.0f,
                                      float*             actual_content_height = nullptr);

}   // namespace spectra
