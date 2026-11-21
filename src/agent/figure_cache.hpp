#pragma once

// The multiproc window agent's (spectra-window) cache of the daemon's figure
// state: every STATE_DIFF op is applied to the cached SnapshotFigureState so
// a later rebuild of the live Figure from the cache (ADD_SERIES, ADD_AXES,
// REMOVE_SERIES, bar categories, a stats series data update) keeps every
// change made since the snapshot.  Header-only so unit tests can check it.

#include <spectra/color.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../ipc/annotation_state.hpp"
#include "../ipc/axes_format_state.hpp"
#include "../ipc/colormap_state.hpp"
#include "../ipc/message.hpp"
#include "../ipc/series_style_state.hpp"
#include "../ipc/style3d_state.hpp"
#include "../ipc/style_state.hpp"
#include "live_series_index.hpp"
#include "snapshot_figure_builder.hpp"

namespace spectra::agent
{

// Upper bounds on indices accepted from IPC diffs. Structural ops grow the
// cache up to the given index, so an unchecked value from a buggy or hostile
// peer could allocate gigabytes.
inline constexpr size_t MAX_CACHE_AXES   = MAX_SNAPSHOT_AXES;
inline constexpr size_t MAX_CACHE_SERIES = size_t(1) << 16;

// ─── Apply a DiffOp to a cached SnapshotFigureState ─────────────────────────
inline void apply_diff_op_to_cache(ipc::SnapshotFigureState& fig, const ipc::DiffOp& op)
{
    switch (op.type)
    {
        case ipc::DiffOp::Type::SET_AXIS_LIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].x_min = op.f1;
                fig.axes[op.axes_index].x_max = op.f2;
                fig.axes[op.axes_index].y_min = op.f3;
                fig.axes[op.axes_index].y_max = op.f4;
            }
            break;
        case ipc::DiffOp::Type::SET_SERIES_COLOR:
            if (op.series_index < fig.series.size())
            {
                fig.series[op.series_index].color_r = op.f1;
                fig.series[op.series_index].color_g = op.f2;
                fig.series[op.series_index].color_b = op.f3;
                fig.series[op.series_index].color_a = op.f4;
                // Explicit colour, unless the op carries a palette slot tag
                // ("C3") or a format letter (theme-aware).
                fig.series[op.series_index].color_index = ipc::parse_palette_slot_tag(op.str_val);
                fig.series[op.series_index].color_letter =
                    (op.str_val.size() == 1 && format_colors::is_letter(op.str_val[0]))
                        ? static_cast<uint8_t>(op.str_val[0])
                        : 0;
            }
            break;
        case ipc::DiffOp::Type::SET_SERIES_VISIBLE:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].visible = op.bool_val;
            break;
        case ipc::DiffOp::Type::SET_FIGURE_TITLE:
            fig.title = op.str_val;
            break;
        case ipc::DiffOp::Type::SET_GRID_VISIBLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].grid_visible = op.bool_val;
            break;
        case ipc::DiffOp::Type::SET_COLORBAR_VISIBLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].colorbar_visible = op.bool_val;
            break;
        case ipc::DiffOp::Type::SET_LINE_WIDTH:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].line_width = op.f1;
            break;
        case ipc::DiffOp::Type::SET_MARKER_SIZE:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].marker_size = op.f1;
            break;
        case ipc::DiffOp::Type::SET_SERIES_LINE_STYLE:
        {
            const uint8_t v = ipc::style_wire_from_double(op.f1, ipc::LINE_STYLE_WIRE_MAX);
            if (op.series_index < fig.series.size() && v != ipc::SERIES_STYLE_UNSET)
                fig.series[op.series_index].line_style = v;
            break;
        }
        case ipc::DiffOp::Type::SET_SERIES_MARKER_STYLE:
        {
            const uint8_t v = ipc::style_wire_from_double(op.f1, ipc::MARKER_STYLE_WIRE_MAX);
            if (op.series_index < fig.series.size() && v != ipc::SERIES_STYLE_UNSET)
                fig.series[op.series_index].marker_style = v;
            break;
        }
        case ipc::DiffOp::Type::SET_OPACITY:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].opacity = op.f1;
            break;
        case ipc::DiffOp::Type::SET_SERIES_DATA:
            if (op.series_index < fig.series.size())
            {
                auto&        series = fig.series[op.series_index];
                const size_t stride = is_3d_series_type(series.type) ? 3u : 2u;
                series.data         = op.data;
                series.point_count  = static_cast<uint32_t>(op.data.size() / stride);
                series.x_origin     = op.f1;   // 2D data origins (0 for 3D senders)
                series.y_origin     = op.f2;
            }
            break;
        case ipc::DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].z_min = op.f1;
                fig.axes[op.axes_index].z_max = op.f2;
            }
            break;
        case ipc::DiffOp::Type::ADD_SERIES:
        {
            if (op.series_index >= MAX_CACHE_SERIES)
                break;
            ipc::SnapshotSeriesState s;
            s.type       = op.str_val;
            s.axes_index = op.axes_index;
            ipc::style_new_series(fig.style, s);
            // Auto colour by position in its axes (mirrors FigureModel::add_series).
            s.color_index = static_cast<int32_t>(std::count_if(
                fig.series.begin(),
                fig.series.begin() + std::min<size_t>(op.series_index, fig.series.size()),
                [&](const ipc::SnapshotSeriesState& o) { return o.axes_index == op.axes_index; }));
            // Grow series list to accommodate the new index
            if (fig.series.size() <= op.series_index)
                fig.series.resize(static_cast<size_t>(op.series_index) + 1);
            fig.series[op.series_index] = std::move(s);
            break;
        }
        case ipc::DiffOp::Type::REMOVE_SERIES:
            // Mirrors FigureModel::remove_series: later series shift down by one.
            if (op.series_index < fig.series.size())
                fig.series.erase(fig.series.begin() + op.series_index);
            break;
        case ipc::DiffOp::Type::ADD_AXES:
        {
            if (op.axes_index >= MAX_CACHE_AXES)
                break;
            ipc::SnapshotAxisState ax;
            ax.is_3d = op.bool_val;
            ipc::style_new_axes(fig.style, ax);
            // Grow axes list to accommodate the new index
            if (fig.axes.size() <= op.axes_index)
                fig.axes.resize(static_cast<size_t>(op.axes_index) + 1);
            fig.axes[op.axes_index] = std::move(ax);
            // f1/f2 carry the daemon's (grown) subplot grid; 0 from older daemons.
            const auto grid_dim = [](double v) -> int32_t
            {
                if (!(v >= 1.0))
                    return 0;
                return static_cast<int32_t>(std::min(v, static_cast<double>(MAX_GRID_DIM)));
            };
            fig.grid_rows = std::max(fig.grid_rows, grid_dim(op.f1));
            fig.grid_cols = std::max(fig.grid_cols, grid_dim(op.f2));
            break;
        }
        case ipc::DiffOp::Type::SET_SERIES_LABEL:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].name = op.str_val;
            break;
        case ipc::DiffOp::Type::SET_AXIS_XLABEL:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].x_label = op.str_val;
            break;
        case ipc::DiffOp::Type::SET_AXIS_YLABEL:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].y_label = op.str_val;
            break;
        case ipc::DiffOp::Type::SET_AXIS_TITLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].title = op.str_val;
            break;
        case ipc::DiffOp::Type::ADD_ANNOTATION:
        case ipc::DiffOp::Type::SET_ANNOTATION:
        case ipc::DiffOp::Type::CLEAR_ANNOTATIONS:
            if (op.axes_index < fig.axes.size())
                ipc::apply_annotation_op(fig.axes[op.axes_index].annotations, op);
            break;
        case ipc::DiffOp::Type::SET_AXIS_SCALE:
            if (op.axes_index < fig.axes.size())
                ipc::apply_axis_scale_op(fig.axes[op.axes_index], op);
            break;
        case ipc::DiffOp::Type::SET_LEGEND_POSITION:
            fig.legend_position =
                ipc::legend_position_to_wire(ipc::legend_position_from_wire(op.f1));
            break;
        case ipc::DiffOp::Type::SET_SERIES_DRAW_STYLE:
            if (op.series_index < fig.series.size())
                ipc::apply_series_draw_style_op(fig.series[op.series_index], op);
            break;
        case ipc::DiffOp::Type::SET_FIGURE_STYLE:
            ipc::restyle_snapshot(fig.style, fig.axes, fig.series, op.str_val);
            break;
        case ipc::DiffOp::Type::SET_FIGURE_SUPTITLE:
        case ipc::DiffOp::Type::SET_FIGURE_LAYOUT:
            ipc::apply_figure_layout_op(fig, op);
            break;
        case ipc::DiffOp::Type::SET_SERIES_CATEGORIES:
        {
            std::vector<std::string> names;
            if (op.series_index < fig.series.size() && ipc::categories_from_op(op, names))
                fig.series[op.series_index].categories = std::move(names);
            break;
        }
        case ipc::DiffOp::Type::SET_SERIES_STYLE_3D:
            if (op.series_index < fig.series.size())
                ipc::series_style_3d_from_op(op, fig.series[op.series_index]);
            break;
        case ipc::DiffOp::Type::SET_AXES_LIGHT_3D:
            if (op.axes_index < fig.axes.size())
                ipc::axes_light_3d_from_op(op, fig.axes[op.axes_index]);
            break;
        case ipc::DiffOp::Type::SET_SERIES_COLORMAP:
            if (op.series_index < fig.series.size())
                ipc::series_colormap_from_op(op, fig.series[op.series_index]);
            break;
        case ipc::DiffOp::Type::SET_SERIES_COLOR_VALUES:
            if (op.series_index < fig.series.size() && op.data.size() <= ipc::MAX_COLOR_VALUES)
                fig.series[op.series_index].color_values = op.data;
            break;
        case ipc::DiffOp::Type::SET_LIVE_FPS:
            // Kept so a rebuild keeps live streaming (and its rate); a
            // disabling op (bool_val false) turns it off.
            if (!op.bool_val)
                fig.live_fps = 0.0f;
            else if (op.f1 > 0.0 && std::isfinite(op.f1))
                fig.live_fps = static_cast<float>(op.f1);
            else if (fig.live_fps <= 0.0f)
                fig.live_fps = 60.0f;
            break;
        default:
            // ADD_FIGURE / REMOVE_FIGURE (figure list, not one figure's state),
            // SET_KNOB_VALUE (knobs live outside the figure cache) and
            // SET_ANIM_PLAYBACK (agent -> app transport, never cached).
            break;
    }
}

}   // namespace spectra::agent
