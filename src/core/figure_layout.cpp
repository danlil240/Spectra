#include "core/figure_layout.hpp"

#include <algorithm>
#include <spectra/figure.hpp>

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
// Rotated label thickness: the renderer's line height for the Label tier.
constexpr float LABEL_LINE_FACTOR = 1.25f;

}   // namespace

float left_label_extent(const Axes& axes)
{
    const TickResult ticks  = axes.compute_y_ticks();
    float            tick_w = 0.0f;
    for (const auto& l : ticks.labels)
        tick_w = std::max(tick_w, inter_text_width(l, TICK_FONT_PX));
    float need = axes.axis_style().tick_length + TICK_PADDING + tick_w;
    if (!axes.ylabel().empty())
        need += YLABEL_GAP + LABEL_FONT_PX * LABEL_LINE_FACTOR;
    return need + LEFT_LABEL_OUTER_PAD;
}

void apply_subplot_layout(Figure& fig, const std::vector<Rect>& rects, const Margins& margins)
{
    const int    cols  = std::max(1, fig.grid_cols());
    const size_t slots = rects.size();

    auto slot_axes = [&](size_t i) -> AxesBase*
    {
        if (i < fig.axes().size() && fig.axes()[i])
            return fig.axes()[i].get();
        if (i < fig.all_axes().size() && fig.all_axes()[i])
            return fig.all_axes()[i].get();
        return nullptr;
    };

    // Pass 1: outside-legend strips and the left growth each column needs.
    std::vector<float> legend_w(slots, 0.0f);
    std::vector<float> grow(static_cast<size_t>(cols), 0.0f);
    for (size_t i = 0; i < slots; ++i)
    {
        AxesBase* a = slot_axes(i);
        if (!a)
            continue;
        legend_w[i] = legend_layout::outside_reserved_width(*a, fig.legend(), rects[i].w);
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

}   // namespace spectra
