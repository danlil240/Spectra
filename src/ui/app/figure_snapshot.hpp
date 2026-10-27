// figure_snapshot.hpp — Serialize a Figure into an IPC SnapshotFigureState.
//
// Used by the multiproc runtime (app_multiproc.cpp) to push figures to the
// backend; header-only so unit tests can exercise it directly.

#pragma once

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "ipc/message.hpp"

namespace spectra
{

// ─── Serialize a Figure into a SnapshotFigureState ──────────────────────────
inline ipc::SnapshotFigureState figure_to_snapshot(const Figure& fig, uint64_t figure_id)
{
    ipc::SnapshotFigureState snap;
    snap.figure_id = figure_id;
    snap.title     = "";
    snap.width     = fig.width();
    snap.height    = fig.height();
    snap.grid_rows = fig.grid_rows();
    snap.grid_cols = fig.grid_cols();

    for (const auto& ax_ptr : fig.axes())
    {
        if (!ax_ptr)
            continue;
        const auto&            ax = *ax_ptr;
        ipc::SnapshotAxisState sa;
        sa.x_min        = ax.x_limits().min;
        sa.x_max        = ax.x_limits().max;
        sa.y_min        = ax.y_limits().min;
        sa.y_max        = ax.y_limits().max;
        sa.grid_visible = ax.grid_enabled();
        sa.x_label      = ax.xlabel();
        sa.y_label      = ax.ylabel();
        sa.title        = ax.title();
        snap.axes.push_back(std::move(sa));

        for (const auto& s_ptr : ax.series())
        {
            if (!s_ptr)
                continue;
            const auto&              s = *s_ptr;
            ipc::SnapshotSeriesState ss;
            ss.name         = s.label();
            ss.color_r      = s.color().r;
            ss.color_g      = s.color().g;
            ss.color_b      = s.color().b;
            ss.color_a      = s.color().a;
            ss.color_index  = s.auto_color_index();   // window resolves from its palette
            ss.color_letter = static_cast<uint8_t>(s.color_letter());   // ... or letter shade
            ss.visible      = s.visible();
            ss.opacity      = s.opacity();
            // x_data()/y_data() are relative to these double origins when the
            // series was fed double data with large values (e.g. timestamps).
            ss.x_origin = s.x_offset();
            ss.y_origin = s.y_offset();

            if (auto* line = dynamic_cast<const LineSeries*>(&s))
            {
                ss.type        = "line";
                ss.line_width  = line->width();
                ss.marker_size = s.marker_size();
                auto xd        = line->x_data();
                auto yd        = line->y_data();
                ss.data.reserve(xd.size() * 2);
                for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                {
                    ss.data.push_back(xd[i]);
                    ss.data.push_back(yd[i]);
                }
                ss.point_count = static_cast<uint32_t>(xd.size());
            }
            else if (auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
            {
                ss.type        = "scatter";
                ss.marker_size = scatter->size();
                ss.line_width  = 2.0f;
                auto xd        = scatter->x_data();
                auto yd        = scatter->y_data();
                ss.data.reserve(xd.size() * 2);
                for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                {
                    ss.data.push_back(xd[i]);
                    ss.data.push_back(yd[i]);
                }
                ss.point_count = static_cast<uint32_t>(xd.size());
            }

            snap.series.push_back(std::move(ss));
        }
    }

    return snap;
}

}   // namespace spectra
