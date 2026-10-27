#pragma once

// axes_selection.hpp — ImGui-free helpers that pick which axes (2D or 3D) the
// inspector's Axes section shows.  2D axes live in Figure::axes(), 3D axes in
// Figure::all_axes(); both are indexed by subplot slot, so a slot index names
// one axes whatever its kind.

#include <algorithm>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "ui/input/selection_context.hpp"

namespace spectra::ui
{

struct AxesSlot
{
    AxesBase* base   = nullptr;   // Always set
    Axes*     axes2d = nullptr;   // Set for a 2D axes
    Axes3D*   axes3d = nullptr;   // Set for a 3D axes
    int       index  = -1;        // Subplot slot
};

// All axes of `fig` in slot order (2D before 3D when a slot somehow has both).
inline std::vector<AxesSlot> figure_axes_slots(Figure& fig)
{
    std::vector<AxesSlot> out;
    const size_t          n = std::max(fig.axes().size(), fig.all_axes().size());
    for (size_t i = 0; i < n; ++i)
    {
        if (i < fig.axes().size() && fig.axes()[i])
        {
            Axes* ax = fig.axes_mut()[i].get();
            out.push_back({ax, ax, nullptr, static_cast<int>(i)});
        }
        if (i < fig.all_axes().size() && fig.all_axes()[i])
        {
            AxesBase* ab = fig.all_axes_mut()[i].get();
            // all_axes() may also mirror a 2D axes already listed above.
            bool listed = false;
            for (const auto& s : out)
                listed = listed || s.base == ab;
            if (listed)
                continue;
            out.push_back(
                {ab, dynamic_cast<Axes*>(ab), dynamic_cast<Axes3D*>(ab), static_cast<int>(i)});
        }
    }
    return out;
}

// Select one axes slot in `ctx` (sets axes for 2D, axes_base always).
inline void select_axes_slot(SelectionContext& ctx, Figure& fig, const AxesSlot& slot)
{
    ctx.select_axes(&fig, slot.axes2d, slot.index);
    ctx.axes_base = slot.base;
}

// Keep an Axes-section selection valid for `fig`: select the first axes when
// nothing (or something else) is selected, keep the slot index when the figure
// changed, and clear when the figure has no axes.  2D and 3D axes both count.
inline void sync_axes_section_selection(SelectionContext& ctx, Figure& fig)
{
    const auto slots = figure_axes_slots(fig);
    if (slots.empty())
    {
        ctx.clear();
        return;
    }

    if (ctx.type == SelectionType::Axes && ctx.figure == &fig)
    {
        // Still pointing at a live axes of this figure?
        for (const auto& s : slots)
        {
            if (s.base == ctx.axes_base || (ctx.axes && s.axes2d == ctx.axes))
            {
                select_axes_slot(ctx, fig, s);
                return;
            }
        }
    }

    // Different figure (or stale axes): keep the slot index when possible.
    if (ctx.type == SelectionType::Axes)
    {
        for (const auto& s : slots)
        {
            if (s.index == ctx.axes_index)
            {
                select_axes_slot(ctx, fig, s);
                return;
            }
        }
    }
    select_axes_slot(ctx, fig, slots.front());
}

}   // namespace spectra::ui
