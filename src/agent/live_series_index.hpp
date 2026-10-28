#pragma once

// Maps a figure-wide series index (as used by the daemon's FigureModel and by
// every series-level DiffOp) to the (axes, position-in-axes) pair that
// addresses the series in a live Figure built from the snapshot cache.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "../ipc/message.hpp"

namespace spectra::agent
{

// Upper bound on the number of axes the window builds for one figure.
constexpr size_t MAX_SNAPSHOT_AXES = 1024;

// True for series types that need a 3D axes.
inline bool is_3d_series_type(const std::string& t)
{
    return t == "line3d" || t == "scatter3d" || t == "surface" || t == "mesh";
}

// Number of axes the window builds for a cached figure (always at least one).
inline size_t built_axes_count(const ipc::SnapshotFigureState& fig)
{
    return std::clamp(fig.axes.size(), size_t(1), MAX_SNAPSHOT_AXES);
}

// True when the window builds axes `axes_index` as a 3D axes.
inline bool built_axes_is_3d(const ipc::SnapshotFigureState& fig, uint32_t axes_index)
{
    return axes_index < fig.axes.size() && fig.axes[axes_index].is_3d;
}

// True when the window places this cached series in its axes.  A series whose
// axes does not exist, or whose dimensionality does not match its axes (a 3D
// type in a 2D axes or the reverse), is not built and so takes no slot in the
// live axes' series list.
inline bool series_is_built(const ipc::SnapshotFigureState& fig, const ipc::SnapshotSeriesState& ss)
{
    if (ss.axes_index >= built_axes_count(fig))
        return false;
    return is_3d_series_type(ss.type) == built_axes_is_3d(fig, ss.axes_index);
}

struct LiveSeriesIndex
{
    uint32_t axes_index   = 0;
    uint32_t series_index = 0;   // position within its axes
};

// Returns false when global_index is out of range for the cached figure, or
// names a series the window did not build (see series_is_built()).
inline bool locate_live_series(const ipc::SnapshotFigureState& fig,
                               uint32_t                        global_index,
                               LiveSeriesIndex&                out)
{
    if (global_index >= fig.series.size() || !series_is_built(fig, fig.series[global_index]))
        return false;
    const uint32_t axes_index = fig.series[global_index].axes_index;
    uint32_t       local      = 0;
    for (uint32_t i = 0; i < global_index; ++i)
    {
        if (fig.series[i].axes_index == axes_index && series_is_built(fig, fig.series[i]))
            ++local;
    }
    out.axes_index   = axes_index;
    out.series_index = local;
    return true;
}

// True for DiffOps that address one existing series by its figure-wide index.
inline bool is_series_property_op(ipc::DiffOp::Type t)
{
    switch (t)
    {
        case ipc::DiffOp::Type::SET_SERIES_COLOR:
        case ipc::DiffOp::Type::SET_SERIES_VISIBLE:
        case ipc::DiffOp::Type::SET_LINE_WIDTH:
        case ipc::DiffOp::Type::SET_MARKER_SIZE:
        case ipc::DiffOp::Type::SET_OPACITY:
        case ipc::DiffOp::Type::SET_SERIES_DATA:
        case ipc::DiffOp::Type::SET_SERIES_LABEL:
            return true;
        default:
            return false;
    }
}

// True for DiffOps that edit an axes' text annotations (series_index is the
// annotation index, not a series).
inline bool is_annotation_op(ipc::DiffOp::Type t)
{
    return t == ipc::DiffOp::Type::ADD_ANNOTATION || t == ipc::DiffOp::Type::SET_ANNOTATION
           || t == ipc::DiffOp::Type::CLEAR_ANNOTATIONS;
}

}   // namespace spectra::agent
