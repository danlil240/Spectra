#pragma once

// Builds a live spectra::Figure from a cached SnapshotFigureState in the
// multiproc window agent (spectra-window).  Header-only so unit tests can
// check that the window places every series where the app put it.
//
// Which cached series land in which live axes slot must agree with
// locate_live_series() (live_series_index.hpp): both go through
// series_is_built().

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "../ipc/annotation_state.hpp"
#include "../ipc/message.hpp"
#include "live_series_index.hpp"
#include "ui/app/figure_snapshot.hpp"

namespace spectra::agent
{

// Upper bound on the grid dimensions accepted from a snapshot.
constexpr int32_t MAX_GRID_DIM = 1024;

// Apply a snapshot series colour.  An auto-coloured series (color_index >= 0)
// keeps following this window's data palette and a format-letter series
// (color_letter, e.g. "r--") this window's theme; otherwise the colour is explicit.
inline void apply_snapshot_color(Series& s, const ipc::SnapshotSeriesState& ss)
{
    if (ss.color_index >= 0)
        s.set_auto_color_index(ss.color_index);
    else if (format_colors::is_letter(static_cast<char>(ss.color_letter)))
        s.set_color_letter(static_cast<char>(ss.color_letter));
    else
        s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
}

// Resolve an axes slot on a live Figure. 2D axes live in axes(); 3D axes are
// created by subplot3d() and only exist in all_axes().
inline AxesBase* live_axes_at(Figure& fig, uint32_t index)
{
    if (index < fig.axes().size() && fig.axes()[index])
        return fig.axes_mut()[index].get();
    if (index < fig.all_axes().size() && fig.all_axes()[index])
        return fig.all_axes_mut()[index].get();
    return nullptr;
}

// Style shared by every series type.
inline void apply_snapshot_common(Series& s, const ipc::SnapshotSeriesState& ss)
{
    apply_snapshot_color(s, ss);
    s.visible(ss.visible);
    s.opacity(ss.opacity);
    if (!ss.name.empty())
        s.label(ss.name);
}

inline void add_3d_series(Axes3D& ax3d, const ipc::SnapshotSeriesState& ss)
{
    // Unpack XYZ stride-3 data
    std::vector<float> xs;
    std::vector<float> ys;
    std::vector<float> zs;
    for (size_t j = 0; j + 2 < ss.data.size(); j += 3)
    {
        xs.push_back(ss.data[j]);
        ys.push_back(ss.data[j + 1]);
        zs.push_back(ss.data[j + 2]);
    }

    if (ss.type == "scatter3d")
    {
        auto& s = ax3d.scatter3d(xs, ys, zs);
        apply_snapshot_common(s, ss);
        s.size(ss.marker_size);
    }
    else if (ss.type == "surface")
    {
        // SurfaceSeries expects 1D grid vectors (unique sorted X, Y)
        // plus a rows*cols Z array.  The IPC data is raveled meshgrid
        // (all X, all Y, all Z each of length rows*cols).
        // Reconstruct 1D grids by extracting unique sorted values.
        auto               near = [](float a, float b) { return std::abs(a - b) < 1e-6f; };
        std::vector<float> ux(xs.begin(), xs.end());
        std::vector<float> uy(ys.begin(), ys.end());
        std::sort(ux.begin(), ux.end());
        ux.erase(std::unique(ux.begin(), ux.end(), near), ux.end());
        std::sort(uy.begin(), uy.end());
        uy.erase(std::unique(uy.begin(), uy.end(), near), uy.end());

        // Reorder Z into row-major (y-row, x-col) order expected by SurfaceSeries
        size_t             ncols = ux.size();
        size_t             nrows = uy.size();
        std::vector<float> z_grid(nrows * ncols, 0.0f);
        for (size_t k = 0; k < xs.size(); ++k)
        {
            auto cit = std::lower_bound(ux.begin(), ux.end(), xs[k] - 1e-6f);
            auto ci  = static_cast<size_t>(std::distance(ux.begin(), cit));
            if (ci >= ncols)
                ci = ncols - 1;
            auto rit = std::lower_bound(uy.begin(), uy.end(), ys[k] - 1e-6f);
            auto ri  = static_cast<size_t>(std::distance(uy.begin(), rit));
            if (ri >= nrows)
                ri = nrows - 1;
            z_grid[ri * ncols + ci] = zs[k];
        }

        auto& s = ax3d.surface(ux, uy, z_grid);
        apply_snapshot_common(s, ss);
    }
    else   // "line3d", and "mesh" (vertices + indices are not sent; drawn as a line)
    {
        auto& s = ax3d.line3d(xs, ys, zs);
        apply_snapshot_common(s, ss);
        s.width(ss.line_width);
    }
}

inline void add_2d_series(Axes& ax, const ipc::SnapshotSeriesState& ss)
{
    if (is_stats_series_type(ss.type))
    {
        Series* s = add_stats_series_from_snapshot(ax, ss);
        if (!s)
        {
            // Malformed payload: keep the slot so later series still line up
            // with the figure-wide indices the daemon uses, but draw nothing.
            ax.line().visible(false);
            return;
        }
        s->x_offset(ss.x_origin).y_offset(ss.y_origin);
        apply_snapshot_common(*s, ss);
        return;
    }

    std::vector<float> xs;
    std::vector<float> ys;
    for (size_t j = 0; j + 1 < ss.data.size(); j += 2)
    {
        xs.push_back(ss.data[j]);
        ys.push_back(ss.data[j + 1]);
    }
    if (ss.type == "scatter")
    {
        auto& s = ax.scatter(xs, ys);
        s.x_offset(ss.x_origin).y_offset(ss.y_origin);
        apply_snapshot_common(s, ss);
        s.size(ss.marker_size);
    }
    else
    {
        auto& s = ax.line(xs, ys);
        s.x_offset(ss.x_origin).y_offset(ss.y_origin);
        apply_snapshot_common(s, ss);
        s.width(ss.line_width);
    }
}

// ─── Replace a live 2D axes' text annotations with a snapshot list ──────────
inline void sync_live_annotations(Axes& ax, const std::vector<ipc::SnapshotAnnotationState>& list)
{
    ax.clear_annotations();
    for (const auto& a : list)
        ipc::apply_annotation_state(ax.text(a.x, a.y, a.text), a);
}

// ─── Build a real Figure from a SnapshotFigureState ──────────────────────────
inline std::unique_ptr<Figure> build_figure_from_snapshot(const ipc::SnapshotFigureState& snap,
                                                          uint32_t override_width  = 0,
                                                          uint32_t override_height = 0)
{
    FigureConfig cfg;
    // Prefer an explicit override (current swapchain size on rebuild), then
    // the size the figure was created with, then the shared default.
    if (override_width > 0)
        cfg.width = override_width;
    else if (snap.width > 0)
        cfg.width = snap.width;
    if (override_height > 0)
        cfg.height = override_height;
    else if (snap.height > 0)
        cfg.height = snap.height;
    auto fig = std::make_unique<Figure>(cfg);

    int rows = std::clamp(snap.grid_rows, int32_t(1), MAX_GRID_DIM);
    int cols = std::clamp(snap.grid_cols, int32_t(1), MAX_GRID_DIM);

    const size_t num_axes = built_axes_count(snap);
    // Figure::subplot()/subplot3d() throw std::out_of_range when the index
    // exceeds rows*cols. The cached grid can lag behind the axes list (e.g. an
    // ADD_AXES from an older daemon without grid info), so grow it to fit.
    while (static_cast<size_t>(rows) * static_cast<size_t>(cols) < num_axes)
        ++rows;
    for (size_t i = 0; i < num_axes; ++i)
    {
        const auto axes_index = static_cast<uint32_t>(i);
        if (built_axes_is_3d(snap, axes_index))
        {
            auto&       ax3d = fig->subplot3d(rows, cols, static_cast<int>(i + 1));
            const auto& sa   = snap.axes[i];
            ax3d.xlim(sa.x_min, sa.x_max);
            ax3d.ylim(sa.y_min, sa.y_max);
            ax3d.zlim(sa.z_min, sa.z_max);
            ax3d.grid(sa.grid_visible);
            if (!sa.x_label.empty())
                ax3d.xlabel(sa.x_label);
            if (!sa.y_label.empty())
                ax3d.ylabel(sa.y_label);
            if (!sa.title.empty())
                ax3d.title(sa.title);
        }
        else
        {
            auto& ax = fig->subplot(rows, cols, static_cast<int>(i + 1));
            if (i < snap.axes.size())
            {
                const auto& sa = snap.axes[i];
                ax.xlim(sa.x_min, sa.x_max);
                ax.ylim(sa.y_min, sa.y_max);
                ax.grid(sa.grid_visible);
                if (!sa.x_label.empty())
                    ax.xlabel(sa.x_label);
                if (!sa.y_label.empty())
                    ax.ylabel(sa.y_label);
                if (!sa.title.empty())
                    ax.title(sa.title);
                sync_live_annotations(ax, sa.annotations);
            }
        }

        // Add the series that belong to this axes, in snapshot order.  A series
        // whose dimensionality does not match the axes is skipped here and by
        // locate_live_series() alike.
        for (const auto& ss : snap.series)
        {
            if (ss.axes_index != axes_index || !series_is_built(snap, ss))
                continue;
            if (auto* ax3d = dynamic_cast<Axes3D*>(live_axes_at(*fig, axes_index)))
                add_3d_series(*ax3d, ss);
            else if (auto* ax2d = dynamic_cast<Axes*>(live_axes_at(*fig, axes_index)))
                add_2d_series(*ax2d, ss);
        }
    }

    // Apply live streaming FPS from snapshot
    if (snap.live_fps > 0.0f)
    {
        fig->anim_.live_streaming = true;
        fig->anim_.fps            = snap.live_fps;
    }

    return fig;
}

}   // namespace spectra::agent
