#ifdef SPECTRA_USE_IMGUI

    #include "marker_actions.hpp"

    #include <algorithm>
    #include <cmath>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>

    #include "core/axis_scale.hpp"
    #include "data_interaction.hpp"
    #include "ui/commands/undo_manager.hpp"

namespace spectra
{

namespace
{

bool series_in(const AxesBase& axes, const Series* s)
{
    for (const auto& p : axes.series())
        if (p.get() == s)
            return true;
    return false;
}

const AxesBase* owning_axes(const DataMarker& m, const Figure& figure)
{
    const AxesBase* want =
        m.is_3d ? static_cast<const AxesBase*>(m.axes3d) : static_cast<const AxesBase*>(m.axes);
    if (!want)
        return nullptr;
    for (const auto& a : figure.axes())
        if (a.get() == want)
            return a.get();
    for (const auto& a : figure.all_axes())
        if (a.get() == want)
            return a.get();
    return nullptr;
}

bool same_marker(const DataMarker& a, const DataMarker& b)
{
    return a.series == b.series && a.point_index == b.point_index && a.axes == b.axes
           && a.axes3d == b.axes3d && a.is_3d == b.is_3d && a.data_x == b.data_x
           && a.data_y == b.data_y && a.data_z == b.data_z;
}

int find_marker(const DataInteraction& di, const DataMarker& m)
{
    const auto& all = di.markers();
    for (size_t i = 0; i < all.size(); ++i)
        if (same_marker(all[i], m))
            return static_cast<int>(i);
    return -1;
}

// Re-insert @p saved (index, marker) pairs, ascending by index, that still
// belong to the current figure and are not already present.
void restore_markers(DataInteraction&                                  di,
                     const std::vector<std::pair<size_t, DataMarker>>& saved,
                     const CurrentFigureFn&                            current_figure)
{
    Figure* fig = current_figure ? current_figure() : nullptr;
    if (!fig)
        return;
    for (const auto& [idx, m] : saved)
        if (marker_in_figure(m, *fig) && find_marker(di, m) < 0)
            di.insert_marker(idx, m);
}

void remove_saved(DataInteraction& di, const std::vector<std::pair<size_t, DataMarker>>& saved)
{
    for (const auto& entry : saved)
    {
        const int i = find_marker(di, entry.second);
        if (i >= 0)
            di.remove_marker(static_cast<size_t>(i));
    }
}

// Live axes @p axes_index of figure @p id, or nullptr once either is gone.
Axes* resolve_axes(const FigureLookup& lookup, FigureId id, size_t axes_index)
{
    Figure* fig = lookup.resolve ? lookup.resolve(id) : nullptr;
    if (!fig || axes_index >= fig->axes().size())
        return nullptr;
    return fig->axes_mut()[axes_index].get();
}

}   // namespace

bool marker_in_figure(const DataMarker& m, const Figure& figure)
{
    const AxesBase* axes = owning_axes(m, figure);
    return axes != nullptr && series_in(*axes, m.series);
}

std::vector<size_t> figure_marker_indices(const std::vector<DataMarker>& markers,
                                          const Figure&                  figure)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < markers.size(); ++i)
        if (marker_in_figure(markers[i], figure))
            out.push_back(i);
    return out;
}

std::string marker_series_name(const DataMarker& m)
{
    return m.series_label.empty() ? std::string("Series") : m.series_label;
}

bool remove_marker_undoable(DataInteraction& di,
                            size_t           idx,
                            UndoManager*     undo,
                            CurrentFigureFn  current_figure)
{
    if (idx >= di.markers().size())
        return false;
    std::vector<std::pair<size_t, DataMarker>> saved{{idx, di.markers()[idx]}};
    di.remove_marker(idx);
    if (undo)
    {
        DataInteraction* dip = &di;
        undo->push(UndoAction{"Remove data tip",
                              [dip, saved, current_figure]()
                              { restore_markers(*dip, saved, current_figure); },
                              [dip, saved]() { remove_saved(*dip, saved); }});
    }
    return true;
}

size_t clear_figure_markers_undoable(DataInteraction& di,
                                     const Figure&    figure,
                                     UndoManager*     undo,
                                     CurrentFigureFn  current_figure)
{
    const std::vector<size_t> indices = figure_marker_indices(di.markers(), figure);
    if (indices.empty())
        return 0;
    std::vector<std::pair<size_t, DataMarker>> saved;
    saved.reserve(indices.size());
    for (size_t i : indices)
        saved.emplace_back(i, di.markers()[i]);
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        di.remove_marker(*it);
    if (undo)
    {
        DataInteraction* dip = &di;
        undo->push(UndoAction{saved.size() == 1 ? "Clear data tip" : "Clear data tips",
                              [dip, saved, current_figure]()
                              { restore_markers(*dip, saved, current_figure); },
                              [dip, saved]() { remove_saved(*dip, saved); }});
    }
    return saved.size();
}

bool center_view_on_marker(const DataMarker&   m,
                           Figure&             figure,
                           UndoManager*        undo,
                           const FigureLookup& lookup)
{
    if (m.is_3d || !marker_in_figure(m, figure))
        return false;
    Axes*  axes       = nullptr;
    size_t axes_index = 0;
    for (size_t i = 0; i < figure.axes().size(); ++i)
    {
        if (figure.axes()[i].get() == m.axes)
        {
            axes       = figure.axes_mut()[i].get();
            axes_index = i;
        }
    }
    if (!axes)
        return false;

    auto drawable = [](ScaleType s, double v)
    { return std::isfinite(v) && (!is_log_scale(s) || v > 0.0); };
    if (!drawable(axes->xscale(), m.data_x) || !drawable(axes->yscale(), m.data_y))
        return false;

    const AxisLimits old_x = axes->x_limits();
    const AxisLimits old_y = axes->y_limits();
    const AxisLimits new_x = zoom_limits_centered(axes->xscale(), old_x, m.data_x, 1.0);
    const AxisLimits new_y = zoom_limits_centered(axes->yscale(), old_y, m.data_y, 1.0);
    if (!(std::isfinite(new_x.min) && std::isfinite(new_x.max) && new_x.min < new_x.max
          && std::isfinite(new_y.min) && std::isfinite(new_y.max) && new_y.min < new_y.max))
        return false;
    axes->xlim(new_x.min, new_x.max);
    axes->ylim(new_y.min, new_y.max);

    const FigureId id = lookup ? lookup.id_of(&figure) : INVALID_FIGURE_ID;
    if (undo && id != 0 && id != INVALID_FIGURE_ID)
    {
        auto apply = [resolve = lookup.resolve, id, axes_index](AxisLimits x, AxisLimits y)
        {
            FigureLookup l{nullptr, resolve};
            if (Axes* ax = resolve_axes(l, id, axes_index))
            {
                ax->xlim(x.min, x.max);
                ax->ylim(y.min, y.max);
            }
        };
        undo->push(UndoAction{"Go to data tip",
                              [apply, old_x, old_y]() { apply(old_x, old_y); },
                              [apply, new_x, new_y]() { apply(new_x, new_y); }});
    }
    return true;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
