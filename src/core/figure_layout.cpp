#include "core/figure_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <spectra/figure.hpp>

#include "core/axis_scale.hpp"
#include "io/text_metrics.hpp"
#include "ui/overlay/legend_layout.hpp"

namespace spectra
{

namespace
{

// Renderer / SVG constants for the y axis decorations (render_geometry.cpp,
// svg_export.cpp): tick label padding, gap to the y label, label font.
constexpr float TICK_PADDING  = 5.0f;
constexpr float YLABEL_GAP    = 6.0f;
constexpr float TICK_FONT_PX  = 13.0f;
constexpr float LABEL_FONT_PX = 14.0f;
constexpr float TITLE_FONT_PX = 18.0f;
// Gap between the tick label row and the X label row (render_geometry.cpp).
constexpr float XLABEL_ROW_GAP = 4.0f;
// Space above the suptitle.
constexpr float SUPTITLE_PAD = 8.0f;
// Rotated label thickness: the renderer's line height for the Label tier.
constexpr float LABEL_LINE_FACTOR = 1.25f;

}   // namespace

float left_label_extent(const Axes& axes)
{
    float tick_w = 0.0f;
    if (axes.y_tick_labels_visible())
    {
        const TickResult ticks = axes.compute_y_ticks();
        for (const auto& l : ticks.labels)
            tick_w = std::max(tick_w, inter_text_width(l, TICK_FONT_PX));
    }
    float need = axes.axis_style().tick_length + TICK_PADDING + tick_w;
    if (!axes.ylabel().empty())
        need += YLABEL_GAP + LABEL_FONT_PX * LABEL_LINE_FACTOR;
    return need + LEFT_LABEL_OUTER_PAD;
}

AxesDecorations measure_decorations(const Axes& axes)
{
    AxesDecorations d;
    const Rect&     vp     = axes.viewport();
    const float     tl     = axes.axis_style().tick_length;
    const float     tick_h = TICK_FONT_PX * LABEL_LINE_FACTOR;
    const float     lbl_h  = LABEL_FONT_PX * LABEL_LINE_FACTOR;
    const float     ttl_h  = TITLE_FONT_PX * LABEL_LINE_FACTOR;
    const bool      show_x = axes.x_tick_labels_visible();
    const bool      show_y = axes.y_tick_labels_visible();

    d.left = left_label_extent(axes);

    // X tick labels are centred on their ticks: the outer ones may stick out
    // past the plot edges.
    const TickResult xt = axes.compute_x_ticks();
    if (show_x && vp.w > 0.0f)
    {
        const ScaleType  sc     = axes.xscale();
        const AxisLimits lim    = axes.x_limits();
        const double     a0     = to_axis_space(sc, lim.min);
        const double     a1     = to_axis_space(sc, lim.max);
        const double     range  = a1 - a0 != 0.0 ? a1 - a0 : 1.0;
        float            over_l = 0.0f;
        float            over_r = 0.0f;
        for (size_t i = 0; i < xt.positions.size() && i < xt.labels.size(); ++i)
        {
            const float px =
                vp.x + static_cast<float>((to_axis_space(sc, xt.positions[i]) - a0) / range) * vp.w;
            if (!(px >= vp.x - 0.5f && px <= vp.x + vp.w + 0.5f))
                continue;
            const float half = inter_text_width(xt.labels[i], TICK_FONT_PX) * 0.5f;
            over_l           = std::max(over_l, vp.x - (px - half));
            over_r           = std::max(over_r, px + half - (vp.x + vp.w));
        }
        if (over_l > 0.0f)
            d.left = std::max(d.left, over_l + LEFT_LABEL_OUTER_PAD);
        if (over_r > 0.0f)
            d.right = over_r + LEFT_LABEL_OUTER_PAD;
    }

    // Below: tick marks, tick labels, then the X label / offset row.
    float bottom = tl;
    if (show_x)
        bottom += TICK_PADDING + tick_h;
    if (!axes.xlabel().empty() || (show_x && !xt.offset_label.empty()))
        bottom = tl + TICK_PADDING + tick_h + XLABEL_ROW_GAP + lbl_h;
    d.bottom = bottom + LEFT_LABEL_OUTER_PAD;

    // Above: the title, sharing its row with the Y offset label.
    float top = 0.0f;
    if (show_y && !axes.compute_y_ticks().offset_label.empty())
        top = XLABEL_ROW_GAP + tick_h;
    if (!axes.title().empty())
        top = std::max(top, TICK_PADDING + ttl_h);
    d.top = top > 0.0f ? top + LEFT_LABEL_OUTER_PAD : 0.0f;
    return d;
}

float suptitle_band_height(const Figure& fig)
{
    if (fig.suptitle().empty())
        return 0.0f;
    return SUPTITLE_PAD + SUPTITLE_FONT_PX * SUPTITLE_SCALE * LABEL_LINE_FACTOR;
}

GridSpacing compute_figure_spacing(const Figure&                       fig,
                                   const std::vector<AxesDecorations>& decor,
                                   float                               width,
                                   float                               height,
                                   const Margins&                      outer)
{
    const int          rows  = std::max(1, fig.grid_rows());
    const int          cols  = std::max(1, fig.grid_cols());
    const FigureStyle& style = fig.style();

    GridSpacing sp;
    sp.outer = outer;
    sp.col_gaps.assign(static_cast<size_t>(cols - 1), std::max(0.0f, style.subplot_hgap));
    sp.row_gaps.assign(static_cast<size_t>(rows - 1), std::max(0.0f, style.subplot_vgap));
    if (!style.constrained_layout)
        return sp;

    // What each column / row needs on each side: the most any of its axes needs.
    std::vector<float> L(static_cast<size_t>(cols), 0.0f);
    std::vector<float> R(static_cast<size_t>(cols), 0.0f);
    std::vector<float> T(static_cast<size_t>(rows), 0.0f);
    std::vector<float> B(static_cast<size_t>(rows), 0.0f);
    for (size_t i = 0; i < decor.size() && i < static_cast<size_t>(rows * cols); ++i)
    {
        const size_t r = i / static_cast<size_t>(cols);
        const size_t c = i % static_cast<size_t>(cols);
        L[c]           = std::max(L[c], decor[i].left);
        R[c]           = std::max(R[c], decor[i].right);
        T[r]           = std::max(T[r], decor[i].top);
        B[r]           = std::max(B[r], decor[i].bottom);
    }

    GridSpacing grown = sp;
    for (int c = 0; c + 1 < cols; ++c)
        grown.col_gaps[c] = std::max(sp.col_gaps[c], R[c] + L[c + 1]);
    for (int r = 0; r + 1 < rows; ++r)
        grown.row_gaps[r] = std::max(sp.row_gaps[r], B[r] + T[r + 1]);
    grown.outer.left   = std::max(outer.left, L.front());
    grown.outer.right  = std::max(outer.right, R.back());
    grown.outer.top    = std::max(outer.top, T.front());
    grown.outer.bottom = std::max(outer.bottom, B.back());

    // Never let the spacing eat the plots: past MAX_SPACING_FRACTION of the
    // size, every grown value gives back the same share of its growth (and on
    // figures too small even for the minimums, everything shrinks).
    auto limit = [](float               size,
                    float&              lo_base,
                    float&              hi_base,
                    std::vector<float>& gaps_base,
                    float&              lo,
                    float&              hi,
                    std::vector<float>& gaps)
    {
        float base = lo_base + hi_base;
        float sum  = lo + hi;
        for (size_t i = 0; i < gaps.size(); ++i)
        {
            base += gaps_base[i];
            sum += gaps[i];
        }
        const float cap = size * MAX_SPACING_FRACTION;
        if (!(sum > cap))
            return;
        if (!(base < cap))
        {
            // Even the minimums do not fit (tiny figure): shrink everything.
            const float k = sum > 0.0f ? std::max(0.0f, cap) / sum : 0.0f;
            lo *= k;
            hi *= k;
            for (auto& g : gaps)
                g *= k;
            return;
        }
        const float t = (cap - base) / (sum - base);
        lo            = lo_base + (lo - lo_base) * t;
        hi            = hi_base + (hi - hi_base) * t;
        for (size_t i = 0; i < gaps.size(); ++i)
            gaps[i] = gaps_base[i] + (gaps[i] - gaps_base[i]) * t;
    };
    limit(width,
          sp.outer.left,
          sp.outer.right,
          sp.col_gaps,
          grown.outer.left,
          grown.outer.right,
          grown.col_gaps);
    limit(height,
          sp.outer.top,
          sp.outer.bottom,
          sp.row_gaps,
          grown.outer.top,
          grown.outer.bottom,
          grown.row_gaps);
    return grown;
}

namespace
{

AxesBase* slot_axes(Figure& fig, size_t i)
{
    if (i < fig.axes().size() && fig.axes()[i])
        return fig.axes()[i].get();
    if (i < fig.all_axes().size() && fig.all_axes()[i])
        return fig.all_axes()[i].get();
    return nullptr;
}

// Hide the tick labels of inner shared axes: x on axes with an x-share peer
// below them in the same column, y on axes with a y-share peer to their left
// in the same row (matplotlib's label_outer for sharex / sharey).
void update_shared_tick_labels(Figure& fig)
{
    const size_t cols = static_cast<size_t>(std::max(1, fig.grid_cols()));
    const auto&  axes = fig.axes();
    auto         slot = [&](const Axes* a) -> size_t
    {
        for (size_t i = 0; i < axes.size(); ++i)
            if (axes[i].get() == a)
                return i;
        return SIZE_MAX;
    };
    for (size_t i = 0; i < axes.size(); ++i)
    {
        Axes* a = axes[i].get();
        if (!a)
            continue;
        bool show_x = true;
        bool show_y = true;
        for (const Axes* p : a->shared_x_axes())
        {
            const size_t j = slot(p);
            if (j != SIZE_MAX && j % cols == i % cols && j / cols > i / cols)
                show_x = false;
        }
        for (const Axes* p : a->shared_y_axes())
        {
            const size_t j = slot(p);
            if (j != SIZE_MAX && j / cols == i / cols && j % cols < i % cols)
                show_y = false;
        }
        a->set_tick_labels_visible(show_x, show_y);
    }
}

}   // namespace

void apply_subplot_layout(Figure& fig, const std::vector<Rect>& rects, const Margins& margins)
{
    apply_subplot_layout(
        fig,
        rects,
        std::vector<float>(static_cast<size_t>(std::max(1, fig.grid_cols())), margins.left));
}

void apply_subplot_layout(Figure&                   fig,
                          const std::vector<Rect>&  rects,
                          const std::vector<float>& left_room)
{
    const int    cols  = std::max(1, fig.grid_cols());
    const size_t slots = rects.size();
    auto         room  = [&](size_t col) { return col < left_room.size() ? left_room[col] : 0.0f; };

    // Pass 1: outside-legend strips and the left growth each column needs.
    std::vector<float> legend_w(slots, 0.0f);
    std::vector<float> grow(static_cast<size_t>(cols), 0.0f);
    for (size_t i = 0; i < slots; ++i)
    {
        AxesBase* a = slot_axes(fig, i);
        if (!a)
            continue;
        legend_w[i] = legend_layout::reserved_width(*a, fig.legend(), rects[i]);
        auto* a2    = dynamic_cast<Axes*>(a);
        if (!a2)
            continue;
        a2->set_viewport(rects[i], legend_w[i]);   // provisional: tick count follows the height
        const size_t col   = i % static_cast<size_t>(cols);
        const float  extra = left_label_extent(*a2) - room(col);
        if (extra > 0.0f)
        {
            const float cap = rects[i].w * LEFT_GROWTH_MAX_FRACTION;
            grow[col]       = std::max(grow[col], std::min(extra, cap));
        }
    }

    // Pass 2: final viewports (3D axes keep the plain cell).
    for (size_t i = 0; i < slots; ++i)
    {
        if (i < fig.axes().size() && fig.axes()[i])
        {
            Rect        r = rects[i];
            const float g =
                std::min(grow[i % static_cast<size_t>(cols)], r.w * LEFT_GROWTH_MAX_FRACTION);
            r.x += g;
            r.w = std::max(0.0f, r.w - g);
            fig.axes()[i]->set_viewport(r, legend_w[i]);
        }
        if (i < fig.all_axes().size() && fig.all_axes()[i])
            fig.all_axes()[i]->set_viewport(rects[i], legend_w[i]);
    }
}

float layout_figure(Figure&        fig,
                    const Rect&    content,
                    const Margins& outer,
                    float          min_subplot_height)
{
    const int    rows  = std::max(1, fig.grid_rows());
    const int    cols  = std::max(1, fig.grid_cols());
    const size_t slots = static_cast<size_t>(rows) * static_cast<size_t>(cols);
    const float  band  = suptitle_band_height(fig);

    update_shared_tick_labels(fig);

    // Manual spacing first: the final one when constrained layout is off, the
    // provisional viewports the labels are measured at otherwise.
    std::vector<AxesDecorations> decor(slots);
    GridSpacing sp = compute_figure_spacing(fig, decor, content.w, content.h - band, outer);
    if (fig.style().constrained_layout)
    {
        GridSpacing prov = sp;
        prov.outer.top += band;
        const auto rects0 = compute_grid_layout(content.w,
                                                content.h,
                                                rows,
                                                cols,
                                                prov,
                                                content.x,
                                                content.y,
                                                min_subplot_height);
        for (size_t i = 0; i < slots && i < fig.axes().size(); ++i)
        {
            Axes* a = fig.axes()[i].get();
            if (!a)
                continue;
            a->set_viewport(rects0[i], legend_layout::reserved_width(*a, fig.legend(), rects0[i]));
            decor[i] = measure_decorations(*a);
        }
        sp = compute_figure_spacing(fig, decor, content.w, content.h - band, outer);
    }
    sp.outer.top += band;

    float      content_h = 0.0f;
    const auto rects     = compute_grid_layout(content.w,
                                           content.h,
                                           rows,
                                           cols,
                                           sp,
                                           content.x,
                                           content.y,
                                           min_subplot_height,
                                           &content_h);

    // Room left of each column: the outer margin, else the gap minus what the
    // column on its left needs on its right side.
    std::vector<float> left_room(static_cast<size_t>(cols), sp.outer.left);
    for (int c = 1; c < cols; ++c)
    {
        float right_need = 0.0f;
        for (int r = 0; r < rows; ++r)
            right_need = std::max(right_need, decor[static_cast<size_t>(r * cols + c - 1)].right);
        left_room[c] = std::max(0.0f, sp.col_gaps[c - 1] - right_need);
    }
    apply_subplot_layout(fig, rects, left_room);

    if (band > 0.0f && !rects.empty())
    {
        const float x0 = rects.front().x;
        const float x1 =
            rects[static_cast<size_t>(cols - 1)].x + rects[static_cast<size_t>(cols - 1)].w;
        fig.set_suptitle_rect(Rect{x0,
                                   content.y + SUPTITLE_PAD,
                                   std::max(0.0f, x1 - x0),
                                   SUPTITLE_FONT_PX * SUPTITLE_SCALE * LABEL_LINE_FACTOR});
    }
    else
    {
        fig.set_suptitle_rect(Rect{0.0f, 0.0f, 0.0f, 0.0f});
    }
    return content_h;
}

}   // namespace spectra
