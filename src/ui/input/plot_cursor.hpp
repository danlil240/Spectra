#pragma once

// Mouse cursor shown over the plot area for the active tool, and the command
// and default key of each tool.  Header-only and toolkit-free: the ImGui
// integration, the Qt runtime and the embed surface each map PlotCursor onto
// the cursors their platform has.

#include "ui/input/input.hpp"

namespace spectra
{

enum class PlotCursor
{
    Default,     // Leave the cursor alone (arrow, or whatever a widget asked for)
    OpenHand,    // Pan tool, ready to drag
    Grabbing,    // A pan drag in progress (any tool's middle-button pan too)
    Crosshair,   // Precise placement: box zoom, measure, ROI
    Text,        // Annotate: click places a text note
};

// Cursor a UI widget asked for this frame (splitters, text fields, links),
// mirrored from ImGui so non-ImGui hosts (Qt) need no ImGui headers.
enum class WidgetCursor
{
    Arrow,
    TextInput,
    ResizeAll,
    ResizeNS,
    ResizeEW,
    ResizeNESW,
    ResizeNWSE,
    Hand,
    NotAllowed,
};

// Cursor for @p tool.  @p over_plot: the pointer is over an axes data area
// with no UI widget in the way.  @p panning: a pan drag is in progress, which
// keeps the grab cursor even when the pointer leaves the axes.
constexpr PlotCursor plot_cursor_for(ToolMode tool, bool over_plot, bool panning)
{
    if (panning)
        return PlotCursor::Grabbing;
    if (!over_plot)
        return PlotCursor::Default;
    switch (tool)
    {
        case ToolMode::Pan:
            return PlotCursor::OpenHand;
        case ToolMode::BoxZoom:
        case ToolMode::Measure:
        case ToolMode::ROI:
            return PlotCursor::Crosshair;
        case ToolMode::Annotate:
            return PlotCursor::Text;
        case ToolMode::Select:
            return PlotCursor::Default;
    }
    return PlotCursor::Default;
}

// True while @p input is dragging the view (left-drag in Pan, middle-drag in
// any tool), i.e. when the grab cursor applies.
inline bool plot_is_panning(const InputHandler& input)
{
    if (input.is_middle_pan_dragging())
        return true;
    return input.tool_mode() == ToolMode::Pan && input.mode() == InteractionMode::Dragging
           && !input.box_zoom_rect().active;
}

// Command that selects @p tool ("tool.pan", ...).
constexpr const char* tool_command_id(ToolMode tool)
{
    switch (tool)
    {
        case ToolMode::Pan:
            return "tool.pan";
        case ToolMode::BoxZoom:
            return "tool.box_zoom";
        case ToolMode::Select:
            return "tool.select";
        case ToolMode::Measure:
            return "tool.measure";
        case ToolMode::Annotate:
            return "tool.annotate";
        case ToolMode::ROI:
            return "tool.roi";
    }
    return "tool.pan";
}

}   // namespace spectra
