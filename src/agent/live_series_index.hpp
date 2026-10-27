#pragma once

// Maps a figure-wide series index (as used by the daemon's FigureModel and by
// every series-level DiffOp) to the (axes, position-in-axes) pair that
// addresses the series in a live Figure built from the snapshot cache.

#include <cstdint>

#include "../ipc/message.hpp"

namespace spectra::agent
{

struct LiveSeriesIndex
{
    uint32_t axes_index   = 0;
    uint32_t series_index = 0;   // position within its axes
};

// Returns false when global_index is out of range for the cached figure.
inline bool locate_live_series(const ipc::SnapshotFigureState& fig,
                               uint32_t                        global_index,
                               LiveSeriesIndex&                out)
{
    if (global_index >= fig.series.size())
        return false;
    const uint32_t axes_index = fig.series[global_index].axes_index;
    uint32_t       local      = 0;
    for (uint32_t i = 0; i < global_index; ++i)
    {
        if (fig.series[i].axes_index == axes_index)
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

}   // namespace spectra::agent
