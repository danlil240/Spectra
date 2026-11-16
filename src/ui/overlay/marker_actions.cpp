#ifdef SPECTRA_USE_IMGUI

    #include "marker_actions.hpp"

    #include <algorithm>
    #include <cmath>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>

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

double to_scale(ScaleType s, double v)
{
    switch (s)
    {
        case ScaleType::Log10:
            return std::log10(v);
        case ScaleType::Log2:
            return std::log2(v);
        case ScaleType::Sqrt:
            return std::sqrt(std::max(v, 0.0));
        case ScaleType::Linear:
            break;
    }
    return v;
}

double from_scale(ScaleType s, double v)
{
    switch (s)
    {
        case ScaleType::Log10:
            return std::pow(10.0, v);
        case ScaleType::Log2:
            return std::exp2(v);
        case ScaleType::Sqrt:
            return v * v;
        case ScaleType::Linear:
            break;
    }
    return v;
}

// Limits [lo, hi] shifted so @p centre is in the middle, in scale space.
// Returns false when the value cannot be shown on this scale.
bool centred_limits(ScaleType s,
                    double    lo,
                    double    hi,
                    double    centre,
                    double&   out_lo,
                    double&   out_hi)
{
    const bool log = s == ScaleType::Log10 || s == ScaleType::Log2;
    if ((log && (centre <= 0.0 || lo <= 0.0 || hi <= 0.0))
        || (s == ScaleType::Sqrt && centre < 0.0))
        return false;
    const double a    = to_scale(s, lo);
    const double b    = to_scale(s, hi);
    const double c    = to_scale(s, centre);
    const double half = 0.5 * (b - a);
    out_lo            = from_scale(s, c - half);
    out_hi            = from_scale(s, c + half);
    return std::isfinite(out_lo) && std::isfinite(out_hi) && out_lo < out_hi;
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

bool center_view_on_marker(const DataMarker& m, Figure& figure, UndoManager* undo)
{
    if (m.is_3d || !marker_in_figure(m, figure))
        return false;
    Axes* axes = nullptr;
    for (auto& a : figure.axes_mut())
        if (a.get() == m.axes)
            axes = a.get();
    if (!axes)
        return false;

    const AxisLimits old_x = axes->x_limits();
    const AxisLimits old_y = axes->y_limits();
    double           x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
    if (!centred_limits(axes->xscale(), old_x.min, old_x.max, m.data_x, x0, x1)
        || !centred_limits(axes->yscale(), old_y.min, old_y.max, m.data_y, y0, y1))
        return false;
    axes->xlim(x0, x1);
    axes->ylim(y0, y1);
    if (undo)
    {
        undo->push(UndoAction{"Go to data tip",
                              [axes, old_x, old_y]()
                              {
                                  axes->xlim(old_x.min, old_x.max);
                                  axes->ylim(old_y.min, old_y.max);
                              },
                              [axes, x0, x1, y0, y1]()
                              {
                                  axes->xlim(x0, x1);
                                  axes->ylim(y0, y1);
                              }});
    }
    return true;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
