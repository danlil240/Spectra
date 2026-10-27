#pragma once

// Pure (ImGui-free) legend layout helpers: multi-column grid sizing so that
// legends never silently truncate entries, and "best" corner selection that
// minimises overlap with plotted data.  Shared by the interactive ImGui legend
// (LegendInteraction) and the renderer's legend for headless/export output
// (Renderer::render_legends) so both place the legend the same way.  Kept
// header-only and independent of ImGui so it can be unit tested.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

namespace spectra::legend_layout
{

// Spacing shared by both legend implementations (pixels).
inline constexpr float MARGIN      = 12.0f;   // legend box inset from the axes viewport edge
inline constexpr float COL_GAP     = 14.0f;   // gap between legend columns
inline constexpr float CLAMP_INSET = 4.0f;    // minimum inset after drag offsets / clamping
inline constexpr float CLEARANCE   = 8.0f;    // "Best" also avoids data this close to the box

struct GridLayout
{
    int   entries      = 0;       // number of legend entries
    int   rows         = 0;       // rows per column (entry i -> col i / rows, row i % rows)
    int   cols         = 1;       // number of columns
    int   visible_rows = 0;       // rows that fit without scrolling
    bool  scroll       = false;   // true if rows > visible_rows (content scrolls)
    float width        = 0.0f;    // total legend width incl. padding
    float height       = 0.0f;    // total legend height incl. padding
};

// Lay out n entries of width col_w / height row_h into as few columns as
// possible, keeping each column at most max_rows_per_col tall and the whole
// legend within max_w x max_h.  Only when even the widest grid that fits
// max_w cannot hold every entry does the layout fall back to scrolling.
inline GridLayout compute_grid(int   n,
                               float col_w,
                               float row_h,
                               float pad_x,
                               float pad_y,
                               float col_gap,
                               float max_w,
                               float max_h,
                               int   max_rows_per_col = 12)
{
    GridLayout g;
    g.entries = n;
    if (n <= 0 || row_h <= 0.0f)
        return g;

    const int rows_fit = std::max(1, static_cast<int>(std::floor((max_h - 2.0f * pad_y) / row_h)));
    const int rows_cap = std::max(1, std::min(rows_fit, max_rows_per_col));

    auto width_for = [&](int c)
    { return 2.0f * pad_x + static_cast<float>(c) * col_w + static_cast<float>(c - 1) * col_gap; };

    int cols = (n + rows_cap - 1) / rows_cap;
    if (cols > 1 && width_for(cols) > max_w)
    {
        // Too wide: use as many columns as fit, then let the rows grow up to
        // the available height (and scroll only beyond that).
        const int cols_fit =
            static_cast<int>(std::floor((max_w - 2.0f * pad_x + col_gap) / (col_w + col_gap)));
        cols = std::max(1, std::min(cols, cols_fit));
    }
    const int rows = (n + cols - 1) / cols;   // balance entries across columns

    g.cols         = cols;
    g.rows         = rows;
    g.visible_rows = std::min(rows, rows_fit);
    g.scroll       = rows > rows_fit;
    g.width        = width_for(cols);
    g.height       = 2.0f * pad_y + static_cast<float>(g.visible_rows) * row_h;
    return g;
}

namespace detail
{
inline bool apply_scale(ScaleType s, double v, double& out)
{
    switch (s)
    {
        case ScaleType::Log10:
            if (!(v > 0.0))
                return false;
            out = std::log10(v);
            return true;
        case ScaleType::Log2:
            if (!(v > 0.0))
                return false;
            out = std::log2(v);
            return true;
        case ScaleType::Sqrt:
            if (v < 0.0)
                return false;
            out = std::sqrt(v);
            return true;
        default:
            out = v;
            return std::isfinite(v);
    }
}

inline Rect corner_rect(LegendPosition pos, const Rect& vp, float w, float h, float margin)
{
    Rect r;
    r.w = w;
    r.h = h;
    switch (pos)
    {
        case LegendPosition::TopLeft:
            r.x = vp.x + margin;
            r.y = vp.y + margin;
            break;
        case LegendPosition::BottomLeft:
            r.x = vp.x + margin;
            r.y = vp.y + vp.h - h - margin;
            break;
        case LegendPosition::BottomRight:
            r.x = vp.x + vp.w - w - margin;
            r.y = vp.y + vp.h - h - margin;
            break;
        case LegendPosition::TopRight:
        default:
            r.x = vp.x + vp.w - w - margin;
            r.y = vp.y + margin;
            break;
    }
    return r;
}
}   // namespace detail

// Candidate corners for LegendPosition::Best, in tie-break preference order.
inline constexpr std::array<LegendPosition, 4> BEST_CANDIDATES = {
    LegendPosition::TopRight,
    LegendPosition::TopLeft,
    LegendPosition::BottomLeft,
    LegendPosition::BottomRight,
};

// Overlap weight of a legend that is fully covered by filled series area.  A
// partly covered legend scores proportionally (covering half of it is worth
// ~100 stray points), so "Best" avoids bars, boxes, violins, histograms,
// bands and shapes rather than only their outline vertices.
inline constexpr float FILL_OVERLAP_WEIGHT = 200.0f;

namespace detail
{
// The geometry of one 2D series as the placement scorer sees it: sampled
// points (lines, markers, outlines, outliers) and filled triangles
// (interleaved x, y, alpha per vertex, in data coordinates).
struct ScoredGeometry
{
    std::span<const float> xs;
    std::span<const float> ys;
    std::span<const float> extra_xs;   // e.g. box-plot outliers
    std::span<const float> extra_ys;
    std::span<const float> fill_verts;
};

inline bool scored_geometry(const Series& s, ScoredGeometry& g)
{
    if (auto* ls = dynamic_cast<const LineSeries*>(&s))
    {
        g.xs = ls->x_data();
        g.ys = ls->y_data();
    }
    else if (auto* ss = dynamic_cast<const ScatterSeries*>(&s))
    {
        g.xs = ss->x_data();
        g.ys = ss->y_data();
    }
    else if (auto* bs = dynamic_cast<const BarSeries*>(&s))
    {
        g.xs         = bs->x_data();
        g.ys         = bs->y_data();
        g.fill_verts = bs->fill_verts();
    }
    else if (auto* hs = dynamic_cast<const HistogramSeries*>(&s))
    {
        g.xs         = hs->x_data();
        g.ys         = hs->y_data();
        g.fill_verts = hs->fill_verts();
    }
    else if (auto* bp = dynamic_cast<const BoxPlotSeries*>(&s))
    {
        g.xs         = bp->x_data();
        g.ys         = bp->y_data();
        g.extra_xs   = bp->outlier_x();
        g.extra_ys   = bp->outlier_y();
        g.fill_verts = bp->fill_verts();
    }
    else if (auto* vn = dynamic_cast<const ViolinSeries*>(&s))
    {
        g.xs         = vn->x_data();
        g.ys         = vn->y_data();
        g.fill_verts = vn->fill_verts();
    }
    else if (auto* bd = dynamic_cast<const BandSeries*>(&s))
    {
        g.xs         = bd->x_data();
        g.ys         = bd->y_data();
        g.fill_verts = bd->fill_verts();
    }
    else if (auto* sh = dynamic_cast<const ShapeSeries*>(&s))
    {
        g.xs         = sh->x_data();
        g.ys         = sh->y_data();
        g.fill_verts = sh->fill_verts();
    }
    else if (auto* st = dynamic_cast<const StemSeries*>(&s))
    {
        g.xs = st->x_data();
        g.ys = st->y_data();
    }
    else
    {
        return false;
    }
    return true;
}

// Area of the intersection of triangle (a, b, c) with rectangle r, by
// clipping the triangle against the four rectangle edges (Sutherland-Hodgman).
inline float triangle_rect_overlap(const std::array<float, 6>& tri, const Rect& r)
{
    // A triangle clipped by 4 half-planes has at most 7 vertices.
    std::array<float, 2 * 8> in{};
    std::array<float, 2 * 8> out{};
    int                      n = 3;
    for (int i = 0; i < 6; ++i)
        in[i] = tri[i];

    const float lo[2] = {r.x, r.y};
    const float hi[2] = {r.x + r.w, r.y + r.h};
    for (int edge = 0; edge < 4 && n > 0; ++edge)
    {
        const int   axis   = edge & 1;   // 0: x, 1: y
        const bool  is_min = edge < 2;   // keep v >= lo, else v <= hi
        const float bound  = is_min ? lo[axis] : hi[axis];
        auto        inside = [&](int k)
        { return is_min ? in[2 * k + axis] >= bound : in[2 * k + axis] <= bound; };
        int m = 0;
        for (int k = 0; k < n; ++k)
        {
            const int  j  = (k + n - 1) % n;   // previous vertex
            const bool ik = inside(k);
            const bool ij = inside(j);
            if (ik != ij)
            {
                const float t  = (bound - in[2 * j + axis]) / (in[2 * k + axis] - in[2 * j + axis]);
                out[2 * m]     = in[2 * j] + t * (in[2 * k] - in[2 * j]);
                out[2 * m + 1] = in[2 * j + 1] + t * (in[2 * k + 1] - in[2 * j + 1]);
                ++m;
            }
            if (ik)
            {
                out[2 * m]     = in[2 * k];
                out[2 * m + 1] = in[2 * k + 1];
                ++m;
            }
        }
        n  = m;
        in = out;
    }
    float area2 = 0.0f;   // shoelace
    for (int k = 0; k < n; ++k)
    {
        const int j = (k + 1) % n;
        area2 += in[2 * k] * in[2 * j + 1] - in[2 * j] * in[2 * k + 1];
    }
    return 0.5f * std::abs(area2);
}
}   // namespace detail

// Overlap of a legend of size w x h placed at the given corner with the
// visible 2D series: one per (sampled) data / outline point inside the
// legend (grown by CLEARANCE), plus FILL_OVERLAP_WEIGHT times the fraction of the legend covered
// by filled areas (bars, histograms, boxes, violins, bands, shapes).
inline size_t overlap_score(const Axes&    axes,
                            const Rect&    vp,
                            LegendPosition pos,
                            float          w,
                            float          h,
                            float          margin,
                            size_t         max_samples_per_series = 4096)
{
    // Score a slightly inflated box so data that merely grazes the legend's
    // edge (a whisker tip, a violin's tail) also counts as crowding it.
    Rect r = detail::corner_rect(pos, vp, w, h, margin);
    r.x -= CLEARANCE;
    r.y -= CLEARANCE;
    r.w += 2.0f * CLEARANCE;
    r.h += 2.0f * CLEARANCE;

    const AxisLimits xl = axes.x_limits();
    const AxisLimits yl = axes.y_limits();
    double           x0 = 0.0, x1 = 1.0, y0 = 0.0, y1 = 1.0;
    if (!detail::apply_scale(axes.xscale(), xl.min, x0)
        || !detail::apply_scale(axes.xscale(), xl.max, x1)
        || !detail::apply_scale(axes.yscale(), yl.min, y0)
        || !detail::apply_scale(axes.yscale(), yl.max, y1))
        return 0;
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    if (!(std::abs(dx) > 0.0) || !(std::abs(dy) > 0.0) || vp.w <= 0.0f || vp.h <= 0.0f)
        return 0;

    double xoff     = 0.0;
    auto   to_pixel = [&](double x, double y, float& px, float& py)
    {
        double sx = 0.0, sy = 0.0;
        if (!detail::apply_scale(axes.xscale(), xoff + x, sx)
            || !detail::apply_scale(axes.yscale(), y, sy))
            return false;
        px = vp.x + static_cast<float>((sx - x0) / dx) * vp.w;
        py = vp.y + static_cast<float>(1.0 - (sy - y0) / dy) * vp.h;
        return true;
    };
    auto in_rect = [&](float px, float py)
    { return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h; };
    auto count_points = [&](std::span<const float> xs, std::span<const float> ys)
    {
        size_t       c      = 0;
        const size_t n      = std::min(xs.size(), ys.size());
        const size_t stride = std::max<size_t>(1, n / std::max<size_t>(1, max_samples_per_series));
        for (size_t i = 0; i < n; i += stride)
        {
            float px = 0.0f, py = 0.0f;
            if (to_pixel(xs[i], ys[i], px, py) && in_rect(px, py))
                ++c;
        }
        return c;
    };

    size_t count        = 0;
    float  covered_area = 0.0f;   // legend pixels covered by filled areas
    for (const auto& sp : axes.series())
    {
        detail::ScoredGeometry g;
        if (!sp || !sp->visible() || !detail::scored_geometry(*sp, g))
            continue;
        xoff = sp->x_offset();
        count += count_points(g.xs, g.ys);
        count += count_points(g.extra_xs, g.extra_ys);

        const size_t tris = g.fill_verts.size() / 9;   // 3 vertices of {x, y, alpha}
        for (size_t t = 0; t < tris; ++t)
        {
            std::array<float, 6> tri{};
            bool                 ok = true;
            for (int v = 0; v < 3 && ok; ++v)
            {
                const float* vert = g.fill_verts.data() + (t * 3 + v) * 3;
                ok                = to_pixel(vert[0], vert[1], tri[2 * v], tri[2 * v + 1]);
            }
            if (!ok)
                continue;
            // Cheap bounding-box reject before clipping.
            const float min_x = std::min({tri[0], tri[2], tri[4]});
            const float max_x = std::max({tri[0], tri[2], tri[4]});
            const float min_y = std::min({tri[1], tri[3], tri[5]});
            const float max_y = std::max({tri[1], tri[3], tri[5]});
            if (max_x < r.x || min_x > r.x + r.w || max_y < r.y || min_y > r.y + r.h)
                continue;
            covered_area += detail::triangle_rect_overlap(tri, r);
        }
    }
    const float coverage = std::min(1.0f, covered_area / (r.w * r.h));
    return count + static_cast<size_t>(std::lround(coverage * FILL_OVERLAP_WEIGHT));
}

// Pick the corner whose legend rectangle covers the fewest data points.
// `current` (if a real corner) is kept unless another corner is clearly
// better, which stops the legend from jumping around on streaming data.
inline LegendPosition choose_best_corner(const Axes&    axes,
                                         const Rect&    vp,
                                         float          w,
                                         float          h,
                                         float          margin,
                                         LegendPosition current = LegendPosition::None)
{
    LegendPosition best       = BEST_CANDIDATES[0];
    size_t         best_score = static_cast<size_t>(-1);
    size_t         cur_score  = static_cast<size_t>(-1);
    for (LegendPosition pos : BEST_CANDIDATES)
    {
        const size_t s = overlap_score(axes, vp, pos, w, h, margin);
        if (pos == current)
            cur_score = s;
        if (s < best_score)
        {
            best_score = s;
            best       = pos;
        }
    }
    if (cur_score != static_cast<size_t>(-1))
    {
        // Hysteresis: only move if it removes a meaningful amount of overlap.
        if (cur_score <= best_score + 2 || cur_score * 4 <= best_score * 5)
            return current;
    }
    return best;
}

// Resolve a configured position to a concrete corner.  Best picks the corner
// with the least data overlap (with hysteresis against `previous`); None is
// returned unchanged (callers hide the legend).
inline LegendPosition resolve_position(const Axes&    axes,
                                       const Rect&    vp,
                                       float          w,
                                       float          h,
                                       LegendPosition requested,
                                       LegendPosition previous = LegendPosition::None)
{
    if (requested != LegendPosition::Best)
        return requested;
    return choose_best_corner(axes, vp, w, h, MARGIN, previous);
}

// Final legend rectangle: the corner placement shifted by an optional user
// drag offset, then clamped to stay inside the viewport.
inline Rect place(const Rect&    vp,
                  LegendPosition corner,
                  float          w,
                  float          h,
                  float          dx = 0.0f,
                  float          dy = 0.0f)
{
    Rect r = detail::corner_rect(corner, vp, w, h, MARGIN);
    r.x += dx;
    r.y += dy;
    r.x = std::max(vp.x + CLAMP_INSET, std::min(r.x, vp.x + vp.w - w - CLAMP_INSET));
    r.y = std::max(vp.y + CLAMP_INSET, std::min(r.y, vp.y + vp.h - h - CLAMP_INSET));
    return r;
}

}   // namespace spectra::legend_layout
