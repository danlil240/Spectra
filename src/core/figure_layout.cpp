#include "core/figure_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <spectra/figure.hpp>

#include "core/axis_scale.hpp"
#include "core/axis_text_style.hpp"
#include "io/text_metrics.hpp"
#include "ui/overlay/legend_layout.hpp"

namespace spectra
{

namespace
{

// Renderer / SVG constants for the axis decorations (render_geometry.cpp,
// vector_scene.cpp): tick label padding, gap to the y label, gap between the
// x tick labels and the x label row, all in logical pixels (scaled with the
// axes' text scale).
constexpr float TICK_PADDING   = 5.0f;
constexpr float YLABEL_GAP     = 6.0f;
constexpr float XLABEL_ROW_GAP = 4.0f;
constexpr float OFFSET_ROW_GAP = 4.0f;
constexpr float TITLE_PADDING  = 5.0f;
constexpr float VERTICAL_OUTER = 6.0f;
// Space above the suptitle.
constexpr float SUPTITLE_PAD = 8.0f;
// Clearance kept past an outermost x tick label that overhangs the plot edge:
// it only has to stay clear of the figure edge / neighbour, so a label that
// already fits the style margin never grows it.
constexpr float EDGE_TICK_PAD = 2.0f;

// Metrics of `axes` at the theme font sizes and scale 1 (what the default
// margins are sized for): the baseline bottom/top growth is measured against.
AxisTextMetrics default_metrics(const Axes& axes)
{
    AxisStyle base;
    base.tick_length = axes.axis_style().tick_length;
    return axis_text_metrics(base, 1.0f);
}

// Suptitle line height at text scale `s`.
float suptitle_line_height(float s)
{
    return SUPTITLE_FONT_PX * SUPTITLE_SCALE * PLOT_TEXT_LINE_FACTOR * s;
}

}   // namespace

std::vector<Axes*> axes_with_twins(const Figure& fig)
{
    std::vector<Axes*> out;
    out.reserve(fig.axes().size());
    for (const auto& a : fig.axes())
    {
        if (!a)
            continue;
        out.push_back(a.get());
        if (a->twin())
            out.push_back(a->twin());
    }
    return out;
}

float left_label_extent(const Axes& axes)
{
    const AxisTextMetrics m      = axis_text_metrics(axes);
    float                 tick_w = 0.0f;
    if (axes.y_tick_labels_visible())
    {
        const TickResult ticks = axes.compute_y_ticks();
        for (const auto& l : ticks.labels)
            tick_w = std::max(tick_w, inter_text_width(l, m.tick_px));
    }
    float need = m.tick_length + m.px(TICK_PADDING) + tick_w;
    if (!axes.ylabel().empty())
        need += m.px(YLABEL_GAP) + m.label_px * PLOT_TEXT_LINE_FACTOR;
    return need + m.px(LEFT_LABEL_OUTER_PAD);
}

float bottom_label_extent(const Axes& axes, const AxisTextMetrics& m)
{
    // Tick marks, then the tick labels (hidden on inner shared axes).
    const bool  show_x = axes.x_tick_labels_visible();
    const float tick_h = m.tick_px * PLOT_TEXT_LINE_FACTOR;
    float       need   = m.tick_length;
    if (show_x)
        need += m.px(TICK_PADDING) + tick_h;
    // Row under the tick label row: the X label and/or the X tick offset /
    // multiplier annotation (drawn at the tick size on the same row,
    // render_plot_text()).
    float row_h = 0.0f;
    if (!axes.xlabel().empty())
        row_h = m.label_px * PLOT_TEXT_LINE_FACTOR;
    if (show_x && !axes.compute_x_ticks().offset_label.empty())
        row_h = std::max(row_h, tick_h);
    if (row_h > 0.0f)
        need = m.tick_length + m.px(TICK_PADDING) + tick_h + m.px(XLABEL_ROW_GAP) + row_h;
    return need + m.px(VERTICAL_OUTER);
}

float top_label_extent(const Axes& axes, const AxisTextMetrics& m)
{
    // The title and the Y tick offset annotation share the row above the plot.
    float need = 0.0f;
    if (!axes.title().empty())
        need = m.title_px * PLOT_TEXT_LINE_FACTOR + m.px(TITLE_PADDING);
    if (axes.y_tick_labels_visible() && !axes.compute_y_ticks().offset_label.empty())
        need = std::max(need, m.tick_px * PLOT_TEXT_LINE_FACTOR + m.px(OFFSET_ROW_GAP));
    return need > 0.0f ? need + m.px(VERTICAL_OUTER) : 0.0f;
}

AxesDecorations measure_decorations(const Axes& axes)
{
    AxesDecorations       d;
    const AxisTextMetrics m  = axis_text_metrics(axes);
    const Rect&           vp = axes.viewport();

    d.left = left_label_extent(axes);

    // X tick labels are centred on their ticks: the outer ones may stick out
    // past the plot edges.
    if (axes.x_tick_labels_visible() && vp.w > 0.0f)
    {
        const TickResult xt     = axes.compute_x_ticks();
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
            const float half = inter_text_width(xt.labels[i], m.tick_px) * 0.5f;
            over_l           = std::max(over_l, vp.x - (px - half));
            over_r           = std::max(over_r, px + half - (vp.x + vp.w));
        }
        if (over_l > 0.0f)
            d.left = std::max(d.left, over_l + m.px(EDGE_TICK_PAD));
        if (over_r > 0.0f)
            d.right = over_r + m.px(EDGE_TICK_PAD);
    }

    d.bottom = bottom_label_extent(axes, m);
    d.top    = top_label_extent(axes, m);
    return d;
}

float suptitle_band_height(const Figure& fig, float text_scale)
{
    if (fig.suptitle().empty())
        return 0.0f;
    if (!(text_scale > 0.0f))
        text_scale = 1.0f;
    return SUPTITLE_PAD * text_scale + suptitle_line_height(text_scale);
}

float figure_text_scale(const Figure& fig)
{
    for (const auto& a : fig.all_axes())
        if (a)
            return a->text_scale();
    for (const auto& a : fig.axes())
        if (a)
            return a->text_scale();
    return 1.0f;
}

GridSpacing compute_figure_spacing(const Figure&                       fig,
                                   const std::vector<AxesDecorations>& decor,
                                   float                               width,
                                   float                               height,
                                   const Margins&                      outer,
                                   float                               gap_scale)
{
    const int          rows  = std::max(1, fig.grid_rows());
    const int          cols  = std::max(1, fig.grid_cols());
    const FigureStyle& style = fig.style();
    if (!(gap_scale > 0.0f))
        gap_scale = 1.0f;

    GridSpacing sp;
    sp.outer = outer;
    sp.col_gaps.assign(static_cast<size_t>(cols - 1),
                       std::max(0.0f, style.subplot_hgap * gap_scale));
    sp.row_gaps.assign(static_cast<size_t>(rows - 1),
                       std::max(0.0f, style.subplot_vgap * gap_scale));
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

void set_text_scale(Figure& fig, float text_scale)
{
    for (auto& a : fig.axes())
        if (a)
            a->set_text_scale(text_scale);
    for (auto& a : fig.all_axes())
        if (a)
            a->set_text_scale(text_scale);
}

}   // namespace

void apply_subplot_layout(Figure&                  fig,
                          const std::vector<Rect>& rects,
                          const Margins&           margins,
                          float                    text_scale)
{
    set_text_scale(fig, text_scale);
    const size_t cols = static_cast<size_t>(std::max(1, fig.grid_cols()));
    const size_t rows = (rects.size() + cols - 1) / cols;
    SubplotRoom  room;
    room.left.assign(cols, margins.left);
    room.top.assign(rows, margins.top);
    room.bottom.assign(rows, margins.bottom);
    apply_subplot_layout(fig, rects, room);
}

void apply_subplot_layout(Figure& fig, const std::vector<Rect>& rects, const SubplotRoom& room)
{
    const int    cols  = std::max(1, fig.grid_cols());
    const size_t slots = rects.size();
    const size_t rows  = (slots + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols);
    auto at = [](const std::vector<float>& v, size_t i) { return i < v.size() ? v[i] : 0.0f; };

    // Pass 1: outside-legend strips and the growth each column (left) and row
    // (top, bottom) needs.  Top / bottom only grow for text larger than what
    // the default margins are sized for (custom font sizes, UI scale), so
    // figures with the theme font sizes keep their layout.
    std::vector<float> legend_w(slots, 0.0f);
    std::vector<float> grow(static_cast<size_t>(cols), 0.0f);
    std::vector<float> grow_top(rows, 0.0f);
    std::vector<float> grow_bottom(rows, 0.0f);
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
        const float  extra = left_label_extent(*a2) - at(room.left, col);
        if (extra > 0.0f)
        {
            const float cap = rects[i].w * LEFT_GROWTH_MAX_FRACTION;
            grow[col]       = std::max(grow[col], std::min(extra, cap));
        }

        const AxisTextMetrics m            = axis_text_metrics(*a2);
        const AxisTextMetrics base         = default_metrics(*a2);
        const float           vcap         = rects[i].h * VERTICAL_GROWTH_MAX_FRACTION;
        const size_t          row          = i / static_cast<size_t>(cols);
        const float           extra_bottom = bottom_label_extent(*a2, m)
                                   - std::max(at(room.bottom, row), bottom_label_extent(*a2, base));
        if (extra_bottom > 0.0f)
            grow_bottom[row] = std::max(grow_bottom[row], std::min(extra_bottom, vcap));
        const float extra_top =
            top_label_extent(*a2, m) - std::max(at(room.top, row), top_label_extent(*a2, base));
        if (extra_top > 0.0f)
            grow_top[row] = std::max(grow_top[row], std::min(extra_top, vcap));
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
            r.w               = std::max(0.0f, r.w - g);
            const size_t row  = i / static_cast<size_t>(cols);
            const float  vcap = r.h * VERTICAL_GROWTH_MAX_FRACTION;
            const float  gt   = std::min(grow_top[row], vcap);
            const float  gb   = std::min(grow_bottom[row], vcap);
            r.y += gt;
            r.h = std::max(0.0f, r.h - gt - gb);
            fig.axes()[i]->set_viewport(r, legend_w[i]);
        }
        if (i < fig.all_axes().size() && fig.all_axes()[i])
            fig.all_axes()[i]->set_viewport(rects[i], legend_w[i]);
    }
}

float layout_figure(Figure&        fig,
                    const Rect&    content,
                    const Margins& outer,
                    float          min_subplot_height,
                    float          text_scale)
{
    if (!(text_scale > 0.0f))
        text_scale = 1.0f;
    const int    rows  = std::max(1, fig.grid_rows());
    const int    cols  = std::max(1, fig.grid_cols());
    const size_t slots = static_cast<size_t>(rows) * static_cast<size_t>(cols);
    const float  band  = suptitle_band_height(fig, text_scale);

    // The text scale first: every measurement below depends on it.
    set_text_scale(fig, text_scale);
    update_shared_tick_labels(fig);

    // Manual spacing first: the final one when constrained layout is off, the
    // provisional viewports the labels are measured at otherwise.
    std::vector<AxesDecorations> decor(slots);
    GridSpacing                  sp =
        compute_figure_spacing(fig, decor, content.w, content.h - band, outer, text_scale);
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
        sp = compute_figure_spacing(fig, decor, content.w, content.h - band, outer, text_scale);
    }
    const float outer_top = sp.outer.top;
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

    // Room around each column / row: the outer margin, else the gap minus
    // what the neighbouring column / row needs on its facing side.
    auto decor_at = [&](int r, int c) -> const AxesDecorations&
    { return decor[static_cast<size_t>(r * cols + c)]; };
    SubplotRoom room;
    room.left.assign(static_cast<size_t>(cols), sp.outer.left);
    for (int c = 1; c < cols; ++c)
    {
        float right_need = 0.0f;
        for (int r = 0; r < rows; ++r)
            right_need = std::max(right_need, decor_at(r, c - 1).right);
        room.left[c] = std::max(0.0f, sp.col_gaps[c - 1] - right_need);
    }
    room.top.assign(static_cast<size_t>(rows), outer_top);
    room.bottom.assign(static_cast<size_t>(rows), sp.outer.bottom);
    for (int r = 0; r + 1 < rows; ++r)
    {
        float top_need    = 0.0f;   // of the row below
        float bottom_need = 0.0f;   // of this row
        for (int c = 0; c < cols; ++c)
        {
            top_need    = std::max(top_need, decor_at(r + 1, c).top);
            bottom_need = std::max(bottom_need, decor_at(r, c).bottom);
        }
        room.bottom[r]  = std::max(0.0f, sp.row_gaps[r] - top_need);
        room.top[r + 1] = std::max(0.0f, sp.row_gaps[r] - bottom_need);
    }
    apply_subplot_layout(fig, rects, room);

    if (band > 0.0f && !rects.empty())
    {
        const float x0 = rects.front().x;
        const float x1 =
            rects[static_cast<size_t>(cols - 1)].x + rects[static_cast<size_t>(cols - 1)].w;
        fig.set_suptitle_rect(Rect{x0,
                                   content.y + SUPTITLE_PAD * text_scale,
                                   std::max(0.0f, x1 - x0),
                                   suptitle_line_height(text_scale)});
    }
    else
    {
        fig.set_suptitle_rect(Rect{0.0f, 0.0f, 0.0f, 0.0f});
    }
    return content_h;
}

}   // namespace spectra
