#pragma once

// style_undo.hpp — apply a plot style (StyleSheet) to a figure with undo.
//
// A style touches the figure (background, palette, legend font), every axes
// (grid, minor grid / ticks, spines, tick direction / length, font sizes,
// new-series defaults) and the width / marker size of existing series, so undo
// restores a snapshot of all of it.

#include <optional>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/style.hpp>
#include <string>
#include <vector>

#include "ui/commands/undo_manager.hpp"

namespace spectra
{

struct FigureStyleSnapshot
{
    struct AxesState
    {
        AxesBase*     axes        = nullptr;
        bool          grid        = true;
        bool          minor_grid  = true;
        bool          minor_ticks = false;
        bool          despine     = false;
        TickDirection tick_dir    = TickDirection::Out;
        AxisStyle     axis_style;
        float         line_width  = 2.0f;
        float         marker_size = 6.0f;
    };
    struct SeriesState
    {
        Series* series      = nullptr;
        float   line_width  = 0.0f;   // LineSeries only
        float   marker_size = 0.0f;
    };

    Figure*                   figure = nullptr;
    FigureStyle               style;
    float                     legend_font_size = 0.0f;
    std::optional<StyleSheet> sheet;
    std::vector<AxesState>    axes;
    std::vector<SeriesState>  series;
};

inline FigureStyleSnapshot capture_figure_style(Figure& fig)
{
    FigureStyleSnapshot snap;
    snap.figure           = &fig;
    snap.style            = fig.style();
    snap.legend_font_size = fig.legend().font_size;
    snap.sheet            = fig.style_sheet();
    fig.for_each_axes(
        [&](AxesBase* ax)
        {
            FigureStyleSnapshot::AxesState a;
            a.axes        = ax;
            a.grid        = ax->grid_enabled();
            a.minor_grid  = ax->minor_grid_enabled();
            a.minor_ticks = ax->minor_ticks_enabled();
            a.despine     = ax->despined();
            a.tick_dir    = ax->tick_direction();
            a.axis_style  = ax->axis_style();
            a.line_width  = ax->default_line_width();
            a.marker_size = ax->default_marker_size();
            snap.axes.push_back(a);
            for (auto& sp : ax->series_mut())
            {
                if (!sp)
                    continue;
                FigureStyleSnapshot::SeriesState s;
                s.series      = sp.get();
                s.marker_size = sp->marker_size();
                if (auto* line = dynamic_cast<LineSeries*>(sp.get()))
                    s.line_width = line->width();
                snap.series.push_back(s);
            }
        });
    return snap;
}

inline void restore_figure_style(const FigureStyleSnapshot& snap)
{
    if (!snap.figure)
        return;
    Figure& fig            = *snap.figure;
    fig.style()            = snap.style;
    fig.legend().font_size = snap.legend_font_size;
    fig.set_style_sheet(snap.sheet);
    for (const auto& a : snap.axes)
    {
        a.axes->grid(a.grid);
        a.axes->minor_grid(a.minor_grid);
        a.axes->minor_ticks(a.minor_ticks);
        a.axes->despine(a.despine);
        a.axes->tick_direction(a.tick_dir);
        a.axes->axis_style() = a.axis_style;
        a.axes->set_series_defaults(a.line_width, a.marker_size);
    }
    for (const auto& s : snap.series)
    {
        if (auto* line = dynamic_cast<LineSeries*>(s.series))
        {
            line->width(s.line_width);
            line->marker_size(s.marker_size);
        }
        else
        {
            s.series->marker_size(s.marker_size);
        }
    }
}

/// Apply @p style to @p fig and push one undo step for it (when @p mgr is set).
inline void undoable_apply_style(UndoManager* mgr, Figure& fig, const StyleSheet& style)
{
    FigureStyleSnapshot before = capture_figure_style(fig);
    fig.apply_style(style);
    if (!mgr)
        return;
    FigureStyleSnapshot after = capture_figure_style(fig);
    mgr->push(UndoAction{"Apply " + style::display_name(style.name) + " style",
                         [before]() { restore_figure_style(before); },
                         [after]() { restore_figure_style(after); }});
}

}   // namespace spectra
