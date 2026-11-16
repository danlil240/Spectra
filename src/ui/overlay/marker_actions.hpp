#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstddef>
    #include <functional>
    #include <spectra/fwd.hpp>
    #include <string>
    #include <vector>

    #include "data_marker.hpp"

namespace spectra
{

class DataInteraction;
class Figure;
class UndoManager;

// Data tip (marker) actions behind the nav rail "Markers" popover: list the
// tips of a figure, remove one or all of them with undo, and pan a view to one.

// Returns the figure the undo callbacks validate restored markers against
// (normally the window's current figure); may return nullptr.
using CurrentFigureFn = std::function<Figure*()>;

// Lifetime-safe figure lookup for undo callbacks that outlive a figure (a tab
// can be closed while its actions stay in the undo history): undo/redo keep
// the figure's id and re-resolve it, doing nothing once it is gone.
struct FigureLookup
{
    // Id of a live figure; 0 or INVALID_FIGURE_ID when unknown.
    std::function<FigureId(const Figure*)> id_of;
    // Live figure for an id, or nullptr once it was closed.
    std::function<Figure*(FigureId)> resolve;

    explicit operator bool() const { return id_of && resolve; }
};

// True when @p m's axes (2D or 3D) belongs to @p figure and its series still
// lives in that axes.  Pointers are only compared, never dereferenced.
bool marker_in_figure(const DataMarker& m, const Figure& figure);

// Indices into @p markers of the tips that belong to @p figure.
std::vector<size_t> figure_marker_indices(const std::vector<DataMarker>& markers,
                                          const Figure&                  figure);

// Series name shown for a tip: its series label, or "Series" when unnamed.
std::string marker_series_name(const DataMarker& m);

// Remove tip @p idx.  With @p undo, pushes "Remove data tip": undo re-inserts
// it (only while it still belongs to current_figure()), redo removes it again.
bool remove_marker_undoable(DataInteraction& di,
                            size_t           idx,
                            UndoManager*     undo,
                            CurrentFigureFn  current_figure);

// Remove every tip of @p figure; returns how many were removed.  With
// @p undo, pushes one "Clear data tips" action restoring them.
size_t clear_figure_markers_undoable(DataInteraction& di,
                                     const Figure&    figure,
                                     UndoManager*     undo,
                                     CurrentFigureFn  current_figure);

// Pan the 2D axes of @p m (looked up in @p figure) so the tip is centred,
// keeping the visible span in axis space (log / signed-sqrt aware, see
// core/axis_scale.hpp).  With @p undo and a valid @p lookup an undoable entry
// is pushed; it stores the figure id and axes index and re-resolves them, so
// undoing after the figure was closed does nothing.  Returns false for 3D
// tips, tips not in @p figure, or a centre the axis cannot show.
bool center_view_on_marker(const DataMarker&   m,
                           Figure&             figure,
                           UndoManager*        undo,
                           const FigureLookup& lookup = {});

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
