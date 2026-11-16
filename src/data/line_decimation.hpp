#pragma once

// Geometry helpers for drawing 2D lines, shared by the GPU renderer and the
// SVG/PDF exporters so both draw the same path:
//  - step expansion (DrawStyle::StepsPre / StepsMid / StepsPost),
//  - M4 decimation (first / min / max / last point per pixel column), the
//    automatic visually-lossless reduction of very dense lines,
//  - the translucency of DenseLineMode::Density.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <spectra/plot_style.hpp>
#include <vector>

namespace spectra::data
{

// ─── Steps ──────────────────────────────────────────────────────────────────

// True for the step draw styles.
constexpr bool is_step_style(DrawStyle s)
{
    return s == DrawStyle::StepsPre || s == DrawStyle::StepsMid || s == DrawStyle::StepsPost;
}

// Vertices per interval (between two data points) of a step-expanded line:
// the data point plus its corner(s).
constexpr size_t step_vertices_per_point(DrawStyle s)
{
    if (s == DrawStyle::StepsMid)
        return 3;
    return is_step_style(s) ? 2 : 1;
}

// Number of vertices of the step expansion of an n-point line.
constexpr size_t step_vertex_count(size_t n, DrawStyle s)
{
    if (n == 0)
        return 0;
    return (n - 1) * step_vertices_per_point(s) + 1;
}

// Index of data point i among the expanded vertices.
constexpr size_t step_vertex_index(size_t i, DrawStyle s)
{
    return i * step_vertices_per_point(s);
}

// Append the expansion of the line through (x, y) to (out_x, out_y):
//   StepsPre:  p0, (x0, y1), p1, (x1, y2), p2 ...    (vertical, then horizontal)
//   StepsPost: p0, (x1, y0), p1, (x2, y1), p2 ...    (horizontal, then vertical)
//   StepsMid:  p0, (m01, y0), (m01, y1), p1 ...      (m01 = (x0 + x1) / 2)
//   Default:   the points unchanged.
// A NaN coordinate makes its corners NaN too, so line gaps survive.
void expand_steps(std::span<const float> x,
                  std::span<const float> y,
                  DrawStyle              style,
                  std::vector<float>&    out_x,
                  std::vector<float>&    out_y);

// Points of matplotlib's stairs(values, edges): a post-step line through
// (edges[i], values[i]) that ends at (edges[n], values[n-1]).  `edges` must
// hold values.size() + 1 entries (otherwise the result is empty).
void stairs_points(std::span<const float> values,
                   std::span<const float> edges,
                   std::vector<float>&    out_x,
                   std::vector<float>&    out_y);

// ─── M4 decimation ──────────────────────────────────────────────────────────

// Decimate only when the visible points outnumber the pixel columns by this
// factor: M4 keeps at most four points per column, so below that it saves
// little and the full line is cheap anyway.
inline constexpr size_t M4_MIN_POINTS_PER_COLUMN = 8;

inline bool should_m4_decimate(size_t visible_points, size_t columns)
{
    return columns > 0 && visible_points > columns * M4_MIN_POINTS_PER_COLUMN;
}

// M4 decimation of points [begin, end) of a line whose x is sorted.
// `column_of(i)` returns point i's pixel column as a double (any value; NaN
// or +/-inf for a non-finite x); `y` holds the y values.  For every column the first, lowest,
// highest and last point are kept, in data order, so a line through the kept
// points covers exactly the same pixels as the line through all of them (for
// lines at least one pixel wide).  Columns before 0 and from `columns` on are
// merged into one column on each side, keeping the segments that enter and
// leave the view.  Points with a NaN or infinite x or y end the current
// column and are kept as gap markers (the line pipeline breaks the line at
// them), so line gaps survive and never count as a column's min or max.
// The kept indices are appended to `out` in increasing order.
template <typename ColumnOf>
void m4_decimate(std::span<const float> y,
                 size_t                 begin,
                 size_t                 end,
                 size_t                 columns,
                 ColumnOf&&             column_of,
                 std::vector<uint32_t>& out)
{
    end = std::min(end, y.size());
    if (begin >= end)
        return;
    const double max_col = static_cast<double>(columns);

    bool     open    = false;
    int64_t  cur_col = 0;
    uint32_t first = 0, last = 0, lo = 0, hi = 0;
    auto     flush = [&]()
    {
        if (!open)
            return;
        open       = false;
        uint32_t a = std::min(lo, hi);
        uint32_t b = std::max(lo, hi);
        out.push_back(first);
        if (a != first)
            out.push_back(a);
        if (b != a && b != first)
            out.push_back(b);
        if (last != b && last != first)
            out.push_back(last);
    };

    for (size_t i = begin; i < end; ++i)
    {
        const auto   idx = static_cast<uint32_t>(i);
        const float  yi  = y[i];
        const double c   = column_of(i);
        // NaN and +/-inf break the line (line.vert draws neither), so they
        // end the column and are kept as gap markers, never as extrema.
        if (!std::isfinite(yi) || !std::isfinite(c))
        {
            flush();
            out.push_back(idx);   // gap marker
            continue;
        }
        const int64_t col =
            c < 0.0 ? -1 : (c >= max_col ? static_cast<int64_t>(columns) : static_cast<int64_t>(c));
        if (!open || col != cur_col)
        {
            flush();
            open    = true;
            cur_col = col;
            first = last = lo = hi = idx;
            continue;
        }
        last = idx;
        if (yi < y[lo])
            lo = idx;
        if (yi > y[hi])
            hi = idx;
    }
    flush();
}

// ─── Density mode ───────────────────────────────────────────────────────────

// Per-segment opacity of DenseLineMode::Density for `visible_points` spread
// over `columns` pixel columns: 1 while there are no more points than
// columns, then falling with the square root of the points per column, so
// only the most visited areas build up to a solid colour and sparse
// excursions stay faint.
inline float density_alpha(size_t visible_points, size_t columns)
{
    constexpr float MIN_ALPHA = 0.01f;
    constexpr float SCALE     = 0.4f;
    if (columns == 0 || visible_points <= columns)
        return 1.0f;
    const double per_column = static_cast<double>(visible_points) / static_cast<double>(columns);
    return std::clamp(static_cast<float>(SCALE / std::sqrt(per_column)), MIN_ALPHA, 1.0f);
}

}   // namespace spectra::data
