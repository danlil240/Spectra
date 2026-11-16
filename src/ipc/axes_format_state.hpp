// axes_format_state.hpp — Axis scales, date-time tick mode, legend position
// bar category names and line draw styles between the core model and IPC (snapshot fields and
// the SET_AXIS_SCALE / SET_LEGEND_POSITION / SET_SERIES_CATEGORIES diff ops).
//
// Header-only so the app, daemon, window agent and unit tests share one copy.
// Values read from a peer are validated (an IPC peer is untrusted).

#pragma once

#include <cstddef>
#include <cstdint>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "message.hpp"

namespace spectra::ipc
{

// Separator between category names in SET_SERIES_CATEGORIES::str_val.
inline constexpr char CATEGORY_SEPARATOR = '\x1F';
// Upper bound on category names accepted from a peer.
inline constexpr size_t MAX_CATEGORIES = size_t(1) << 16;

inline ScaleType scale_from_wire(double v)
{
    switch (static_cast<int>(v))
    {
        case 1:
            return ScaleType::Log10;
        case 2:
            return ScaleType::Log2;
        case 3:
            return ScaleType::Sqrt;
        default:
            return ScaleType::Linear;
    }
}

inline uint8_t scale_to_wire(ScaleType s)
{
    return static_cast<uint8_t>(s);
}

inline LegendPosition legend_position_from_wire(double v)
{
    const int i = static_cast<int>(v);
    if (!(v >= 0.0) || i > static_cast<int>(LegendPosition::OutsideRight))
        return LegendPosition::Best;
    return static_cast<LegendPosition>(i);
}

inline uint8_t legend_position_to_wire(LegendPosition p)
{
    return static_cast<uint8_t>(p);
}

// ─── Axis scale / date-time ─────────────────────────────────────────────────

inline void axis_format_to_state(const Axes& ax, SnapshotAxisState& sa)
{
    sa.x_scale    = scale_to_wire(ax.xscale());
    sa.y_scale    = scale_to_wire(ax.yscale());
    sa.x_datetime = ax.xaxis_datetime();
    sa.y_datetime = ax.yaxis_datetime();
}

inline void apply_axis_format_state(Axes& ax, const SnapshotAxisState& sa)
{
    ax.xscale(scale_from_wire(sa.x_scale));
    ax.yscale(scale_from_wire(sa.y_scale));
    ax.xaxis_datetime(sa.x_datetime);
    ax.yaxis_datetime(sa.y_datetime);
}

inline bool same_axis_format(const SnapshotAxisState& a, const SnapshotAxisState& b)
{
    return a.x_scale == b.x_scale && a.y_scale == b.y_scale && a.x_datetime == b.x_datetime
           && a.y_datetime == b.y_datetime;
}

inline DiffOp make_axis_scale_op(uint64_t                 figure_id,
                                 uint32_t                 axes_index,
                                 const SnapshotAxisState& sa)
{
    DiffOp op;
    op.type       = DiffOp::Type::SET_AXIS_SCALE;
    op.figure_id  = figure_id;
    op.axes_index = axes_index;
    op.f1         = sa.x_scale;
    op.f2         = sa.y_scale;
    op.f3         = sa.x_datetime ? 1.0 : 0.0;
    op.f4         = sa.y_datetime ? 1.0 : 0.0;
    return op;
}

inline void apply_axis_scale_op(SnapshotAxisState& sa, const DiffOp& op)
{
    sa.x_scale    = scale_to_wire(scale_from_wire(op.f1));
    sa.y_scale    = scale_to_wire(scale_from_wire(op.f2));
    sa.x_datetime = op.f3 != 0.0;
    sa.y_datetime = op.f4 != 0.0;
}

// ─── Legend position ────────────────────────────────────────────────────────

inline DiffOp make_legend_position_op(uint64_t figure_id, LegendPosition pos)
{
    DiffOp op;
    op.type      = DiffOp::Type::SET_LEGEND_POSITION;
    op.figure_id = figure_id;
    op.f1        = legend_position_to_wire(pos);
    return op;
}

// ─── Bar category names ─────────────────────────────────────────────────────

inline DiffOp make_series_categories_op(uint64_t                        figure_id,
                                        uint32_t                        axes_index,
                                        uint32_t                        series_index,
                                        const std::vector<std::string>& names)
{
    DiffOp op;
    op.type         = DiffOp::Type::SET_SERIES_CATEGORIES;
    op.figure_id    = figure_id;
    op.axes_index   = axes_index;
    op.series_index = series_index;
    op.f1           = static_cast<double>(names.size());
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
            op.str_val += CATEGORY_SEPARATOR;
        for (char c : names[i])
            op.str_val += c == CATEGORY_SEPARATOR ? ' ' : c;
    }
    return op;
}

// Names carried by a SET_SERIES_CATEGORIES op; false when malformed.
inline bool categories_from_op(const DiffOp& op, std::vector<std::string>& out)
{
    out.clear();
    if (!(op.f1 >= 0.0) || op.f1 > static_cast<double>(MAX_CATEGORIES))
        return false;
    const auto n = static_cast<size_t>(op.f1);
    if (n == 0)
        return op.str_val.empty();
    std::string cur;
    for (char c : op.str_val)
    {
        if (c == CATEGORY_SEPARATOR)
        {
            out.push_back(std::move(cur));
            cur.clear();
            if (out.size() >= n)
                return false;
        }
        else
        {
            cur += c;
        }
    }
    out.push_back(std::move(cur));
    if (out.size() != n)
    {
        out.clear();
        return false;
    }
    return true;
}

// ─── Line draw style ────────────────────────────────────────────────────────

// Wire values of spectra::DrawStyle / spectra::DenseLineMode; anything out of
// range from a peer reads as the default.
inline uint8_t draw_style_to_wire(DrawStyle s)
{
    return static_cast<uint8_t>(s);
}
inline DrawStyle draw_style_from_wire(double v)
{
    if (!(v >= 0.0 && v <= static_cast<double>(DrawStyle::StepsPost)))
        return DrawStyle::Default;
    return static_cast<DrawStyle>(static_cast<int>(v));
}
inline uint8_t dense_mode_to_wire(DenseLineMode m)
{
    return static_cast<uint8_t>(m);
}
inline DenseLineMode dense_mode_from_wire(double v)
{
    if (!(v >= 0.0 && v <= static_cast<double>(DenseLineMode::Density)))
        return DenseLineMode::Auto;
    return static_cast<DenseLineMode>(static_cast<int>(v));
}

// SET_SERIES_DRAW_STYLE: f1 = DrawStyle, f2 = DenseLineMode.
inline DiffOp make_series_draw_style_op(uint64_t      figure_id,
                                        uint32_t      axes_index,
                                        uint32_t      series_index,
                                        DrawStyle     style,
                                        DenseLineMode mode)
{
    DiffOp op;
    op.type         = DiffOp::Type::SET_SERIES_DRAW_STYLE;
    op.figure_id    = figure_id;
    op.axes_index   = axes_index;
    op.series_index = series_index;
    op.f1           = draw_style_to_wire(style);
    op.f2           = dense_mode_to_wire(mode);
    return op;
}

// Store a SET_SERIES_DRAW_STYLE op's values in a snapshot series.
inline void apply_series_draw_style_op(SnapshotSeriesState& s, const DiffOp& op)
{
    s.draw_style = draw_style_to_wire(draw_style_from_wire(op.f1));
    s.dense_mode = dense_mode_to_wire(dense_mode_from_wire(op.f2));
}

// Apply snapshot draw-style values to a live line series.
inline void apply_series_draw_style(LineSeries& line, uint8_t draw_style, uint8_t dense_mode)
{
    const DrawStyle     ds = draw_style_from_wire(draw_style);
    const DenseLineMode dm = dense_mode_from_wire(dense_mode);
    if (line.draw_style() != ds)
        line.draw_style(ds);
    if (line.dense_mode() != dm)
        line.dense_mode(dm);
}

}   // namespace spectra::ipc
