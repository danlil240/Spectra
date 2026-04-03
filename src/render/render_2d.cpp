// render_2d.cpp — 2D series rendering (line, scatter, stat fill) and selection highlight.
// Split from renderer.cpp (MR-1) for focused module ownership.

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_shapes3d.hpp>
#include <spectra/series_stats.hpp>

#include "ui/theme/theme.hpp"

namespace spectra
{

void Renderer::render_series(Series& series,
                             const Rect& /*viewport*/,
                             const VisibleRange* visible,
                             double              view_cx,
                             double              view_cy)
{
    auto it = series_gpu_data_.find(&series);
    if (it == series_gpu_data_.end())
        return;

    auto& gpu = it->second;
    if (!gpu.ssbo)
        return;

    SeriesPushConstants pc{};
    const auto&         c = series.color();
    pc.color[0]           = c.r;
    pc.color[1]           = c.g;
    pc.color[2]           = c.b;
    pc.color[3]           = c.a * series.opacity();

    const auto& style = series.plot_style();
    pc.line_style     = static_cast<uint32_t>(style.line_style);
    pc.marker_type    = static_cast<uint32_t>(style.marker_style);
    pc.marker_size    = style.marker_size;
    pc.opacity        = style.opacity;

    // Camera-relative data_offset: bridges the gap between the series'
    // upload origin and the current view center.  Both origin and view_cx/cy
    // are double-precision, so the subtraction is exact; the result is small
    // (within the drift threshold) and converts to float without loss.
    pc.data_offset_x = static_cast<float>(gpu.origin_x - view_cx);
    pc.data_offset_y = static_cast<float>(gpu.origin_y - view_cy);

    // Use cached SeriesType to avoid 6x dynamic_cast per series per frame.
    // Only perform the single targeted static_cast for the known type.
    switch (gpu.type)
    {
        case SeriesType::Line2D:
        {
            auto* line = static_cast<LineSeries*>(&series);
            if (style.line_style != LineStyle::Solid && style.line_style != LineStyle::None)
            {
                DashPattern dp = get_dash_pattern(style.line_style, line->width());
                for (int i = 0; i < dp.count && i < 8; ++i)
                    pc.dash_pattern[i] = dp.segments[i];
                pc.dash_total = dp.total;
                pc.dash_count = dp.count;
            }

            // Compute visible segment range via binary search on sorted x_data.
            // For unsorted data, fall back to drawing all segments.
            uint32_t first_seg = 0;
            uint32_t seg_count =
                line->point_count() > 1 ? static_cast<uint32_t>(line->point_count()) - 1 : 0;
            uint32_t first_pt = 0;
            uint32_t pt_count = static_cast<uint32_t>(line->point_count());

            if (visible && line->point_count() > 2)
            {
                const auto& xd = line->x_data();
                size_t      n  = xd.size();
                // Quick check: is x_data sorted? (sample a few points)
                bool sorted = (n < 2)
                              || (xd[0] <= xd[n / 4] && xd[n / 4] <= xd[n / 2]
                                  && xd[n / 2] <= xd[3 * n / 4] && xd[3 * n / 4] <= xd[n - 1]);
                if (sorted)
                {
                    const float* begin = xd.data();
                    const float* end   = begin + n;
                    // Find first point >= x_min (with 1-point margin for segment connectivity)
                    auto   lo     = std::lower_bound(begin, end, visible->x_min);
                    size_t lo_idx = static_cast<size_t>(lo - begin);
                    if (lo_idx > 0)
                        lo_idx--;   // include one segment before visible range

                    // Find first point > x_max
                    auto   hi     = std::upper_bound(begin, end, visible->x_max);
                    size_t hi_idx = static_cast<size_t>(hi - begin);
                    if (hi_idx < n)
                        hi_idx++;   // include one segment after visible range

                    if (lo_idx < hi_idx && hi_idx <= n)
                    {
                        first_seg             = static_cast<uint32_t>(lo_idx);
                        uint32_t last_seg_end = static_cast<uint32_t>(hi_idx);
                        if (last_seg_end > 0)
                            last_seg_end--;   // segments = points - 1
                        seg_count = (last_seg_end > first_seg) ? (last_seg_end - first_seg) : 0;

                        first_pt = static_cast<uint32_t>(lo_idx);
                        pt_count = static_cast<uint32_t>(hi_idx - lo_idx);
                    }
                }
            }

            if (style.has_line() && seg_count > 0)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = line->width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw(seg_count * 6, first_seg * 6);
            }
            if (style.has_marker() && pt_count > 0)
            {
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size = style.marker_size;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw_instanced(6, pt_count, first_pt);
            }
            break;
        }
        case SeriesType::Scatter2D:
        {
            auto* scatter = static_cast<ScatterSeries*>(&series);
            backend_.bind_pipeline(scatter_pipeline_);
            pc.point_size  = scatter->size();
            pc.marker_type = static_cast<uint32_t>(style.marker_style);
            if (pc.marker_type == 0)
            {
                const auto& theme_colors = theme_mgr_.colors();
                float       bg_luma      = 0.2126f * theme_colors.bg_canvas.r
                                + 0.7152f * theme_colors.bg_canvas.g
                                + 0.0722f * theme_colors.bg_canvas.b;
                pc.marker_type = static_cast<uint32_t>(bg_luma > 0.80f ? MarkerStyle::FilledCircle
                                                                       : MarkerStyle::Circle);
            }
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(scatter->point_count()));
            break;
        }
        case SeriesType::Line3D:
        {
            auto* line3d = static_cast<LineSeries3D*>(&series);
            if (line3d->point_count() > 1)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                backend_.bind_pipeline(is_transparent ? line3d_transparent_pipeline_
                                                      : line3d_pipeline_);
                pc.line_width = line3d->width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(line3d->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Scatter3D:
        {
            auto* scatter3d      = static_cast<ScatterSeries3D*>(&series);
            bool  is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
            backend_.bind_pipeline(is_transparent ? scatter3d_transparent_pipeline_
                                                  : scatter3d_pipeline_);
            pc.point_size  = scatter3d->size();
            pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(scatter3d->point_count()));
            break;
        }
        case SeriesType::Surface3D:
        {
            auto* surface = static_cast<SurfaceSeries*>(&series);
            if (gpu.index_buffer)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                if (surface->wireframe())
                {
                    if (!surface->is_wireframe_mesh_generated())
                        return;
                    backend_.bind_pipeline(is_transparent
                                               ? surface_wireframe3d_transparent_pipeline_
                                               : surface_wireframe3d_pipeline_);
                    pc._pad2[0] = surface->ambient();
                    pc._pad2[1] = surface->specular();
                    if (surface->shininess() > 0.0f)
                    {
                        pc.marker_size = surface->shininess();
                        pc.marker_type = 0;
                    }
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.bind_index_buffer(gpu.index_buffer);
                    backend_.draw_indexed(
                        static_cast<uint32_t>(surface->wireframe_mesh().indices.size()));
                }
                else
                {
                    if (!surface->is_mesh_generated())
                        return;
                    const auto& surf_mesh = surface->mesh();
                    backend_.bind_pipeline(is_transparent ? surface3d_transparent_pipeline_
                                                          : surface3d_pipeline_);
                    pc._pad2[0] = surface->ambient();
                    pc._pad2[1] = surface->specular();
                    if (surface->shininess() > 0.0f)
                    {
                        pc.marker_size = surface->shininess();
                        pc.marker_type = 0;
                    }
                    // Encode colormap in push constants for fragment shader:
                    // dash_count = colormap type (1=Viridis..7=Grayscale, 0=None)
                    // dash_pattern[0..1] = model-space Z range
                    auto cm = surface->colormap_type();
                    if (cm != ColormapType::None)
                    {
                        pc.dash_count      = static_cast<int>(cm);
                        pc.dash_pattern[0] = -3.0f;   // box_half_size (model-space Z min)
                        pc.dash_pattern[1] = 3.0f;    // box_half_size (model-space Z max)
                    }
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.bind_index_buffer(gpu.index_buffer);
                    backend_.draw_indexed(static_cast<uint32_t>(surf_mesh.indices.size()));
                }
            }
            break;
        }
        case SeriesType::Mesh3D:
        {
            auto* mesh = static_cast<MeshSeries*>(&series);
            if (gpu.index_buffer)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                backend_.bind_pipeline(is_transparent ? mesh3d_transparent_pipeline_
                                                      : mesh3d_pipeline_);
                pc._pad2[0] = mesh->ambient();
                pc._pad2[1] = mesh->specular();
                if (mesh->shininess() > 0.0f)
                {
                    pc.marker_size = mesh->shininess();
                    pc.marker_type = 0;
                }
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.bind_index_buffer(gpu.index_buffer);
                backend_.draw_indexed(static_cast<uint32_t>(mesh->indices().size()));
            }
            break;
        }
        case SeriesType::BoxPlot2D:
        {
            auto* bp = static_cast<BoxPlotSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.45f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (bp->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(bp->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            // Render outliers as scatter points (using persistent buffer from upload)
            if (gpu.outlier_buffer && gpu.outlier_count > 0)
            {
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size  = 5.0f;
                pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.outlier_buffer, 0);
                backend_.draw_instanced(6, static_cast<uint32_t>(gpu.outlier_count));
            }
            break;
        }
        case SeriesType::Violin2D:
        {
            auto* vn = static_cast<ViolinSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.40f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (vn->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(vn->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Histogram2D:
        {
            auto* hist = static_cast<HistogramSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.65f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (hist->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.0f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(hist->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Bar2D:
        {
            auto* bs = static_cast<BarSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.75f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (bs->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(bs->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Shape2D:
        {
            auto* sh = static_cast<ShapeSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.50f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (sh->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(sh->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Shape3D:
        {
            auto* sh3d = static_cast<ShapeSeries3D*>(&series);
            if (gpu.index_buffer)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                backend_.bind_pipeline(is_transparent ? mesh3d_transparent_pipeline_
                                                      : mesh3d_pipeline_);
                pc._pad2[0] = sh3d->ambient();
                pc._pad2[1] = sh3d->specular();
                if (sh3d->shininess() > 0.0f)
                {
                    pc.marker_size = sh3d->shininess();
                    pc.marker_type = 0;
                }
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.bind_index_buffer(gpu.index_buffer);
                backend_.draw_indexed(static_cast<uint32_t>(gpu.index_count));
            }
            break;
        }
        case SeriesType::Custom:
        {
            if (series_type_registry_)
            {
                const auto* entry = series_type_registry_->find(gpu.custom_type_name);
                if (entry && entry->draw_fn && entry->pipeline)
                {
                    float viewport_xywh[4] = {0, 0, 0, 0};   // Set by caller via set_viewport
                    entry->draw_fn(backend_,
                                   entry->pipeline,
                                   gpu.plugin_gpu_state,
                                   viewport_xywh,
                                   pc);
                }
            }
            break;
        }
        case SeriesType::Unknown:
            break;
    }
}

void Renderer::render_selection_highlight(AxesBase& axes, const Rect& /*viewport*/)
{
    if (selected_series_.empty())
        return;

    const auto& accent = theme_mgr_.colors().accent;

    for (const auto* sel : selected_series_)
    {
        if (!sel)
            continue;

        // Check this series belongs to this axes
        bool found = false;
        for (const auto& sp : axes.series())
        {
            if (sp.get() == sel)
            {
                found = true;
                break;
            }
        }
        if (!found)
            continue;

        auto it = series_gpu_data_.find(sel);
        if (it == series_gpu_data_.end() || !it->second.ssbo)
            continue;

        auto& gpu = it->second;

        SeriesPushConstants pc{};
        pc.color[0] = accent.r;
        pc.color[1] = accent.g;
        pc.color[2] = accent.b;
        pc.color[3] = 0.85f;
        pc.opacity  = 1.0f;

        // Camera-relative data_offset for 2D selection highlight
        auto agpu_it = axes_gpu_data_.find(&axes);
        if (agpu_it != axes_gpu_data_.end())
        {
            pc.data_offset_x = static_cast<float>(gpu.origin_x - agpu_it->second.view_center_x);
            pc.data_offset_y = static_cast<float>(gpu.origin_y - agpu_it->second.view_center_y);
        }

        switch (gpu.type)
        {
            case SeriesType::Line2D:
            {
                auto* line = static_cast<const LineSeries*>(sel);
                if (line->point_count() < 2)
                    break;

                uint32_t first_seg = 0;
                uint32_t seg_count = static_cast<uint32_t>(line->point_count()) - 1;
                uint32_t first_pt  = 0;
                uint32_t pt_count  = static_cast<uint32_t>(line->point_count());

                // Apply visible-range culling (same as normal render path)
                if (auto* axes2d = dynamic_cast<Axes*>(&axes))
                {
                    auto xlim = axes2d->x_limits();
                    if (line->point_count() > 256)
                    {
                        const auto& xd = line->x_data();
                        size_t      n  = xd.size();
                        bool        sorted =
                            (n < 2)
                            || (xd[0] <= xd[n / 4] && xd[n / 4] <= xd[n / 2]
                                && xd[n / 2] <= xd[3 * n / 4] && xd[3 * n / 4] <= xd[n - 1]);
                        if (sorted)
                        {
                            const float* begin  = xd.data();
                            const float* end    = begin + n;
                            auto         lo     = std::lower_bound(begin, end, xlim.min);
                            size_t       lo_idx = static_cast<size_t>(lo - begin);
                            if (lo_idx > 0)
                                lo_idx--;
                            auto   hi     = std::upper_bound(begin, end, xlim.max);
                            size_t hi_idx = static_cast<size_t>(hi - begin);
                            if (hi_idx < n)
                                hi_idx++;
                            if (lo_idx < hi_idx && hi_idx <= n)
                            {
                                first_seg             = static_cast<uint32_t>(lo_idx);
                                uint32_t last_seg_end = static_cast<uint32_t>(hi_idx);
                                if (last_seg_end > 0)
                                    last_seg_end--;
                                seg_count =
                                    (last_seg_end > first_seg) ? (last_seg_end - first_seg) : 0;
                                first_pt = static_cast<uint32_t>(lo_idx);
                                pt_count = static_cast<uint32_t>(hi_idx - lo_idx);
                            }
                        }
                    }
                }

                if (seg_count > 0)
                {
                    backend_.bind_pipeline(line_pipeline_);
                    pc.line_width = 1.5f;
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.draw(seg_count * 6, first_seg * 6);
                }
                if (pt_count > 0)
                {
                    backend_.bind_pipeline(scatter_pipeline_);
                    pc.point_size  = 5.0f;
                    pc.marker_type = static_cast<uint32_t>(MarkerStyle::FilledCircle);
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.draw_instanced(6, pt_count, first_pt);
                }
                break;
            }
            case SeriesType::Scatter2D:
            {
                auto* scatter = static_cast<const ScatterSeries*>(sel);
                if (scatter->point_count() == 0)
                    break;
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size  = scatter->size() + 3.0f;
                pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw_instanced(6, static_cast<uint32_t>(scatter->point_count()));
                break;
            }
            case SeriesType::Line3D:
            {
                auto* line3d = static_cast<const LineSeries3D*>(sel);
                if (line3d->point_count() < 2)
                    break;
                backend_.bind_pipeline(line3d_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(line3d->point_count()) - 1;
                backend_.draw(segments * 6);

                if (line3d->point_count() > 0)
                {
                    backend_.bind_pipeline(scatter3d_pipeline_);
                    pc.point_size  = 5.0f;
                    pc.marker_type = static_cast<uint32_t>(MarkerStyle::FilledCircle);
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.draw_instanced(6, static_cast<uint32_t>(line3d->point_count()));
                }
                break;
            }
            case SeriesType::Scatter3D:
            {
                auto* scatter3d = static_cast<const ScatterSeries3D*>(sel);
                if (scatter3d->point_count() == 0)
                    break;
                backend_.bind_pipeline(scatter3d_pipeline_);
                pc.point_size  = scatter3d->size() + 3.0f;
                pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw_instanced(6, static_cast<uint32_t>(scatter3d->point_count()));
                break;
            }
            default:
                break;
        }
    }
}

}   // namespace spectra
