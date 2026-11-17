#pragma once

// style_undo.hpp — apply a plot style (StyleSheet) to a figure with undo.
//
// A style touches the figure (background, palette, legend font), every axes
// (grid, minor grid / ticks, spines, tick direction / length, font sizes,
// new-series defaults) and the width / marker size of existing series, so undo
// restores a snapshot of all of it.

#include <cstdint>
#include <functional>
#include <optional>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <spectra/style.hpp>
#include <string>
#include <vector>

#include "ui/commands/undo_manager.hpp"

namespace spectra
{

// Undo outlives what it refers to: a series can be deleted (Inspector trash,
// deferred removal), an axes removed or the figure tab closed after the style
// was applied.  So the snapshot holds no pointer it would dereference: restore
// walks the live figure's current axes / series and applies an entry only to
// an object that is still there.  Series are matched by Series::instance_id()
// (an address can be reused by a new series), axes by address and position,
// and the figure is re-resolved by id through a StyleFigureLookup when given.
struct FigureStyleSnapshot
{
    struct AxesState
    {
        const AxesBase* axes        = nullptr;   // identity only, never dereferenced
        size_t          index       = 0;         // position in for_each_axes()
        bool            grid        = true;
        bool            minor_grid  = true;
        bool            minor_ticks = false;
        bool            despine     = false;
        TickDirection   tick_dir    = TickDirection::Out;
        AxisStyle       axis_style;
        float           line_width  = 2.0f;
        float           marker_size = 6.0f;
    };
    struct SeriesState
    {
        std::uint64_t id          = 0;      // Series::instance_id()
        float         line_width  = 0.0f;   // LineSeries only
        float         marker_size = 0.0f;
        float         point_size  = 0.0f;   // ScatterSeries::size()
    };

    Figure*  figure    = nullptr;   // used when there is no lookup
    FigureId figure_id = INVALID_FIGURE_ID;
    // Only the FigureStyle fields a StyleSheet sets (Figure::apply_style), so
    // undo leaves layout settings (margins, constrained layout) alone.
    bool                      background_override = false;
    Color                     background;
    std::string               palette;
    float                     legend_font_size = 0.0f;
    std::optional<StyleSheet> sheet;
    std::vector<AxesState>    axes;
    std::vector<SeriesState>  series;
};

/// Re-resolves a figure by id so an undo entry is a no-op once the figure is
/// gone.  Without one, the figure pointer is trusted (tests, embedders whose
/// figures outlive their undo history).
struct StyleFigureLookup
{
    std::function<FigureId(const Figure*)> id_of;
    std::function<Figure*(FigureId)>       resolve;
    explicit                               operator bool() const { return id_of && resolve; }
};

inline FigureStyleSnapshot capture_figure_style(Figure& fig, const StyleFigureLookup& lookup = {})
{
    FigureStyleSnapshot snap;
    snap.figure = &fig;
    if (lookup)
    {
        const FigureId id = lookup.id_of(&fig);   // 0 = unknown, like INVALID_FIGURE_ID
        snap.figure_id    = id == 0 ? INVALID_FIGURE_ID : id;
    }
    snap.background_override = fig.style().background_override;
    snap.background          = fig.style().background;
    snap.palette             = fig.style().palette;
    snap.legend_font_size    = fig.legend().font_size;
    snap.sheet               = fig.style_sheet();
    fig.for_each_axes(
        [&](AxesBase* ax)
        {
            FigureStyleSnapshot::AxesState a;
            a.axes        = ax;
            a.index       = snap.axes.size();
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
                s.id          = sp->instance_id();
                s.marker_size = sp->marker_size();
                if (auto* line = dynamic_cast<LineSeries*>(sp.get()))
                    s.line_width = line->width();
                else if (auto* scatter = dynamic_cast<ScatterSeries*>(sp.get()))
                    s.point_size = scatter->size();
                snap.series.push_back(s);
            }
        });
    return snap;
}

/// Restore @p snap onto the live figure.  Returns false (and changes nothing)
/// when the figure no longer exists.
inline bool restore_figure_style(const FigureStyleSnapshot& snap,
                                 const StyleFigureLookup&   lookup = {})
{
    Figure* fig = snap.figure;
    if (lookup && snap.figure_id != INVALID_FIGURE_ID)
        fig = lookup.resolve(snap.figure_id);
    if (!fig)
        return false;

    fig->style().background_override = snap.background_override;
    fig->style().background          = snap.background;
    fig->style().palette             = snap.palette;
    fig->legend().font_size          = snap.legend_font_size;
    fig->set_style_sheet(snap.sheet);

    auto find_axes = [&](const AxesBase* ax, size_t index) -> const FigureStyleSnapshot::AxesState*
    {
        for (const auto& a : snap.axes)
            if (a.axes == ax && a.index == index)
                return &a;
        return nullptr;
    };
    auto find_series = [&](std::uint64_t id) -> const FigureStyleSnapshot::SeriesState*
    {
        for (const auto& s : snap.series)
            if (s.id == id)
                return &s;
        return nullptr;
    };
    size_t axes_index = 0;

    fig->for_each_axes(
        [&](AxesBase* ax)
        {
            if (const auto* a = find_axes(ax, axes_index++))
            {
                ax->grid(a->grid);
                ax->minor_grid(a->minor_grid);
                ax->minor_ticks(a->minor_ticks);
                ax->despine(a->despine);
                ax->tick_direction(a->tick_dir);
                ax->axis_style() = a->axis_style;
                ax->set_series_defaults(a->line_width, a->marker_size);
            }
            for (auto& sp : ax->series_mut())
            {
                const auto* s = sp ? find_series(sp->instance_id()) : nullptr;
                if (!s)
                    continue;   // added after the snapshot
                if (auto* line = dynamic_cast<LineSeries*>(sp.get()))
                {
                    line->width(s->line_width);
                    line->marker_size(s->marker_size);
                }
                else if (auto* scatter = dynamic_cast<ScatterSeries*>(sp.get()))
                {
                    scatter->size(s->point_size);
                    scatter->marker_size(s->marker_size);
                }
                else
                {
                    sp->marker_size(s->marker_size);
                }
            }
        });
    return true;
}

/// Apply @p style to @p fig and push one undo step for it (when @p mgr is set).
/// Pass @p lookup whenever the figure can be closed while the step is in the
/// undo history.
inline void undoable_apply_style(UndoManager*             mgr,
                                 Figure&                  fig,
                                 const StyleSheet&        style,
                                 const StyleFigureLookup& lookup = {})
{
    FigureStyleSnapshot before = capture_figure_style(fig, lookup);
    fig.apply_style(style);
    if (!mgr)
        return;
    FigureStyleSnapshot after = capture_figure_style(fig, lookup);
    mgr->push(UndoAction{"Apply " + style::display_name(style.name) + " style",
                         [before, lookup]() { restore_figure_style(before, lookup); },
                         [after, lookup]() { restore_figure_style(after, lookup); }});
}

}   // namespace spectra
