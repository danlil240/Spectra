#include "core/figure_layout.hpp"

#include <algorithm>
#include <spectra/figure.hpp>

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

// Metrics of `axes` at the theme font sizes and scale 1 (what the default
// margins are sized for): the baseline bottom/top growth is measured against.
AxisTextMetrics default_metrics(const Axes& axes)
{
    AxisStyle base;
    base.tick_length = axes.axis_style().tick_length;
    return axis_text_metrics(base, 1.0f);
}

}   // namespace

float left_label_extent(const Axes& axes)
{
    const AxisTextMetrics m      = axis_text_metrics(axes);
    const TickResult      ticks  = axes.compute_y_ticks();
    float                 tick_w = 0.0f;
    for (const auto& l : ticks.labels)
        tick_w = std::max(tick_w, inter_text_width(l, m.tick_px));
    float need = m.tick_length + m.px(TICK_PADDING) + tick_w;
    if (!axes.ylabel().empty())
        need += m.px(YLABEL_GAP) + m.label_px * PLOT_TEXT_LINE_FACTOR;
    return need + m.px(LEFT_LABEL_OUTER_PAD);
}

float bottom_label_extent(const Axes& axes, const AxisTextMetrics& m)
{
    float need = m.tick_length + m.px(TICK_PADDING) + m.tick_px * PLOT_TEXT_LINE_FACTOR;
    if (!axes.xlabel().empty())
        need += m.px(XLABEL_ROW_GAP) + m.label_px * PLOT_TEXT_LINE_FACTOR;
    return need + m.px(VERTICAL_OUTER);
}

float top_label_extent(const Axes& axes, const AxisTextMetrics& m)
{
    float need = 0.0f;
    if (!axes.title().empty())
        need = m.title_px * PLOT_TEXT_LINE_FACTOR + m.px(TITLE_PADDING);
    else if (!axes.compute_y_ticks().offset_label.empty())
        need = m.tick_px * PLOT_TEXT_LINE_FACTOR + m.px(OFFSET_ROW_GAP);
    return need > 0.0f ? need + m.px(VERTICAL_OUTER) : 0.0f;
}

void apply_subplot_layout(Figure&                  fig,
                          const std::vector<Rect>& rects,
                          const Margins&           margins,
                          float                    text_scale)
{
    const int    cols  = std::max(1, fig.grid_cols());
    const size_t slots = rects.size();
    const size_t rows  = (slots + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols);

    auto slot_axes = [&](size_t i) -> AxesBase*
    {
        if (i < fig.axes().size() && fig.axes()[i])
            return fig.axes()[i].get();
        if (i < fig.all_axes().size() && fig.all_axes()[i])
            return fig.all_axes()[i].get();
        return nullptr;
    };

    for (auto& a : fig.axes())
        if (a)
            a->set_text_scale(text_scale);
    for (auto& a : fig.all_axes())
        if (a)
            a->set_text_scale(text_scale);

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
        AxesBase* a = slot_axes(i);
        if (!a)
            continue;
        legend_w[i] = legend_layout::reserved_width(*a, fig.legend(), rects[i]);
        auto* a2    = dynamic_cast<Axes*>(a);
        if (!a2)
            continue;
        a2->set_viewport(rects[i], legend_w[i]);   // provisional: tick count follows the height
        const float extra = left_label_extent(*a2) - margins.left;
        if (extra > 0.0f)
        {
            const float  cap = rects[i].w * LEFT_GROWTH_MAX_FRACTION;
            const size_t col = i % static_cast<size_t>(cols);
            grow[col]        = std::max(grow[col], std::min(extra, cap));
        }

        const AxisTextMetrics m    = axis_text_metrics(*a2);
        const AxisTextMetrics base = default_metrics(*a2);
        const float           vcap = rects[i].h * VERTICAL_GROWTH_MAX_FRACTION;
        const size_t          row  = i / static_cast<size_t>(cols);
        const float           extra_bottom =
            bottom_label_extent(*a2, m) - std::max(margins.bottom, bottom_label_extent(*a2, base));
        if (extra_bottom > 0.0f)
            grow_bottom[row] = std::max(grow_bottom[row], std::min(extra_bottom, vcap));
        const float extra_top =
            top_label_extent(*a2, m) - std::max(margins.top, top_label_extent(*a2, base));
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

}   // namespace spectra
