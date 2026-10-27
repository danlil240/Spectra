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
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

namespace spectra::legend_layout
{

// Spacing shared by both legend implementations (pixels).
inline constexpr float MARGIN      = 12.0f;   // legend box inset from the axes viewport edge
inline constexpr float COL_GAP     = 14.0f;   // gap between legend columns
inline constexpr float CLAMP_INSET = 4.0f;    // minimum inset after drag offsets / clamping

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

// Number of (sampled) data points of visible line/scatter series that fall
// inside a legend of size w x h placed at the given corner.
inline size_t overlap_score(const Axes&    axes,
                            const Rect&    vp,
                            LegendPosition pos,
                            float          w,
                            float          h,
                            float          margin,
                            size_t         max_samples_per_series = 4096)
{
    const Rect r = detail::corner_rect(pos, vp, w, h, margin);

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

    size_t count = 0;
    for (const auto& sp : axes.series())
    {
        if (!sp || !sp->visible())
            continue;
        std::span<const float> xs;
        std::span<const float> ys;
        bool                   filled = false;   // outline encloses a filled area
        if (auto* bs = dynamic_cast<const BarSeries*>(sp.get()))
        {
            xs     = bs->x_data();
            ys     = bs->y_data();
            filled = true;
        }
        else if (auto* hs = dynamic_cast<const HistogramSeries*>(sp.get()))
        {
            xs     = hs->x_data();
            ys     = hs->y_data();
            filled = true;
        }
        else if (auto* bp = dynamic_cast<const BoxPlotSeries*>(sp.get()))
        {
            xs     = bp->x_data();
            ys     = bp->y_data();
            filled = true;
        }
        else if (auto* vn = dynamic_cast<const ViolinSeries*>(sp.get()))
        {
            xs     = vn->x_data();
            ys     = vn->y_data();
            filled = true;
        }
        if (filled)
        {
            // Filled shapes: probe a grid of points inside the legend rect
            // and count those that fall inside any outline polygon (polygons
            // are separated by NaN breaks in the outline data).
            std::vector<std::vector<std::pair<float, float>>> polys(1);
            const size_t                                      n    = std::min(xs.size(), ys.size());
            const double                                      xoff = sp->x_offset();
            for (size_t i = 0; i < n; ++i)
            {
                double sx = 0.0, sy = 0.0;
                if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])
                    || !detail::apply_scale(axes.xscale(), xoff + xs[i], sx)
                    || !detail::apply_scale(axes.yscale(), ys[i], sy))
                {
                    if (!polys.back().empty())
                        polys.emplace_back();
                    continue;
                }
                const float px = vp.x + static_cast<float>((sx - x0) / dx) * vp.w;
                const float py = vp.y + static_cast<float>(1.0 - (sy - y0) / dy) * vp.h;
                // Outline vertices (whisker ends, bar corners) count like points.
                if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h)
                    ++count;
                polys.back().emplace_back(px, py);
            }
            constexpr int PROBES_X = 8;
            constexpr int PROBES_Y = 6;
            for (int gy = 0; gy < PROBES_Y; ++gy)
            {
                const float py = r.y + (static_cast<float>(gy) + 0.5f) * r.h / PROBES_Y;
                for (int gx = 0; gx < PROBES_X; ++gx)
                {
                    const float px     = r.x + (static_cast<float>(gx) + 0.5f) * r.w / PROBES_X;
                    bool        inside = false;
                    for (const auto& poly : polys)
                    {
                        const size_t m = poly.size();
                        for (size_t a = 0, b = m - 1; a < m; b = a++)
                        {
                            const auto [ax, ay] = poly[a];
                            const auto [bx, by] = poly[b];
                            if (((ay > py) != (by > py))
                                && (px < (bx - ax) * (py - ay) / (by - ay) + ax))
                                inside = !inside;
                        }
                        if (inside)
                            break;
                    }
                    if (inside)
                        count += 4;   // covering a filled area is worse than a stray point
                }
            }
            continue;
        }
        if (auto* ls = dynamic_cast<const LineSeries*>(sp.get()))
        {
            xs = ls->x_data();
            ys = ls->y_data();
        }
        else if (auto* ss = dynamic_cast<const ScatterSeries*>(sp.get()))
        {
            xs = ss->x_data();
            ys = ss->y_data();
        }
        else
        {
            continue;
        }
        const size_t n      = std::min(xs.size(), ys.size());
        const size_t stride = std::max<size_t>(1, n / std::max<size_t>(1, max_samples_per_series));
        const double xoff   = sp->x_offset();
        for (size_t i = 0; i < n; i += stride)
        {
            double sx = 0.0, sy = 0.0;
            if (!detail::apply_scale(axes.xscale(), xoff + xs[i], sx)
                || !detail::apply_scale(axes.yscale(), ys[i], sy))
                continue;
            const float px = vp.x + static_cast<float>((sx - x0) / dx) * vp.w;
            const float py = vp.y + static_cast<float>(1.0 - (sy - y0) / dy) * vp.h;
            if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h)
                ++count;
        }
    }
    return count;
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
