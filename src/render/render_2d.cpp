// render_2d.cpp — 2D series rendering (line, scatter, stat fill) and selection highlight.
// Split from renderer.cpp (MR-1) for focused module ownership.

#include "renderer.hpp"

#include <cmath>

#include <algorithm>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_shapes3d.hpp>
#include <spectra/series_stats.hpp>

#include "ui/viewmodel/figure_view_model.hpp"
#include "ui/viewmodel/series_view_model.hpp"
#include "ui/workspace/plugin_guard.hpp"

#include "render/scatter_marker.hpp"
#include "ui/theme/theme.hpp"

namespace spectra
{

LineDrawRange compute_line_draw_range(std::span<const float> x,
                                      size_t                 n,
                                      bool                   x_sorted,
                                      bool                   cull,
                                      float                  local_x_min,
                                      float                  local_x_max)
{
    // Never reference points beyond what both the x array and the GPU buffer hold.
    n = std::min(n, x.size());

    LineDrawRange r;
    r.pt_count  = static_cast<uint32_t>(n);
    r.seg_count = n > 1 ? static_cast<uint32_t>(n) - 1 : 0;

    if (!cull || !x_sorted || n <= 2 || std::isnan(local_x_min) || std::isnan(local_x_max))
        return r;

    const float* begin = x.data();
    const float* end   = begin + n;
    // Find first point >= x_min (with 1-point margin for segment connectivity)
    auto lo_idx = static_cast<size_t>(std::lower_bound(begin, end, local_x_min) - begin);
    if (lo_idx > 0)
        lo_idx--;   // include one segment before visible range

    // Find first point > x_max
    auto hi_idx = static_cast<size_t>(std::upper_bound(begin, end, local_x_max) - begin);
    if (hi_idx < n)
        hi_idx++;   // include one segment after visible range

    if (lo_idx < hi_idx && hi_idx <= n)
    {
        r.first_seg       = static_cast<uint32_t>(lo_idx);
        auto last_seg_end = static_cast<uint32_t>(hi_idx);
        if (last_seg_end > 0)
            last_seg_end--;   // segments = points - 1
        r.seg_count = (last_seg_end > r.first_seg) ? (last_seg_end - r.first_seg) : 0;

        r.first_pt = static_cast<uint32_t>(lo_idx);
        r.pt_count = static_cast<uint32_t>(hi_idx - lo_idx);
    }
    return r;
}

void Renderer::render_series(Series&             series,
                             const Rect&         viewport,
                             const VisibleRange* visible,
                             double              view_cx,
                             double              view_cy)
{
    auto it = series_gpu_data_.find(&series);
    if (it == series_gpu_data_.end())
        return;

    auto& gpu = it->second;
    // Custom plugin series keep their buffers in plugin-owned plugin_gpu_state
    // and never allocate the host ssbo; only built-in types need it here.
    if (!gpu.ssbo && gpu.type != SeriesType::Custom)
        return;

    SeriesPushConstants pc{};

    // Phase 2 (LT-5): read color/opacity via SeriesViewModel when available,
    // enabling per-view color/opacity overrides.
    Color eff_color;
    float eff_opacity = NAN;
    if (figure_vm_)
    {
        auto& svm   = figure_vm_->get_or_create_series_vm(&series);
        eff_color   = svm.effective_color();
        eff_opacity = svm.effective_opacity();
    }
    else
    {
        eff_color   = series.color();
        eff_opacity = series.opacity();
    }
    // 'k' / 'w' follow the theme foreground so they stay visible (see theme.hpp).
    eff_color   = ui::adapt_series_color_to_theme(eff_color, plot_colors());
    pc.color[0] = eff_color.r;
    pc.color[1] = eff_color.g;
    pc.color[2] = eff_color.b;
    pc.color[3] = eff_color.a * eff_opacity;

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
            // For unsorted data, fall back to drawing all segments.  Counts are
            // bounded by the points actually uploaded to the SSBO.
            // Visible range is in absolute coords; x_data is relative to the
            // series x_offset.  Convert in double (exact) then to float.
            const double xoff        = line->x_offset();
            const float  local_x_min = visible ? static_cast<float>(visible->x_min - xoff) : 0.0f;
            const float  local_x_max = visible ? static_cast<float>(visible->x_max - xoff) : 0.0f;
            const auto   range =
                compute_line_draw_range(line->x_data(),
                                        std::min(line->point_count(), gpu.uploaded_count),
                                        line->x_is_sorted(),
                                        visible != nullptr,
                                        local_x_min,
                                        local_x_max);
            const uint32_t first_seg = range.first_seg;
            const uint32_t seg_count = range.seg_count;
            const uint32_t first_pt  = range.first_pt;
            const uint32_t pt_count  = range.pt_count;

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
                // first_pt is the first *instance*: scatter.vert reads
                // points[gl_InstanceIndex] and uses gl_VertexIndex % 6 as the corner.
                backend_.draw_instanced(6, pt_count, 0, first_pt);
            }
            break;
        }
        case SeriesType::ChunkedLine2D:
        {
            auto* cl = static_cast<ChunkedLineSeries*>(&series);
            if (gpu.uploaded_count < 2)
                break;

            if (style.line_style != LineStyle::Solid && style.line_style != LineStyle::None)
            {
                DashPattern dp = get_dash_pattern(style.line_style, cl->width());
                for (int i = 0; i < dp.count && i < 8; ++i)
                    pc.dash_pattern[i] = dp.segments[i];
                pc.dash_total = dp.total;
                pc.dash_count = dp.count;
            }

            uint32_t seg_count = static_cast<uint32_t>(gpu.uploaded_count) - 1;
            auto     pt_count  = static_cast<uint32_t>(gpu.uploaded_count);

            if (style.has_line() && seg_count > 0)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = cl->width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw(seg_count * 6, 0);
            }
            if (style.has_marker() && pt_count > 0)
            {
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size = style.marker_size;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw_instanced(6, pt_count, 0);
            }
            break;
        }
        case SeriesType::Scatter2D:
        {
            auto* scatter = static_cast<ScatterSeries*>(&series);
            // Scalar-valued scatter uses a vec4 SSBO (x, y, value, pad) and a
            // dedicated vertex shader. The fragment shader maps the scalar on
            // the GPU so changing ranges never requires CPU color expansion.
            const bool use_colormap = scatter->has_colormap() && scatter_colormap_pipeline_
                                      && gpu.ssbo_stride_floats == 4;
            if (scatter->has_colormap() && !scatter_colormap_pipeline_)
                note_unsupported("colormapped scatter (drawn in one color)");
            if (use_colormap)
            {
                pc.dash_count      = static_cast<int>(scatter->colormap());
                pc.dash_pattern[0] = scatter->colormap_min();
                pc.dash_pattern[1] = scatter->colormap_max();
                pc._pad2[0]        = 1.0f;
            }
            backend_.bind_pipeline(use_colormap ? scatter_colormap_pipeline_ : scatter_pipeline_);
            pc.point_size = scatter->size();
            // No explicit marker style: filled dot with a theme-contrasting edge.
            const ScatterMarkerParams marker =
                resolve_scatter_marker(style.marker_style,
                                       use_colormap,
                                       plot_colors().bg_canvas.luminance());
            pc.marker_type = marker.marker_type;
            pc._pad2[1]    = marker.edge_mix;
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(scatter->point_count()));
            break;
        }
        case SeriesType::Line3D:
        {
            if (!require_pipeline(line3d_pipeline_, "3D line"))
                break;
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
            if (!require_pipeline(scatter3d_pipeline_, "3D scatter"))
                break;
            auto* scatter3d      = static_cast<ScatterSeries3D*>(&series);
            bool  is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
            backend_.bind_pipeline(is_transparent ? scatter3d_transparent_pipeline_
                                                  : scatter3d_pipeline_);
            pc.point_size = scatter3d->size();
            // Honour the series' marker style (filled, square, ...); without an
            // explicit style use the same default dot as 2D scatter.
            const ScatterMarkerParams marker =
                resolve_scatter_marker(scatter3d->marker_style(),
                                       false,
                                       plot_colors().bg_canvas.luminance());
            pc.marker_type = marker.marker_type;
            pc._pad2[1]    = marker.edge_mix;
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(scatter3d->point_count()));
            break;
        }
        case SeriesType::Surface3D:
        {
            if (!require_pipeline(surface3d_pipeline_, "3D surface"))
                break;
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
            if (!require_pipeline(mesh3d_pipeline_, "3D mesh"))
                break;
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
        case SeriesType::Band2D:
        {
            auto* band = static_cast<BandSeries*>(&series);
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= band->fill_opacity();
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            if (band->show_edges() && band->edge_width() > 0.0f && band->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = band->edge_width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                const uint32_t segments = static_cast<uint32_t>(band->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Stem2D:
        {
            auto* stem = static_cast<StemSeries*>(&series);
            // Draw stems as line segments
            if (stem->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width  = stem->stem_width();
                pc.marker_type = 0;   // suppress markers on line vertices (NaN breaks)
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(stem->point_count()) - 1;
                backend_.draw(segments * 6);
                // Draw head markers if enabled
                if (stem->show_heads())
                {
                    backend_.bind_pipeline(scatter_pipeline_);
                    pc.point_size  = style.marker_size > 0.0f ? style.marker_size : 6.0f;
                    pc.marker_type = static_cast<uint32_t>(MarkerStyle::FilledCircle);
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    // The SSBO holds 3 vertices per stem: (x, baseline), (x, y),
                    // (NaN, NaN).  The scatter shader reads points[gl_InstanceIndex],
                    // so draw one instance per stem with firstInstance = 3*i + 1
                    // to place each head on its tip vertex (first_vertex stays 0 so
                    // gl_VertexIndex % 6 still enumerates the quad corners).
                    const size_t stem_count = gpu.uploaded_count / 3;
                    for (size_t i = 0; i < stem_count; ++i)
                        backend_.draw_instanced(6, 1, 0, static_cast<uint32_t>(i * 3 + 1));
                }
            }
            break;
        }
        case SeriesType::Shape2D:
        {
            auto* sh = static_cast<ShapeSeries*>(&series);
            // Draw fill with per-vertex gradient alpha; arrowheads (the tail of the
            // fill buffer) are drawn opaque.
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                const auto total = static_cast<uint32_t>(gpu.fill_vertex_count);
                const auto solid_first =
                    std::min(total, static_cast<uint32_t>(sh->solid_fill_first_vertex()));
                backend_.bind_pipeline(stat_fill_pipeline_);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                if (solid_first > 0)
                {
                    SeriesPushConstants fill_pc = pc;
                    fill_pc.color[3] *= 0.50f;
                    backend_.push_constants(fill_pc);
                    backend_.draw(solid_first);
                }
                if (solid_first < total)
                {
                    backend_.push_constants(pc);
                    backend_.draw(total - solid_first, solid_first);
                }
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
            if (!require_pipeline(mesh3d_pipeline_, "3D shape"))
                break;
            auto* sh3d = static_cast<ShapeSeries3D*>(&series);
            if (gpu.index_buffer)
            {
                pc._pad2[0] = sh3d->ambient();
                pc._pad2[1] = sh3d->specular();
                if (sh3d->shininess() > 0.0f)
                {
                    pc.marker_size = sh3d->shininess();
                    pc.marker_type = 0;
                }

                // One indexed draw per colour range.  Ranges with the
                // zero-alpha sentinel keep the series colour; explicit
                // per-shape colours replace it.  Opaque ranges are drawn
                // before transparent ones so the latter blend over them.
                const auto     ranges    = sh3d->draw_ranges();
                const auto     idx_limit = static_cast<uint32_t>(gpu.index_count);
                PipelineHandle bound{};
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (const auto& r : ranges)
                    {
                        if (r.first_index >= idx_limit)
                            continue;
                        const uint32_t count = std::min(r.index_count, idx_limit - r.first_index);
                        SeriesPushConstants range_pc = pc;
                        if (!r.inherits_series_color())
                        {
                            range_pc.color[0] = r.color.r;
                            range_pc.color[1] = r.color.g;
                            range_pc.color[2] = r.color.b;
                            range_pc.color[3] = r.color.a;
                        }
                        const bool transparent = (range_pc.color[3] * range_pc.opacity) < 0.99f;
                        if (transparent != (pass == 1))
                            continue;
                        const PipelineHandle pipe =
                            transparent ? mesh3d_transparent_pipeline_ : mesh3d_pipeline_;
                        if (pipe.id != bound.id)
                        {
                            backend_.bind_pipeline(pipe);
                            backend_.bind_buffer(gpu.ssbo, 0);
                            backend_.bind_index_buffer(gpu.index_buffer);
                            bound = pipe;
                        }
                        backend_.push_constants(range_pc);
                        backend_.draw_indexed(count, r.first_index, 0);
                    }
                }
            }
            break;
        }
        case SeriesType::Custom:
        {
            if (series_type_registry_)
            {
                auto entry = series_type_registry_->find_shared(gpu.custom_type_name);
                // Types registered after Renderer::init() (i.e. by plugins) have
                // no pipeline yet: create it lazily here on the render thread.
                if (entry && !entry->pipeline && !entry->pipeline_failed)
                    series_type_registry_->create_pipelines(backend_);
                if (entry && entry->draw_fn && entry->pipeline && !entry->faulted)
                {
                    float viewport_xywh[4] = {viewport.x, viewport.y, viewport.w, viewport.h};
                    auto  result           = plugin_guard_invoke(entry->type_name.c_str(),
                                                      [&]() {
                                                          entry->draw_fn(backend_,
                                                                         entry->pipeline,
                                                                         gpu.plugin_gpu_state,
                                                                         viewport_xywh,
                                                                         pc);
                                                      });
                    if (result != PluginCallResult::Success)
                    {
                        entry->faulted = true;
                    }
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

    const auto& accent = plot_colors().accent;

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

                // Apply visible-range culling (same as normal render path):
                // same cached per-frame limits, converted to series-local x.
                const bool is_2d       = dynamic_cast<Axes*>(&axes) != nullptr;
                const bool cull        = is_2d && agpu_it != axes_gpu_data_.end();
                float      local_x_min = 0.0f;
                float      local_x_max = 0.0f;
                if (cull)
                {
                    const double xoff = line->x_offset();
                    local_x_min       = static_cast<float>(agpu_it->second.cached_xlim_min - xoff);
                    local_x_max       = static_cast<float>(agpu_it->second.cached_xlim_max - xoff);
                }
                const auto range =
                    compute_line_draw_range(line->x_data(),
                                            std::min(line->point_count(), gpu.uploaded_count),
                                            line->x_is_sorted(),
                                            cull,
                                            local_x_min,
                                            local_x_max);
                const uint32_t first_seg = range.first_seg;
                const uint32_t seg_count = range.seg_count;
                const uint32_t first_pt  = range.first_pt;
                const uint32_t pt_count  = range.pt_count;

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
                    backend_.draw_instanced(6, pt_count, 0, first_pt);
                }
                break;
            }
            case SeriesType::ChunkedLine2D:
            {
                if (gpu.uploaded_count < 2)
                    break;

                uint32_t seg_count = static_cast<uint32_t>(gpu.uploaded_count) - 1;
                auto     pt_count  = static_cast<uint32_t>(gpu.uploaded_count);

                if (seg_count > 0)
                {
                    backend_.bind_pipeline(line_pipeline_);
                    pc.line_width = 1.5f;
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.draw(seg_count * 6, 0);
                }
                if (pt_count > 0)
                {
                    backend_.bind_pipeline(scatter_pipeline_);
                    pc.point_size  = 5.0f;
                    pc.marker_type = static_cast<uint32_t>(MarkerStyle::FilledCircle);
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.draw_instanced(6, pt_count, 0);
                }
                break;
            }
            case SeriesType::Scatter2D:
            {
                auto* scatter = static_cast<const ScatterSeries*>(sel);
                if (scatter->point_count() == 0)
                    break;
                backend_.bind_pipeline(gpu.ssbo_stride_floats == 4 ? scatter_colormap_pipeline_
                                                                   : scatter_pipeline_);
                pc.point_size  = scatter->size() + 3.0f;
                pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
                pc.dash_count  = 0;
                pc._pad2[0]    = 0.0f;
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
