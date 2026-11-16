#pragma once

// 3D series styles and 3D axes light over IPC (the "depth_shade",
// "show_edges", "contour_projection", "light_mode", "light_angles" and "fog"
// Python properties, the SnapshotSeriesState / SnapshotAxisState 3D fields
// and the SET_SERIES_STYLE_3D / SET_AXES_LIGHT_3D diff ops).  Header-only so
// the daemon, the window agent and the unit tests share one interpretation.

#include <spectra/axes3d.hpp>
#include <spectra/series3d.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "message.hpp"
#include "series_style_state.hpp"

namespace spectra::ipc
{

// Upper bound on the contour level count accepted from IPC.
inline constexpr int32_t MAX_CONTOUR_LEVELS = 1000;

// Snapshot / op values -> a live 3D series.  depth_shade: SERIES_STYLE_UNSET
// keeps the type's default; show_edges / contour_levels apply to surfaces.
inline void apply_series_style_3d(Series&                     s,
                                  uint8_t                     depth_shade,
                                  bool                        show_edges,
                                  int32_t                     contour_levels,
                                  const std::array<float, 4>& edge_color)
{
    if (depth_shade <= 1)
    {
        if (auto* l3 = dynamic_cast<LineSeries3D*>(&s))
            l3->depth_shade(depth_shade == 1);
        else if (auto* s3 = dynamic_cast<ScatterSeries3D*>(&s))
            s3->depth_shade(depth_shade == 1);
    }
    if (auto* surf = dynamic_cast<SurfaceSeries*>(&s))
    {
        if (surf->show_edges() != show_edges)
            surf->show_edges(show_edges);
        bool finite = true;
        for (float c : edge_color)
            finite = finite && std::isfinite(c);
        if (finite)
        {
            auto ch = [](float c) { return std::clamp(c, 0.0f, 1.0f); };
            surf->edge_color(
                Color{ch(edge_color[0]), ch(edge_color[1]), ch(edge_color[2]), ch(edge_color[3])});
        }
        const bool contours = contour_levels > 0;
        if (surf->contour_projection() != contours)
            surf->contour_projection(contours);
        if (contours && surf->contour_levels() != contour_levels)
            surf->contour_levels(std::min(contour_levels, MAX_CONTOUR_LEVELS));
    }
}

inline void apply_series_style_3d(Series& s, const SnapshotSeriesState& ss)
{
    apply_series_style_3d(s, ss.depth_shade, ss.show_edges, ss.contour_levels, ss.edge_color);
}

// SET_SERIES_STYLE_3D op fields -> snapshot fields.
inline void series_style_3d_from_op(const DiffOp& op, SnapshotSeriesState& ss)
{
    ss.depth_shade    = style_wire_from_double(op.f1, 1);
    ss.show_edges     = op.f2 >= 0.5;
    ss.contour_levels = (std::isfinite(op.f3) && op.f3 >= 1.0)
                            ? static_cast<int32_t>(std::min(op.f3, double{MAX_CONTOUR_LEVELS}))
                            : 0;
    if (op.data.size() == 4)
        std::copy(op.data.begin(), op.data.end(), ss.edge_color.begin());
}

// Snapshot / op values -> a live 3D axes.
inline void apply_axes_light_3d(Axes3D& ax, const SnapshotAxisState& sa)
{
    ax.light_mode(sa.light_mode == static_cast<uint8_t>(LightMode::Headlight) ? LightMode::Headlight
                                                                              : LightMode::Fixed);
    ax.fog(std::isfinite(sa.fog) ? sa.fog : 0.0f);
    if (sa.light_angles_set && std::isfinite(sa.light_azimuth) && std::isfinite(sa.light_elevation))
        ax.light_angles(sa.light_azimuth, sa.light_elevation);
}

// SET_AXES_LIGHT_3D op fields -> snapshot fields.
inline void axes_light_3d_from_op(const DiffOp& op, SnapshotAxisState& sa)
{
    sa.light_mode = op.f1 >= 0.5 ? static_cast<uint8_t>(LightMode::Headlight)
                                 : static_cast<uint8_t>(LightMode::Fixed);
    sa.fog        = std::isfinite(op.f2) ? static_cast<float>(std::clamp(op.f2, 0.0, 1.0)) : 0.0f;
    sa.light_angles_set = op.bool_val;
    sa.light_azimuth    = static_cast<float>(op.f3);
    sa.light_elevation  = static_cast<float>(op.f4);
}

// Snapshot fields -> SET_AXES_LIGHT_3D / SET_SERIES_STYLE_3D op fields.
inline void axes_light_3d_to_op(const SnapshotAxisState& sa, DiffOp& op)
{
    op.type     = DiffOp::Type::SET_AXES_LIGHT_3D;
    op.f1       = sa.light_mode;
    op.f2       = sa.fog;
    op.f3       = sa.light_azimuth;
    op.f4       = sa.light_elevation;
    op.bool_val = sa.light_angles_set;
}
inline void series_style_3d_to_op(const SnapshotSeriesState& ss, DiffOp& op)
{
    op.type = DiffOp::Type::SET_SERIES_STYLE_3D;
    op.f1   = ss.depth_shade;
    op.f2   = ss.show_edges ? 1.0 : 0.0;
    op.f3   = ss.contour_levels;
    op.data.assign(ss.edge_color.begin(), ss.edge_color.end());
}

}   // namespace spectra::ipc
