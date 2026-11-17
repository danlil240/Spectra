// style_state.hpp — plot styles (StyleSheet) on the IPC snapshot model.
//
// A figure's built-in plot style travels by name (SnapshotFigureState::style,
// DiffOp SET_FIGURE_STYLE).  The window applies it with Figure::apply_style();
// the backend model and the window's snapshot cache mirror the parts the
// snapshot itself carries (axes grid, series line width / marker size) with
// these helpers, so a rebuild from the snapshot keeps the styled look.

#pragma once

#include <spectra/style.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "message.hpp"

namespace spectra::ipc
{

// Series types whose width / marker size follow the style (Axes::add_series).
inline bool style_sets_line_width(const SnapshotSeriesState& s)
{
    return s.type == "line";
}
inline bool style_sets_marker_size(const SnapshotSeriesState& s)
{
    return style_sets_line_width(s) || s.type == "scatter";
}

// The style named `name`, or a default StyleSheet when there is none.
inline StyleSheet snapshot_style_or_default(std::string_view name)
{
    const StyleSheet* st = style::find(name);
    return st ? *st : StyleSheet{};
}

// New axes / series of a styled figure (mirrors Figure::subplot and
// Axes::add_series applying the figure's style).
inline void style_new_axes(std::string_view style_name, SnapshotAxisState& ax)
{
    if (const StyleSheet* st = style::find(style_name))
        ax.grid_visible = st->grid;
}
inline void style_new_series(std::string_view style_name, SnapshotSeriesState& s)
{
    const StyleSheet* st = style::find(style_name);
    if (!st)
        return;
    if (style_sets_line_width(s))
        s.line_width = st->line_width;
    if (style_sets_marker_size(s))
        s.marker_size = st->marker_size;
}

// Restyle a figure's snapshot state (mirrors Figure::apply_style): every axes
// takes the style's grid; series still at the previous style's default width /
// marker size take the new one.  Returns false (nothing changed) for an
// unknown style name.
inline bool restyle_snapshot(std::string&                      style,
                             std::vector<SnapshotAxisState>&   axes,
                             std::vector<SnapshotSeriesState>& series,
                             std::string_view                  name)
{
    const StyleSheet* st = style::find(name);
    if (!st)
        return false;
    const StyleSheet prev = snapshot_style_or_default(style);
    for (auto& ax : axes)
        ax.grid_visible = st->grid;
    for (auto& s : series)
    {
        if (style_sets_line_width(s) && s.line_width == prev.line_width)
            s.line_width = st->line_width;
        if (style_sets_marker_size(s) && s.marker_size == prev.marker_size)
            s.marker_size = st->marker_size;
    }
    style = st->name;
    return true;
}

inline DiffOp make_figure_style_op(uint64_t figure_id, std::string_view name)
{
    DiffOp op;
    op.type      = DiffOp::Type::SET_FIGURE_STYLE;
    op.figure_id = figure_id;
    op.str_val   = std::string(name);
    return op;
}

}   // namespace spectra::ipc
