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
#include <string>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "io/text_metrics.hpp"

namespace spectra::legend_layout
{

// Spacing shared by both legend implementations (pixels).
inline constexpr float MARGIN      = 12.0f;   // legend box inset from the axes viewport edge
inline constexpr float COL_GAP     = 14.0f;   // gap between legend columns
inline constexpr float CLAMP_INSET = 4.0f;    // minimum inset after drag offsets / clamping
inline constexpr float CLEARANCE   = 8.0f;    // "Best" also avoids data this close to the box
// LegendPosition::OutsideRight: gap between the plot area (or its colorbar)
// and the legend box, and the largest share of a layout cell it may take.
inline constexpr float OUTSIDE_GAP          = 12.0f;
inline constexpr float OUTSIDE_MAX_FRACTION = 0.4f;
// Widest swatch column (+ visibility eye) of the three legend drawers, and the
// label size assumed when LegendConfig::font_size is automatic (Label tier).
inline constexpr float OUTSIDE_SWATCH_W     = 36.0f;
inline constexpr float OUTSIDE_AUTO_FONT_PX = 14.0f;
// "Best": one overlap point per this many pixels of line running under the legend.
inline constexpr float SEGMENT_PX_PER_POINT = 4.0f;

struct GridLayout
{
    int   entries      = 0;       // number of legend entries
    int   rows         = 0;       // rows per column (entry i -> col i / rows, row i % rows)
    int   cols         = 1;       // number of columns
    int   visible_rows = 0;       // rows that fit without scrolling
    bool  scroll       = false;   // true if rows > visible_rows (content scrolls)
    float width        = 0.0f;    // total legend width incl. padding
    float height       = 0.0f;    // total legend height incl. padding
    float cell_w       = 0.0f;    // column width used for the layout
    float cell_h       = 0.0f;    // row height used for the layout
    int   shown        = 0;       // entries laid out (all of them unless overflow > 0)
    int   overflow     = 0;       // entries summarised by a "+N more" cell
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

    // A single column never grows wider than the legend region: callers fit
    // (ellipsize) labels to column_width() when drawing (audit S04).
    col_w = std::min(col_w, std::max(0.0f, max_w - 2.0f * pad_x));

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
    g.cell_w       = col_w;
    g.cell_h       = row_h;
    g.shown        = n;
    return g;
}

// Text of the overflow indicator cell drawn by compute_static_grid layouts.
inline std::string overflow_text(int hidden)
{
    return "+" + std::to_string(hidden) + " more";
}

// Lines of the hover tooltip of a "+N more" cell: the labels of the entries
// the cell summarises (entries[shown..]), in legend order.  At most
// `max_lines` labels are listed; the rest are counted on a final
// "... and K more" line so the tooltip itself stays bounded.
inline std::vector<std::string> overflow_tooltip_lines(std::span<const std::string> labels,
                                                       int                          shown,
                                                       int                          max_lines = 20)
{
    std::vector<std::string> out;
    const int                n      = static_cast<int>(labels.size());
    const int                first  = std::clamp(shown, 0, n);
    const int                hidden = n - first;
    if (hidden <= 0)
        return out;
    // List every label when only one more would be summarised by the last line.
    const int listed = hidden <= max_lines + 1 ? hidden : std::max(0, max_lines);
    out.assign(labels.begin() + first, labels.begin() + first + listed);
    if (listed < hidden)
        out.push_back("\xE2\x80\xA6 and " + std::to_string(hidden - listed) + " more");
    return out;
}

inline std::vector<std::string> overflow_tooltip_lines(std::span<Series* const> entries,
                                                       int                      shown,
                                                       int                      max_lines = 20)
{
    std::vector<std::string> labels;
    labels.reserve(entries.size());
    for (const Series* s : entries)
        labels.push_back(s ? s->label() : std::string{});
    return overflow_tooltip_lines(std::span<const std::string>(labels), shown, max_lines);
}

// Static (non-scrollable) legend layout for image export (PNG / GPU
// readback), where a scrolling legend would silently lose entries.  Policy:
//   1. Use the regular compute_grid layout when it needs no scrolling.
//   2. Otherwise retry with the tighter row height `min_row_h`.
//   3. If every entry still cannot fit, fill every visible cell but the last
//      with entries (column-major, in order) and use the last cell for an
//      explicit "+N more" indicator (overflow_text), so omitted entries are
//      always reported rather than dropped silently.  `overflow_w` is the
//      width of the widest possible indicator text; columns are widened to
//      hold it if needed.
// The result never scrolls: rows == visible_rows, and `shown` + `overflow`
// == n.  The interactive ImGui legend keeps using compute_grid and scrolls.
//
// Font size (shared by the GPU/PNG and SVG legends): an explicit
// LegendConfig::font_size is used exactly, with row_h = font_size + 6 and
// min_row_h = font_size + 2 (the window legend's row height too), and is
// never shrunk to make more entries fit.  Only the automatic size (0) may
// fall back to a smaller font before entries are summarised; the SVG auto
// size (12.5 px) is already below the GPU tick font, so only the GPU path
// has a smaller font to try.
inline GridLayout compute_static_grid(int   n,
                                      float col_w,
                                      float row_h,
                                      float min_row_h,
                                      float overflow_w,
                                      float pad_x,
                                      float pad_y,
                                      float col_gap,
                                      float max_w,
                                      float max_h,
                                      int   max_rows_per_col = 12)
{
    GridLayout g =
        compute_grid(n, col_w, row_h, pad_x, pad_y, col_gap, max_w, max_h, max_rows_per_col);
    if (!g.scroll)
        return g;

    const float tight_h = min_row_h > 0.0f ? std::min(row_h, min_row_h) : row_h;
    if (tight_h < row_h)
    {
        g = compute_grid(n, col_w, tight_h, pad_x, pad_y, col_gap, max_w, max_h, max_rows_per_col);
        if (!g.scroll)
            return g;
    }

    const float cell_w = std::max(col_w, overflow_w);
    if (cell_w > col_w)
        g = compute_grid(n, cell_w, tight_h, pad_x, pad_y, col_gap, max_w, max_h, max_rows_per_col);
    if (!g.scroll)   // a wider column can only reduce capacity, but be safe
        return g;

    const int slots = g.cols * g.visible_rows;
    g.rows          = g.visible_rows;
    g.scroll        = false;
    g.shown         = std::max(0, std::min(n, slots - 1));
    g.overflow      = n - g.shown;
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
        case ScaleType::Sqrt:   // signed square root, as in core/axis_scale.hpp
            out = std::copysign(std::sqrt(std::abs(v)), v);
            return std::isfinite(v);
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
        case LegendPosition::OutsideRight:   // vp is the outside strip (legend_area)
            r.x = vp.x;
            r.y = vp.y;
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
    bool                   connected = false;   // xs/ys form a polyline (NaN = break)
    float                  radius    = 0.0f;    // marker radius in pixels (points)
};

inline bool scored_geometry(const Series& s, ScoredGeometry& g)
{
    if (auto* ls = dynamic_cast<const LineSeries*>(&s))
    {
        g.xs        = ls->x_data();
        g.ys        = ls->y_data();
        g.connected = ls->plot_style().has_line();
        if (ls->plot_style().has_marker())
            g.radius = 0.5f * ls->marker_size();
    }
    else if (auto* ss = dynamic_cast<const ScatterSeries*>(&s))
    {
        g.xs     = ss->x_data();
        g.ys     = ss->y_data();
        g.radius = 0.5f * std::max(ss->size(), ss->marker_size());
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
        g.xs        = st->x_data();
        g.ys        = st->y_data();
        g.connected = true;
    }
    else
    {
        return false;
    }
    return true;
}

// Length of segment (x0, y0)-(x1, y1) inside rectangle r (Liang-Barsky clip).
inline float segment_rect_length(float x0, float y0, float x1, float y1, const Rect& r)
{
    const float dx   = x1 - x0;
    const float dy   = y1 - y0;
    float       t0   = 0.0f;
    float       t1   = 1.0f;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - r.x, r.x + r.w - x0, y0 - r.y, r.y + r.h - y0};
    for (int i = 0; i < 4; ++i)
    {
        if (p[i] == 0.0f)
        {
            if (q[i] < 0.0f)
                return 0.0f;   // parallel and outside
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0f)
            t0 = std::max(t0, t);
        else
            t1 = std::min(t1, t);
        if (t0 > t1)
            return 0.0f;
    }
    return (t1 - t0) * std::sqrt(dx * dx + dy * dy);
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
// legend (grown by CLEARANCE, and by the marker radius for markers), one per
// SEGMENT_PX_PER_POINT pixels of line segment crossing it (so a sparse line
// that cuts through the box between two samples still counts), plus
// FILL_OVERLAP_WEIGHT times the fraction of the legend covered by filled
// areas (bars, histograms, boxes, violins, bands, shapes).
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
    double yoff     = 0.0;
    auto   to_pixel = [&](double x, double y, float& px, float& py)
    {
        double sx = 0.0, sy = 0.0;
        if (!detail::apply_scale(axes.xscale(), xoff + x, sx)
            || !detail::apply_scale(axes.yscale(), yoff + y, sy))
            return false;
        px = vp.x + static_cast<float>((sx - x0) / dx) * vp.w;
        py = vp.y + static_cast<float>(1.0 - (sy - y0) / dy) * vp.h;
        return true;
    };
    float radius  = 0.0f;   // marker radius of the series being scored
    auto  in_rect = [&](float px, float py)
    {
        return px >= r.x - radius && px <= r.x + r.w + radius && py >= r.y - radius
               && py <= r.y + r.h + radius;
    };
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
    // Pixels of polyline inside the box (between consecutive sampled points).
    auto segment_length = [&](std::span<const float> xs, std::span<const float> ys)
    {
        float        len    = 0.0f;
        const size_t n      = std::min(xs.size(), ys.size());
        const size_t stride = std::max<size_t>(1, n / std::max<size_t>(1, max_samples_per_series));
        bool         have   = false;
        float        lx = 0.0f, ly = 0.0f;
        for (size_t i = 0; i < n; i += stride)
        {
            float px = 0.0f, py = 0.0f;
            if (!std::isfinite(xs[i]) || !std::isfinite(ys[i]) || !to_pixel(xs[i], ys[i], px, py))
            {
                have = false;   // NaN gap or unplottable point breaks the line
                continue;
            }
            if (have)
                len += detail::segment_rect_length(lx, ly, px, py, r);
            lx   = px;
            ly   = py;
            have = true;
        }
        return len;
    };

    size_t count        = 0;
    float  covered_area = 0.0f;   // legend pixels covered by filled areas
    float  line_px      = 0.0f;   // pixels of line segments under the legend
    for (const auto& sp : axes.series())
    {
        detail::ScoredGeometry g;
        if (!sp || !sp->visible() || !detail::scored_geometry(*sp, g))
            continue;
        xoff   = sp->x_offset();
        yoff   = sp->y_offset();
        radius = g.radius;
        count += count_points(g.xs, g.ys);
        radius = 0.0f;
        count += count_points(g.extra_xs, g.extra_ys);
        if (g.connected)
            line_px += segment_length(g.xs, g.ys);

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
    return count + static_cast<size_t>(std::lround(coverage * FILL_OVERLAP_WEIGHT))
           + static_cast<size_t>(std::lround(line_px / SEGMENT_PX_PER_POINT));
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
// returned unchanged (callers hide the legend).  Data overlap is only scored
// for 2D axes: a 3D view's projection changes with the camera, so Best maps
// to TopRight there (as in the SVG exporter).
inline LegendPosition resolve_position(const AxesBase& axes,
                                       const Rect&     vp,
                                       float           w,
                                       float           h,
                                       LegendPosition  requested,
                                       LegendPosition  previous = LegendPosition::None)
{
    if (requested != LegendPosition::Best)
        return requested;
    if (const auto* a2 = dynamic_cast<const Axes*>(&axes))
        return choose_best_corner(*a2, vp, w, h, MARGIN, previous);
    return LegendPosition::TopRight;
}

// Region a legend lays itself out in: the plot area for the inside
// positions, the strip the figure layout reserved right of the axes
// (AxesBase::outside_legend_area(), minus OUTSIDE_GAP) for OutsideRight.
// Callers size the legend grid to this region and pass it to fits()/place().
inline Rect legend_area(const AxesBase& axes, LegendPosition pos)
{
    if (pos != LegendPosition::OutsideRight)
        return axes.viewport();
    Rect a = axes.outside_legend_area();
    a.x += OUTSIDE_GAP;
    a.w = std::max(0.0f, a.w - OUTSIDE_GAP);
    // Top-aligned with the plot area (the strip spans the layout cell).
    a.y = axes.viewport().y;
    a.h = axes.viewport().h;
    return a;
}

// Largest legend box (width, height) the grid layout may use inside
// legend_area(): the area minus the corner MARGIN on both sides for inside
// positions, the whole strip for OutsideRight.
inline float max_box_w(const Rect& area, LegendPosition pos)
{
    return pos == LegendPosition::OutsideRight ? area.w : area.w - 2.0f * MARGIN;
}
inline float max_box_h(const Rect& area, LegendPosition pos)
{
    return pos == LegendPosition::OutsideRight ? area.h : area.h - 2.0f * MARGIN;
}

// Series that get a legend entry, in axes order: labeled and not hidden from
// the legend.  Works for 2D and 3D axes alike, so the window (ImGui) and the
// GPU / PNG export legends list the same entries.  Hidden series stay listed
// (dimmed) so they can be toggled back on.
inline std::vector<Series*> legend_entries(const AxesBase& axes)
{
    std::vector<Series*> out;
    for (const auto& s : axes.series())
    {
        if (s && !s->label().empty() && s->show_in_legend())
            out.push_back(s.get());
    }
    return out;
}

// Base (un-themed) swatch colour of a series.  Colormapped surfaces are drawn
// from their colormap, not series.color(), so their swatch shows the middle
// of the map (as in the SVG exporter).
inline Color swatch_color(const Series& s)
{
    if (const auto* surf = dynamic_cast<const SurfaceSeries*>(&s);
        surf && surf->colormap_type() != ColormapType::None)
        return SurfaceSeries::sample_colormap(surf->colormap_type(), 0.5f);
    return s.color();
}

// Width of one legend column of grid `g` (compute_grid may have narrowed it
// below the requested col_w).  Labels must be fitted to this width minus the
// swatch / icon space so they stay inside the box.
inline float column_width(const GridLayout& g, float pad_x, float col_gap)
{
    if (g.cols <= 0)
        return 0.0f;
    const float inner = g.width - 2.0f * pad_x - static_cast<float>(g.cols - 1) * col_gap;
    return std::max(0.0f, inner / static_cast<float>(g.cols));
}

// Width the figure layout reserves on the right of a layout cell of width
// @p cell_w for an OutsideRight legend of @p axes: the gap plus a one-column
// legend box sized from the labels (Inter metrics, so window, PNG and SVG
// share one layout), capped at OUTSIDE_MAX_FRACTION of the cell.  0 when the
// legend is hidden, not OutsideRight, or the axes has no legend entries.
inline float outside_reserved_width(const AxesBase& axes, const LegendConfig& cfg, float cell_w)
{
    if (!cfg.visible || cfg.position != LegendPosition::OutsideRight || !(cell_w > 0.0f))
        return 0.0f;
    const auto entries = legend_entries(axes);
    if (entries.empty())
        return 0.0f;
    const float font_px = cfg.font_size > 0.0f ? cfg.font_size : OUTSIDE_AUTO_FONT_PX;
    float       label_w = 0.0f;
    for (const Series* s : entries)
        label_w = std::max(label_w, inter_text_width(s->label(), font_px));
    const float box_w = 2.0f * (cfg.padding + 2.0f) + OUTSIDE_SWATCH_W + label_w + 2.0f;
    return std::min(OUTSIDE_GAP + box_w + CLAMP_INSET, cell_w * OUTSIDE_MAX_FRACTION);
}

// True if a w x h legend fits inside the viewport with the CLAMP_INSET
// border.  Legends that do not (e.g. tiny subplot cells where not even one
// row fits) are skipped rather than drawn over neighbouring plots.
inline bool fits(const Rect& vp, float w, float h, LegendPosition pos = LegendPosition::TopRight)
{
    if (pos == LegendPosition::OutsideRight)   // vp is the outside strip: no inset
        return w > 0.0f && w <= vp.w + 0.5f && h <= vp.h;
    return w <= vp.w - 2.0f * CLAMP_INSET && h <= vp.h - 2.0f * CLAMP_INSET;
}

// Final legend rectangle: the corner placement shifted by an optional user
// drag offset, then clamped to stay inside the viewport.  The size is clamped
// too, so the result never extends past the viewport (check fits() first to
// know whether the content was shrunk).
inline Rect place(const Rect&    vp,
                  LegendPosition corner,
                  float          w,
                  float          h,
                  float          dx = 0.0f,
                  float          dy = 0.0f)
{
    if (corner == LegendPosition::OutsideRight)
    {
        // vp is the outside strip (legend_area): fill it from its top-left.
        w      = std::min(w, std::max(0.0f, vp.w));
        h      = std::min(h, std::max(0.0f, vp.h));
        Rect r = detail::corner_rect(corner, vp, w, h, 0.0f);
        r.x    = std::max(vp.x, std::min(r.x + dx, vp.x + vp.w - w));
        r.y    = std::max(vp.y, std::min(r.y + dy, vp.y + vp.h - h));
        return r;
    }
    w      = std::min(w, std::max(0.0f, vp.w - 2.0f * CLAMP_INSET));
    h      = std::min(h, std::max(0.0f, vp.h - 2.0f * CLAMP_INSET));
    Rect r = detail::corner_rect(corner, vp, w, h, MARGIN);
    r.x += dx;
    r.y += dy;
    r.x = std::max(vp.x + CLAMP_INSET, std::min(r.x, vp.x + vp.w - w - CLAMP_INSET));
    r.y = std::max(vp.y + CLAMP_INSET, std::min(r.y, vp.y + vp.h - h - CLAMP_INSET));
    return r;
}

// ── Label ink on a custom legend background ─────────────────────────────────
// WCAG 2.1 relative luminance / contrast of opaque sRGB colours (alpha ignored).
inline float wcag_luminance(const Color& c)
{
    auto lin = [](float v)
    { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}
inline float wcag_contrast(const Color& a, const Color& b)
{
    const float la = wcag_luminance(a), lb = wcag_luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}
// Source-over of `fg` onto opaque `bg` (encoded sRGB, like ImGui); result opaque.
inline Color composite_over(const Color& fg, const Color& bg)
{
    const float t = std::clamp(fg.a, 0.0f, 1.0f);
    return Color{fg.r * t + bg.r * (1.0f - t),
                 fg.g * t + bg.g * (1.0f - t),
                 fg.b * t + bg.b * (1.0f - t),
                 1.0f};
}

inline constexpr float LABEL_MIN_CONTRAST = 4.5f;
inline constexpr Color LABEL_INK_DARK{0.075f, 0.085f, 0.105f, 1.0f};
inline constexpr Color LABEL_INK_LIGHT{0.960f, 0.968f, 0.980f, 1.0f};

// Label ink for a legend box filled with `box_bg` (possibly translucent) drawn
// over the opaque `canvas`: `ink` when it reaches 4.5:1 against that effective
// background, otherwise near-black or near-white, whichever contrasts more.
// `ink`'s alpha is kept so hidden-series dimming still applies.
inline Color label_ink(const Color& ink, const Color& box_bg, const Color& canvas)
{
    const Color eff = composite_over(box_bg, Color{canvas.r, canvas.g, canvas.b, 1.0f});
    if (wcag_contrast(composite_over(ink, eff), eff) >= LABEL_MIN_CONTRAST)
        return ink;
    const Color pick = wcag_contrast(LABEL_INK_DARK, eff) >= wcag_contrast(LABEL_INK_LIGHT, eff)
                           ? LABEL_INK_DARK
                           : LABEL_INK_LIGHT;
    return Color{pick.r, pick.g, pick.b, ink.a};
}

}   // namespace spectra::legend_layout
