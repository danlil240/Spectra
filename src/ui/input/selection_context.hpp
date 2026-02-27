#pragma once

#include <algorithm>
#include <spectra/fwd.hpp>
#include <vector>

namespace spectra::ui
{

enum class SelectionType
{
    None,
    Figure,
    Axes,
    Series,
    SeriesBrowser
};

// Per-series entry in a multi-selection
struct SelectedSeriesEntry
{
    Series*   series       = nullptr;
    AxesBase* axes_base    = nullptr;
    Axes*     axes         = nullptr;
    int       axes_index   = -1;
    int       series_index = -1;
};

struct SelectionContext
{
    SelectionType type         = SelectionType::None;
    Figure*       figure       = nullptr;
    Axes*         axes         = nullptr;
    AxesBase*     axes_base    = nullptr;   // Always set (2D or 3D)
    Series*       series       = nullptr;
    int           axes_index   = -1;   // Which axes in the figure (for display)
    int           series_index = -1;   // Which series in the axes (for display)

    // Multi-series selection (populated when multiple series are selected)
    std::vector<SelectedSeriesEntry> selected_series;

    void clear()
    {
        type         = SelectionType::None;
        figure       = nullptr;
        axes         = nullptr;
        axes_base    = nullptr;
        series       = nullptr;
        axes_index   = -1;
        series_index = -1;
        selected_series.clear();
    }

    void select_figure(Figure* fig)
    {
        clear();
        type   = SelectionType::Figure;
        figure = fig;
    }

    void select_axes(Figure* fig, Axes* ax, int idx)
    {
        clear();
        type       = SelectionType::Axes;
        figure     = fig;
        axes       = ax;
        axes_index = idx;
        // axes_base set by caller if needed
    }

    void select_series_browser(Figure* fig)
    {
        clear();
        type   = SelectionType::SeriesBrowser;
        figure = fig;
    }

    void select_series(Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
    {
        clear();
        type         = SelectionType::Series;
        figure       = fig;
        axes         = ax;
        axes_index   = ax_idx;
        series       = s;
        series_index = s_idx;
        // axes_base set by caller (needs full type info for Axes→AxesBase cast)
        // Also add to multi-selection list for uniform handling
        selected_series.push_back({s, nullptr, ax, ax_idx, s_idx});
    }

    // Add a series to multi-selection (shift-click / range select)
    void add_series(Figure* fig, Axes* ax, AxesBase* ab, int ax_idx, Series* s, int s_idx)
    {
        if (!s)
            return;
        // If not already in Series mode, switch to it
        if (type != SelectionType::Series)
        {
            clear();
            type   = SelectionType::Series;
            figure = fig;
        }
        // Don't add duplicates
        for (const auto& e : selected_series)
        {
            if (e.series == s)
                return;
        }
        selected_series.push_back({s, ab, ax, ax_idx, s_idx});
        // Primary selection = last added
        series       = s;
        axes         = ax;
        axes_base    = ab;
        axes_index   = ax_idx;
        series_index = s_idx;
        figure       = fig;
    }

    // Toggle a series in/out of multi-selection
    void toggle_series(Figure* fig, Axes* ax, AxesBase* ab, int ax_idx, Series* s, int s_idx)
    {
        if (!s)
            return;
        // If already selected, remove it
        auto it = std::find_if(selected_series.begin(),
                               selected_series.end(),
                               [s](const SelectedSeriesEntry& e) { return e.series == s; });
        if (it != selected_series.end())
        {
            selected_series.erase(it);
            if (selected_series.empty())
            {
                clear();
            }
            else
            {
                // Update primary to last in list
                auto& last   = selected_series.back();
                series       = last.series;
                axes         = last.axes;
                axes_base    = last.axes_base;
                axes_index   = last.axes_index;
                series_index = last.series_index;
            }
            return;
        }
        // Not selected — add it
        add_series(fig, ax, ab, ax_idx, s, s_idx);
    }

    // Check if a specific series is in the multi-selection
    bool is_selected(const Series* s) const
    {
        if (!s)
            return false;
        for (const auto& e : selected_series)
        {
            if (e.series == s)
                return true;
        }
        return false;
    }

    // Number of selected series
    size_t selected_count() const { return selected_series.size(); }

    // Check if we have a multi-selection (more than 1)
    bool has_multi_selection() const { return selected_series.size() > 1; }
};

}   // namespace spectra::ui
