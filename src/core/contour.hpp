#pragma once

// Marching squares iso-lines and contour level selection (GPU-free, used by
// ContourSeries and the unit tests).

#include <cstddef>
#include <span>
#include <vector>

namespace spectra::contour
{

/// One iso-line in grid-index space: x is a fractional column, y a fractional
/// row (node (c, r) is at (c, r)).  A closed loop repeats its first point.
struct Polyline
{
    std::vector<float> x;
    std::vector<float> y;
    bool               closed = false;
};

/// Iso-lines of @p level through the row-major grid @p z (cols x rows).
/// Uses marching squares with linear interpolation along the cell edges;
/// saddle cells are resolved with the cell-centre average.  A node counts as
/// "above" when z >= level.  Cells with a non-finite corner are skipped.
/// Segments are joined into polylines through the edges they share.
std::vector<Polyline> trace(std::span<const float> z, size_t cols, size_t rows, float level);

/// About @p n "nice" levels (1/2/2.5/5 x 10^k steps) for data in [zmin, zmax].
/// With @p cover_range false (contour lines) only levels strictly inside the
/// range are returned; with true (filled contours) the first level is <= zmin
/// and the last >= zmax, so the bands cover the whole range.  Empty for a
/// non-finite or flat range (cover_range: a single band around the value).
std::vector<float> auto_levels(float zmin, float zmax, int n, bool cover_range);

}   // namespace spectra::contour
