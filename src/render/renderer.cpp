#include "renderer.hpp"

#include <algorithm>
#include <cstring>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <vector>

#include "../ui/axes3d_renderer.hpp"
#include "../ui/theme.hpp"

namespace spectra
{

Renderer::Renderer(Backend& backend) : backend_(backend) {}

void Renderer::notify_series_removed(const Series* series)
{
    auto it = series_gpu_data_.find(series);
    if (it != series_gpu_data_.end())
    {
        // Move GPU resources into the current ring slot.  They will be
        // destroyed DELETION_RING_SIZE frames later, after the GPU has
        // finished all command buffers that might reference them.
        deletion_ring_[deletion_ring_write_].push_back(std::move(it->second));
        series_gpu_data_.erase(it);
    }
}

void Renderer::flush_pending_deletions()
{
    // Destroy the oldest slot — these resources were queued DELETION_RING_SIZE
    // frames ago, so the GPU is guaranteed to be done with them.
    uint32_t destroy_slot = (deletion_ring_write_ + 1) % DELETION_RING_SIZE;
    auto& slot = deletion_ring_[destroy_slot];
    for (auto& gpu : slot)
    {
        if (gpu.ssbo)
            backend_.destroy_buffer(gpu.ssbo);
        if (gpu.index_buffer)
            backend_.destroy_buffer(gpu.index_buffer);
    }
    slot.clear();

    // Advance write pointer to the slot we just freed.
    deletion_ring_write_ = destroy_slot;
}

Renderer::~Renderer()
{
    // Wait for GPU to finish using all resources before destroying them
    backend_.wait_idle();

    // Flush ALL deferred deletion ring slots
    for (auto& slot : deletion_ring_)
    {
        for (auto& gpu : slot)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
        }
        slot.clear();
    }

    // Clean up per-series GPU data
    for (auto& [ptr, data] : series_gpu_data_)
    {
        if (data.ssbo)
        {
            backend_.destroy_buffer(data.ssbo);
        }
        if (data.index_buffer)
        {
            backend_.destroy_buffer(data.index_buffer);
        }
    }
    series_gpu_data_.clear();

    // Clean up per-axes GPU data (grid + border + bbox + tick buffers)
    for (auto& [ptr, data] : axes_gpu_data_)
    {
        if (data.grid_buffer)
            backend_.destroy_buffer(data.grid_buffer);
        if (data.border_buffer)
            backend_.destroy_buffer(data.border_buffer);
        if (data.bbox_buffer)
            backend_.destroy_buffer(data.bbox_buffer);
        if (data.tick_buffer)
            backend_.destroy_buffer(data.tick_buffer);
    }
    axes_gpu_data_.clear();

    if (frame_ubo_buffer_)
    {
        backend_.destroy_buffer(frame_ubo_buffer_);
    }
}

bool Renderer::init()
{
    // Create pipelines for each series type
    line_pipeline_ = backend_.create_pipeline(PipelineType::Line);
    scatter_pipeline_ = backend_.create_pipeline(PipelineType::Scatter);
    grid_pipeline_ = backend_.create_pipeline(PipelineType::Grid);

    // Create 3D pipelines
    line3d_pipeline_ = backend_.create_pipeline(PipelineType::Line3D);
    scatter3d_pipeline_ = backend_.create_pipeline(PipelineType::Scatter3D);
    mesh3d_pipeline_ = backend_.create_pipeline(PipelineType::Mesh3D);
    surface3d_pipeline_ = backend_.create_pipeline(PipelineType::Surface3D);
    grid3d_pipeline_ = backend_.create_pipeline(PipelineType::Grid3D);
    grid_overlay3d_pipeline_ = backend_.create_pipeline(PipelineType::GridOverlay3D);

    // Create wireframe 3D pipelines (line topology)
    surface_wireframe3d_pipeline_ = backend_.create_pipeline(PipelineType::SurfaceWireframe3D);
    surface_wireframe3d_transparent_pipeline_ =
        backend_.create_pipeline(PipelineType::SurfaceWireframe3D_Transparent);

    // Create transparent 3D pipelines (depth test ON, depth write OFF)
    line3d_transparent_pipeline_ = backend_.create_pipeline(PipelineType::Line3D_Transparent);
    scatter3d_transparent_pipeline_ = backend_.create_pipeline(PipelineType::Scatter3D_Transparent);
    mesh3d_transparent_pipeline_ = backend_.create_pipeline(PipelineType::Mesh3D_Transparent);
    surface3d_transparent_pipeline_ = backend_.create_pipeline(PipelineType::Surface3D_Transparent);

    // Create frame UBO buffer
    frame_ubo_buffer_ = backend_.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));

    return true;
}

void Renderer::begin_render_pass()
{
    // NOTE: flush_pending_deletions() is called from App::run() right after
    // begin_frame() succeeds, NOT here.  This ensures the fence wait has
    // completed before any GPU resources are freed.

    const auto& theme_colors = ui::ThemeManager::instance().colors();
    Color bg_color = Color(theme_colors.bg_primary.r,
                           theme_colors.bg_primary.g,
                           theme_colors.bg_primary.b,
                           theme_colors.bg_primary.a);
    backend_.begin_render_pass(bg_color);
    backend_.set_line_width(1.0f);  // Set default for VK_DYNAMIC_STATE_LINE_WIDTH
}

void Renderer::render_figure_content(Figure& figure)
{
    uint32_t w = figure.width();
    uint32_t h = figure.height();

    // Set full-figure viewport and scissor
    backend_.set_viewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    backend_.set_scissor(0, 0, w, h);

    // Wire up deferred-deletion callback on every axes so that
    // clear_series() / remove_series() safely defer GPU cleanup.
    auto removal_cb = [this](const Series* s) { notify_series_removed(s); };

    // Render each 2D axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto& ax = *axes_ptr;
        ax.set_series_removed_callback(removal_cb);
        const auto& vp = ax.viewport();

        render_axes(ax, vp, w, h);
    }

    // Render each 3D axes (stored in all_axes_)
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto& ax = *axes_ptr;
        ax.set_series_removed_callback(removal_cb);
        const auto& vp = ax.viewport();

        render_axes(ax, vp, w, h);
    }

    // Legend and text labels are now rendered by ImGui (see imgui_integration.cpp)
}

void Renderer::end_render_pass()
{
    backend_.end_render_pass();
}

void Renderer::render_figure(Figure& figure)
{
    // Convenience: starts render pass, draws content, ends render pass.
    // Use begin_render_pass / render_figure_content / end_render_pass
    // separately when ImGui or other overlays need to render inside the
    // same render pass.
    begin_render_pass();
    render_figure_content(figure);
    end_render_pass();
}

void Renderer::upload_series_data(Series& series)
{
    // Try 2D series first
    auto* line = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);

    // Try 3D series
    auto* line3d = dynamic_cast<LineSeries3D*>(&series);
    auto* scatter3d = dynamic_cast<ScatterSeries3D*>(&series);
    auto* surface = dynamic_cast<SurfaceSeries*>(&series);
    auto* mesh = dynamic_cast<MeshSeries*>(&series);

    auto& gpu = series_gpu_data_[&series];

    // Tag series type on first encounter (avoids 6x dynamic_cast in render_series)
    if (gpu.type == SeriesType::Unknown)
    {
        if (line)
            gpu.type = SeriesType::Line2D;
        else if (scatter)
            gpu.type = SeriesType::Scatter2D;
        else if (line3d)
            gpu.type = SeriesType::Line3D;
        else if (scatter3d)
            gpu.type = SeriesType::Scatter3D;
        else if (surface)
            gpu.type = SeriesType::Surface3D;
        else if (mesh)
            gpu.type = SeriesType::Mesh3D;
    }

    // Handle 2D line/scatter (vec2 interleaved)
    if (line || scatter)
    {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t count = 0;

        if (line)
        {
            x_data = line->x_data().data();
            y_data = line->y_data().data();
            count = line->point_count();
        }
        else if (scatter)
        {
            x_data = scatter->x_data().data();
            y_data = scatter->y_data().data();
            count = scatter->point_count();
        }

        if (count == 0)
            return;

        size_t byte_size = count * 2 * sizeof(float);
        if (!gpu.ssbo || gpu.uploaded_count < count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_size = byte_size * 2;
            gpu.ssbo = backend_.create_buffer(BufferUsage::Storage, alloc_size);
        }

        size_t floats_needed = count * 2;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);
        for (size_t i = 0; i < count; ++i)
        {
            upload_scratch_[i * 2] = x_data[i];
            upload_scratch_[i * 2 + 1] = y_data[i];
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count = count;
        series.clear_dirty();
    }
    // Handle 3D line/scatter (vec4 interleaved: x,y,z,pad)
    else if (line3d || scatter3d)
    {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        const float* z_data = nullptr;
        size_t count = 0;

        if (line3d)
        {
            x_data = line3d->x_data().data();
            y_data = line3d->y_data().data();
            z_data = line3d->z_data().data();
            count = line3d->point_count();
        }
        else if (scatter3d)
        {
            x_data = scatter3d->x_data().data();
            y_data = scatter3d->y_data().data();
            z_data = scatter3d->z_data().data();
            count = scatter3d->point_count();
        }

        if (count == 0)
            return;

        size_t byte_size = count * 4 * sizeof(float);
        if (!gpu.ssbo || gpu.uploaded_count < count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_size = byte_size * 2;
            gpu.ssbo = backend_.create_buffer(BufferUsage::Storage, alloc_size);
        }

        size_t floats_needed = count * 4;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);
        for (size_t i = 0; i < count; ++i)
        {
            upload_scratch_[i * 4] = x_data[i];
            upload_scratch_[i * 4 + 1] = y_data[i];
            upload_scratch_[i * 4 + 2] = z_data[i];
            upload_scratch_[i * 4 + 3] = 0.0f;  // padding
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count = count;
        series.clear_dirty();
    }
    // Handle surface (vertex buffer + index buffer)
    else if (surface)
    {
        // Choose between wireframe and solid mesh
        const SurfaceMesh* active_mesh = nullptr;
        if (surface->wireframe())
        {
            if (!surface->is_wireframe_mesh_generated())
            {
                surface->generate_wireframe_mesh();
            }
            if (!surface->is_wireframe_mesh_generated())
                return;
            active_mesh = &surface->wireframe_mesh();
        }
        else
        {
            if (!surface->is_mesh_generated())
            {
                surface->generate_mesh();
            }
            if (!surface->is_mesh_generated())
                return;
            active_mesh = &surface->mesh();
        }

        if (active_mesh->vertices.empty() || active_mesh->indices.empty())
            return;

        size_t vert_byte_size = active_mesh->vertices.size() * sizeof(float);
        size_t idx_byte_size = active_mesh->indices.size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < active_mesh->vertex_count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, active_mesh->vertices.data(), vert_byte_size);
        gpu.uploaded_count = active_mesh->vertex_count;

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < active_mesh->indices.size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, active_mesh->indices.data(), idx_byte_size);
        gpu.index_count = active_mesh->indices.size();

        series.clear_dirty();
    }
    // Handle mesh (vertex buffer + index buffer)
    else if (mesh)
    {
        if (mesh->vertices().empty() || mesh->indices().empty())
            return;

        size_t vert_byte_size = mesh->vertices().size() * sizeof(float);
        size_t idx_byte_size = mesh->indices().size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < mesh->vertex_count())
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, mesh->vertices().data(), vert_byte_size);
        gpu.uploaded_count = mesh->vertex_count();

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < mesh->indices().size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, mesh->indices().data(), idx_byte_size);
        gpu.index_count = mesh->indices().size();

        series.clear_dirty();
    }
}

void Renderer::render_axes(AxesBase& axes,
                           const Rect& viewport,
                           uint32_t fig_width,
                           uint32_t fig_height)
{
    // Set scissor to axes viewport
    backend_.set_scissor(static_cast<int32_t>(viewport.x),
                         static_cast<int32_t>(viewport.y),
                         static_cast<uint32_t>(viewport.w),
                         static_cast<uint32_t>(viewport.h));

    // Set viewport
    backend_.set_viewport(viewport.x, viewport.y, viewport.w, viewport.h);

    FrameUBO ubo{};

    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        // 3D projection with camera
        // Build perspective projection
        float aspect = viewport.w / viewport.h;
        const auto& cam = axes3d->camera();

        if (cam.projection_mode == Camera::ProjectionMode::Perspective)
        {
            // Perspective projection matrix
            float fov_rad = cam.fov * 3.14159265f / 180.0f;
            float f = 1.0f / tanf(fov_rad * 0.5f);
            std::memset(ubo.projection, 0, 16 * sizeof(float));
            ubo.projection[0] = f / aspect;
            ubo.projection[5] = -f;  // Negative for Vulkan Y-down
            ubo.projection[10] = cam.far_clip / (cam.near_clip - cam.far_clip);
            ubo.projection[11] = -1.0f;
            ubo.projection[14] = (cam.far_clip * cam.near_clip) / (cam.near_clip - cam.far_clip);
        }
        else
        {
            // Orthographic projection with proper near/far depth
            // Must match Camera::projection_matrix() convention:
            // half_w = ortho_size * aspect, half_h = ortho_size
            float half_w = cam.ortho_size * aspect;
            float half_h = cam.ortho_size;
            build_ortho_projection_3d(
                -half_w, half_w, -half_h, half_h, cam.near_clip, cam.far_clip, ubo.projection);
        }

        // Camera view matrix
        const mat4& view = cam.view_matrix();
        std::memcpy(ubo.view, view.m, 16 * sizeof(float));

        // Model matrix maps data coordinates into fixed-size normalized cube
        mat4 model = axes3d->data_to_normalized_matrix();
        std::memcpy(ubo.model, model.m, 16 * sizeof(float));

        ubo.near_plane = cam.near_clip;
        ubo.far_plane = cam.far_clip;

        // Camera position for lighting
        ubo.camera_pos[0] = cam.position.x;
        ubo.camera_pos[1] = cam.position.y;
        ubo.camera_pos[2] = cam.position.z;

        // Light direction from Axes3D (configurable)
        if (axes3d->lighting_enabled())
        {
            vec3 ld = axes3d->light_dir();
            ubo.light_dir[0] = ld.x;
            ubo.light_dir[1] = ld.y;
            ubo.light_dir[2] = ld.z;
        }
        else
        {
            // Zero light_dir signals shader to skip lighting (use flat color)
            ubo.light_dir[0] = 0.0f;
            ubo.light_dir[1] = 0.0f;
            ubo.light_dir[2] = 0.0f;
        }
    }
    else if (auto* axes2d = dynamic_cast<Axes*>(&axes))
    {
        // 2D orthographic projection
        auto xlim = axes2d->x_limits();
        auto ylim = axes2d->y_limits();

        build_ortho_projection(xlim.min, xlim.max, ylim.min, ylim.max, ubo.projection);
        // Identity view matrix (2D)
        ubo.view[0] = 1.0f;
        ubo.view[5] = 1.0f;
        ubo.view[10] = 1.0f;
        ubo.view[15] = 1.0f;
        // Identity model matrix (2D)
        ubo.model[0] = 1.0f;
        ubo.model[5] = 1.0f;
        ubo.model[10] = 1.0f;
        ubo.model[15] = 1.0f;

        ubo.near_plane = 0.01f;
        ubo.far_plane = 1000.0f;

        // Default camera position and light for 2D
        ubo.camera_pos[0] = 0.0f;
        ubo.camera_pos[1] = 0.0f;
        ubo.camera_pos[2] = 1.0f;
        ubo.light_dir[0] = 0.0f;
        ubo.light_dir[1] = 0.0f;
        ubo.light_dir[2] = 1.0f;
    }

    ubo.viewport_width = viewport.w;
    ubo.viewport_height = viewport.h;
    ubo.time = 0.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // Render axis border (2D only)
    if (axes.border_enabled() && !dynamic_cast<Axes3D*>(&axes))
        render_axis_border(axes, viewport, fig_width, fig_height);

    // Render 3D bounding box and tick marks
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        render_bounding_box(*axes3d, viewport);
        render_tick_marks(*axes3d, viewport);
    }

    // Render grid BEFORE series so series appears on top (for 3D)
    render_grid(axes, viewport);

    // For 3D axes, sort series by distance from camera for correct transparency.
    // Opaque series render first (front-to-back for early-Z benefit),
    // then transparent series render back-to-front (painter's algorithm).
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        const auto& cam = axes3d->camera();
        vec3 cam_pos = cam.position;
        mat4 model_mat = axes3d->data_to_normalized_matrix();

        // Collect visible series with their distances
        struct SortEntry
        {
            Series* series;
            float distance;
            bool transparent;
        };
        std::vector<SortEntry> opaque_entries;
        std::vector<SortEntry> transparent_entries;

        for (auto& series_ptr : axes.series())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            if (series_ptr->is_dirty())
            {
                upload_series_data(*series_ptr);
            }

            // Compute centroid distance from camera
            vec3 centroid{0.0f, 0.0f, 0.0f};
            auto* line3d = dynamic_cast<LineSeries3D*>(series_ptr.get());
            auto* scatter3d = dynamic_cast<ScatterSeries3D*>(series_ptr.get());
            auto* surface = dynamic_cast<SurfaceSeries*>(series_ptr.get());
            auto* mesh_s = dynamic_cast<MeshSeries*>(series_ptr.get());

            if (line3d)
                centroid = line3d->compute_centroid();
            else if (scatter3d)
                centroid = scatter3d->compute_centroid();
            else if (surface)
                centroid = surface->compute_centroid();
            else if (mesh_s)
                centroid = mesh_s->compute_centroid();

            // Transform centroid to world space via model matrix
            vec4 world_c = mat4_mul_vec4(model_mat, {centroid.x, centroid.y, centroid.z, 1.0f});
            vec3 world_pos = {world_c.x, world_c.y, world_c.z};
            float dist = vec3_length(world_pos - cam_pos);

            bool is_transparent = (series_ptr->color().a * series_ptr->opacity()) < 0.99f;

            if (is_transparent)
            {
                transparent_entries.push_back({series_ptr.get(), dist, true});
            }
            else
            {
                opaque_entries.push_back({series_ptr.get(), dist, false});
            }
        }

        // Sort opaque front-to-back (for early-Z optimization)
        std::sort(opaque_entries.begin(),
                  opaque_entries.end(),
                  [](const SortEntry& a, const SortEntry& b) { return a.distance < b.distance; });

        // Sort transparent back-to-front (painter's algorithm)
        std::sort(transparent_entries.begin(),
                  transparent_entries.end(),
                  [](const SortEntry& a, const SortEntry& b) { return a.distance > b.distance; });

        // Render opaque first, then transparent
        for (auto& entry : opaque_entries)
        {
            render_series(*entry.series, viewport);
        }
        for (auto& entry : transparent_entries)
        {
            render_series(*entry.series, viewport);
        }
    }
    else
    {
        // 2D: render in order (no sorting needed)
        for (auto& series_ptr : axes.series())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            if (series_ptr->is_dirty())
            {
                upload_series_data(*series_ptr);
            }

            render_series(*series_ptr, viewport);
        }
    }

    // Tick labels, axis labels, and titles are now rendered by ImGui
}

void Renderer::render_grid(AxesBase& axes, const Rect& /*viewport*/)
{
    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        if (!axes3d->grid_enabled())
            return;

        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();
        auto gp = axes3d->grid_planes();
        auto& gpu = axes_gpu_data_[&axes];

        // Check if limits/planes changed — skip regeneration if cached
        auto& gc = gpu.grid_cache;
        bool limits_changed = !gc.valid || gc.xmin != xlim.min
                              || gc.xmax != xlim.max || gc.ymin != ylim.min
                              || gc.ymax != ylim.max || gc.zmin != zlim.min
                              || gc.zmax != zlim.max
                              || gpu.cached_grid_planes != static_cast<int>(gp);

        if (limits_changed)
        {
            // Generate 3D grid vertices using Axes3DRenderer helper
            Axes3DRenderer::GridPlaneData grid_gen;
            vec3 min_corner = {xlim.min, ylim.min, zlim.min};
            vec3 max_corner = {xlim.max, ylim.max, zlim.max};

            if (static_cast<int>(gp & Axes3D::GridPlane::XY))
                grid_gen.generate_xy_plane(min_corner, max_corner, zlim.min, 10);
            if (static_cast<int>(gp & Axes3D::GridPlane::XZ))
                grid_gen.generate_xz_plane(min_corner, max_corner, ylim.min, 10);
            if (static_cast<int>(gp & Axes3D::GridPlane::YZ))
                grid_gen.generate_yz_plane(min_corner, max_corner, xlim.min, 10);

            if (grid_gen.vertices.empty())
                return;

            size_t float_count = grid_gen.vertices.size() * 3;
            if (grid_scratch_.size() < float_count)
                grid_scratch_.resize(float_count);
            for (size_t i = 0; i < grid_gen.vertices.size(); ++i)
            {
                grid_scratch_[i * 3] = grid_gen.vertices[i].x;
                grid_scratch_[i * 3 + 1] = grid_gen.vertices[i].y;
                grid_scratch_[i * 3 + 2] = grid_gen.vertices[i].z;
            }

            size_t byte_size = float_count * sizeof(float);
            if (!gpu.grid_buffer || gpu.grid_capacity < byte_size)
            {
                if (gpu.grid_buffer)
                    backend_.destroy_buffer(gpu.grid_buffer);
                gpu.grid_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
                gpu.grid_capacity = byte_size * 2;
            }
            backend_.upload_buffer(gpu.grid_buffer, grid_scratch_.data(), byte_size);
            gpu.grid_vertex_count = static_cast<uint32_t>(float_count / 3);
            gc.xmin = xlim.min;
            gc.xmax = xlim.max;
            gc.ymin = ylim.min;
            gc.ymax = ylim.max;
            gc.zmin = zlim.min;
            gc.zmax = zlim.max;
            gpu.cached_grid_planes = static_cast<int>(gp);
            gc.valid = true;
        }

        if (!gpu.grid_buffer || gpu.grid_vertex_count == 0)
            return;

        // Draw 3D grid as overlay (no depth test so it's always visible)
        backend_.bind_pipeline(grid_overlay3d_pipeline_);

        SeriesPushConstants pc{};
        const auto& theme_colors = ui::ThemeManager::instance().colors();
        float blend = 0.3f;
        pc.color[0] = theme_colors.grid_line.r * (1.0f - blend) + blend;
        pc.color[1] = theme_colors.grid_line.g * (1.0f - blend) + blend;
        pc.color[2] = theme_colors.grid_line.b * (1.0f - blend) + blend;
        pc.color[3] = 0.35f;
        pc.line_width = 1.0f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(gpu.grid_buffer, 0);
        backend_.draw(gpu.grid_vertex_count);
    }
    else if (auto* axes2d = dynamic_cast<Axes*>(&axes))
    {
        // 2D grid rendering
        if (!axes2d->grid_enabled())
            return;

        auto xlim = axes2d->x_limits();
        auto ylim = axes2d->y_limits();
        auto& gpu = axes_gpu_data_[&axes];

        // Check if limits changed — skip regeneration if cached
        auto& gc = gpu.grid_cache;
        bool limits_changed = !gc.valid || gc.xmin != xlim.min
                              || gc.xmax != xlim.max || gc.ymin != ylim.min
                              || gc.ymax != ylim.max;

        if (limits_changed)
        {
            auto x_ticks = axes2d->compute_x_ticks();
            auto y_ticks = axes2d->compute_y_ticks();

            size_t num_x = x_ticks.positions.size();
            size_t num_y = y_ticks.positions.size();
            if (num_x == 0 && num_y == 0)
                return;

            size_t total_lines = num_x + num_y;
            size_t grid2d_floats = total_lines * 4;
            if (grid_scratch_.size() < grid2d_floats)
                grid_scratch_.resize(grid2d_floats);
            size_t wi = 0;

            for (size_t i = 0; i < num_x; ++i)
            {
                float x = x_ticks.positions[i];
                grid_scratch_[wi++] = x;
                grid_scratch_[wi++] = ylim.min;
                grid_scratch_[wi++] = x;
                grid_scratch_[wi++] = ylim.max;
            }
            for (size_t i = 0; i < num_y; ++i)
            {
                float y = y_ticks.positions[i];
                grid_scratch_[wi++] = xlim.min;
                grid_scratch_[wi++] = y;
                grid_scratch_[wi++] = xlim.max;
                grid_scratch_[wi++] = y;
            }

            size_t byte_size = wi * sizeof(float);
            if (!gpu.grid_buffer || gpu.grid_capacity < byte_size)
            {
                if (gpu.grid_buffer)
                    backend_.destroy_buffer(gpu.grid_buffer);
                gpu.grid_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
                gpu.grid_capacity = byte_size * 2;
            }
            backend_.upload_buffer(gpu.grid_buffer, grid_scratch_.data(), byte_size);
            gpu.grid_vertex_count = static_cast<uint32_t>(total_lines * 2);
            gc.xmin = xlim.min;
            gc.xmax = xlim.max;
            gc.ymin = ylim.min;
            gc.ymax = ylim.max;
            gc.valid = true;
        }

        if (!gpu.grid_buffer || gpu.grid_vertex_count == 0)
            return;

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc{};
        const auto& as = axes2d->axis_style();
        if (as.grid_color.a > 0.0f)
        {
            pc.color[0] = as.grid_color.r;
            pc.color[1] = as.grid_color.g;
            pc.color[2] = as.grid_color.b;
            pc.color[3] = as.grid_color.a;
        }
        else
        {
            const auto& theme_colors = ui::ThemeManager::instance().colors();
            pc.color[0] = theme_colors.grid_line.r;
            pc.color[1] = theme_colors.grid_line.g;
            pc.color[2] = theme_colors.grid_line.b;
            pc.color[3] = theme_colors.grid_line.a;
        }
        pc.line_width = as.grid_width;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.set_line_width(std::max(1.0f, as.grid_width));
        backend_.bind_buffer(gpu.grid_buffer, 0);
        backend_.draw(gpu.grid_vertex_count);
        backend_.set_line_width(1.0f);
    }
}

void Renderer::render_bounding_box(Axes3D& axes, const Rect& /*viewport*/)
{
    if (!axes.show_bounding_box())
        return;

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();
    auto& gpu = axes_gpu_data_[&axes];

    // Check if limits changed — skip regeneration if cached
    auto& bc = gpu.bbox_cache;
    bool limits_changed = !bc.valid || bc.xmin != xlim.min
                          || bc.xmax != xlim.max || bc.ymin != ylim.min
                          || bc.ymax != ylim.max || bc.zmin != zlim.min
                          || bc.zmax != zlim.max;

    if (limits_changed)
    {
        vec3 min_corner = {xlim.min, ylim.min, zlim.min};
        vec3 max_corner = {xlim.max, ylim.max, zlim.max};

        Axes3DRenderer::BoundingBoxData bbox;
        bbox.generate(min_corner, max_corner);

        if (bbox.edge_vertices.empty())
            return;

        size_t bbox_floats = bbox.edge_vertices.size() * 3;
        if (bbox_scratch_.size() < bbox_floats)
            bbox_scratch_.resize(bbox_floats);
        for (size_t i = 0; i < bbox.edge_vertices.size(); ++i)
        {
            bbox_scratch_[i * 3] = bbox.edge_vertices[i].x;
            bbox_scratch_[i * 3 + 1] = bbox.edge_vertices[i].y;
            bbox_scratch_[i * 3 + 2] = bbox.edge_vertices[i].z;
        }

        size_t byte_size = bbox_floats * sizeof(float);
        if (!gpu.bbox_buffer || gpu.bbox_capacity < byte_size)
        {
            if (gpu.bbox_buffer)
                backend_.destroy_buffer(gpu.bbox_buffer);
            gpu.bbox_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size);
            gpu.bbox_capacity = byte_size;
        }
        backend_.upload_buffer(gpu.bbox_buffer, bbox_scratch_.data(), byte_size);
        gpu.bbox_vertex_count = static_cast<uint32_t>(bbox.edge_vertices.size());
        bc.xmin = xlim.min;
        bc.xmax = xlim.max;
        bc.ymin = ylim.min;
        bc.ymax = ylim.max;
        bc.zmin = zlim.min;
        bc.zmax = zlim.max;
        bc.valid = true;
    }

    if (!gpu.bbox_buffer || gpu.bbox_vertex_count == 0)
        return;

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc{};
    const auto& theme_colors = ui::ThemeManager::instance().colors();
    pc.color[0] = theme_colors.grid_line.r * 0.7f;
    pc.color[1] = theme_colors.grid_line.g * 0.7f;
    pc.color[2] = theme_colors.grid_line.b * 0.7f;
    pc.color[3] = theme_colors.grid_line.a * 0.8f;
    pc.line_width = 1.5f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.bbox_buffer, 0);
    backend_.draw(gpu.bbox_vertex_count);
}

void Renderer::render_tick_marks(Axes3D& axes, const Rect& /*viewport*/)
{
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();
    auto& gpu = axes_gpu_data_[&axes];

    // Check if limits changed — skip regeneration if cached
    auto& tc = gpu.tick_cache;
    bool limits_changed = !tc.valid || tc.xmin != xlim.min
                          || tc.xmax != xlim.max || tc.ymin != ylim.min
                          || tc.ymax != ylim.max || tc.zmin != zlim.min
                          || tc.zmax != zlim.max;

    if (limits_changed)
    {
        vec3 min_corner = {xlim.min, ylim.min, zlim.min};
        vec3 max_corner = {xlim.max, ylim.max, zlim.max};

        // Tick length: ~2% of axis range
        float x_tick_len = (ylim.max - ylim.min) * 0.02f;
        float y_tick_len = (xlim.max - xlim.min) * 0.02f;
        float z_tick_len = (xlim.max - xlim.min) * 0.02f;

        Axes3DRenderer::TickMarkData x_data;
        x_data.generate_x_ticks(axes, min_corner, max_corner);
        Axes3DRenderer::TickMarkData y_data;
        y_data.generate_y_ticks(axes, min_corner, max_corner);
        Axes3DRenderer::TickMarkData z_data;
        z_data.generate_z_ticks(axes, min_corner, max_corner);

        size_t total_ticks =
            x_data.positions.size() + y_data.positions.size() + z_data.positions.size();
        if (total_ticks == 0)
            return;

        size_t floats_needed = total_ticks * 6;
        if (tick_scratch_.size() < floats_needed)
            tick_scratch_.resize(floats_needed);
        size_t wi = 0;

        for (const auto& pos : x_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y - x_tick_len;
            tick_scratch_[wi++] = pos.z;
        }
        for (const auto& pos : y_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x - y_tick_len;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
        }
        for (const auto& pos : z_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x - z_tick_len;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
        }

        size_t byte_size = wi * sizeof(float);
        if (!gpu.tick_buffer || gpu.tick_capacity < byte_size)
        {
            if (gpu.tick_buffer)
                backend_.destroy_buffer(gpu.tick_buffer);
            gpu.tick_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
            gpu.tick_capacity = byte_size * 2;
        }
        backend_.upload_buffer(gpu.tick_buffer, tick_scratch_.data(), byte_size);
        gpu.tick_vertex_count = static_cast<uint32_t>(wi / 3);
        tc.xmin = xlim.min;
        tc.xmax = xlim.max;
        tc.ymin = ylim.min;
        tc.ymax = ylim.max;
        tc.zmin = zlim.min;
        tc.zmax = zlim.max;
        tc.valid = true;
    }

    if (!gpu.tick_buffer || gpu.tick_vertex_count == 0)
        return;

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc{};
    const auto& theme_colors = ui::ThemeManager::instance().colors();
    pc.color[0] = theme_colors.grid_line.r * 0.6f;
    pc.color[1] = theme_colors.grid_line.g * 0.6f;
    pc.color[2] = theme_colors.grid_line.b * 0.6f;
    pc.color[3] = theme_colors.grid_line.a;
    pc.line_width = 1.5f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.tick_buffer, 0);
    backend_.draw(gpu.tick_vertex_count);
}

void Renderer::render_axis_border(AxesBase& axes,
                                  const Rect& /*viewport*/,
                                  uint32_t /*fig_width*/,
                                  uint32_t /*fig_height*/)
{
    // Draw border in data space using the already-bound data-space UBO.
    // Inset vertices by a tiny fraction of the axis range so they don't
    // land exactly on the NDC ±1.0 clip boundary (which causes clipping
    // of the top/right edges on some GPUs).
    auto* axes2d = dynamic_cast<Axes*>(&axes);
    if (!axes2d)
        return;  // Border only for 2D axes
    auto xlim = axes2d->x_limits();
    auto ylim = axes2d->y_limits();

    float x_range = xlim.max - xlim.min;
    float y_range = ylim.max - ylim.min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    // Use epsilon to prevent NDC boundary clipping
    // Slightly larger for symmetric ranges to ensure all borders visible
    float eps_x = 0.002f * x_range;  // 0.2% of x range
    float eps_y = 0.002f * y_range;  // 0.2% of y range
    const float MIN_EPS = 1e-8f;
    if (eps_x < MIN_EPS)
        eps_x = MIN_EPS;
    if (eps_y < MIN_EPS)
        eps_y = MIN_EPS;

    float x0 = xlim.min + eps_x;
    float y0 = ylim.min + eps_y;
    float x1 = xlim.max - eps_x;
    float y1 = ylim.max - eps_y;

    float border_verts[] = {
        // Bottom edge
        x0,
        y0,
        x1,
        y0,
        // Top edge
        x0,
        y1,
        x1,
        y1,
        // Left edge
        x0,
        y0,
        x0,
        y1,
        // Right edge
        x1,
        y0,
        x1,
        y1,
    };

    size_t byte_size = sizeof(border_verts);

    // Use per-axes border buffer so multi-subplot draws don't overwrite
    // each other within the same command buffer.
    auto& gpu = axes_gpu_data_[&axes];
    if (!gpu.border_buffer || gpu.border_capacity < byte_size)
    {
        if (gpu.border_buffer)
        {
            backend_.destroy_buffer(gpu.border_buffer);
        }
        gpu.border_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        gpu.border_capacity = byte_size;
    }
    backend_.upload_buffer(gpu.border_buffer, border_verts, byte_size);

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc{};
    const auto& theme_colors = ui::ThemeManager::instance().colors();
    pc.color[0] = theme_colors.axis_line.r;
    pc.color[1] = theme_colors.axis_line.g;
    pc.color[2] = theme_colors.axis_line.b;
    pc.color[3] = theme_colors.axis_line.a;
    pc.line_width = 1.0f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.border_buffer, 0);
    backend_.draw(8);  // 4 lines × 2 vertices
}

void Renderer::render_series(Series& series, const Rect& /*viewport*/)
{
    auto it = series_gpu_data_.find(&series);
    if (it == series_gpu_data_.end())
        return;

    auto& gpu = it->second;
    if (!gpu.ssbo)
        return;

    SeriesPushConstants pc{};
    const auto& c = series.color();
    pc.color[0] = c.r;
    pc.color[1] = c.g;
    pc.color[2] = c.b;
    pc.color[3] = c.a * series.opacity();

    const auto& style = series.plot_style();
    pc.line_style = static_cast<uint32_t>(style.line_style);
    pc.marker_type = static_cast<uint32_t>(style.marker_style);
    pc.marker_size = style.marker_size;
    pc.opacity = style.opacity;

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
            if (style.has_line() && line->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = line->width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(line->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            if (style.has_marker())
            {
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size = style.marker_size;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw_instanced(6, static_cast<uint32_t>(line->point_count()));
            }
            break;
        }
        case SeriesType::Scatter2D:
        {
            auto* scatter = static_cast<ScatterSeries*>(&series);
            backend_.bind_pipeline(scatter_pipeline_);
            pc.point_size = scatter->size();
            pc.marker_type = static_cast<uint32_t>(style.marker_style);
            if (pc.marker_type == 0)
                pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
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
            auto* scatter3d = static_cast<ScatterSeries3D*>(&series);
            bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
            backend_.bind_pipeline(is_transparent ? scatter3d_transparent_pipeline_
                                                  : scatter3d_pipeline_);
            pc.point_size = scatter3d->size();
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
        case SeriesType::Unknown:
            break;
    }
}

void Renderer::build_ortho_projection(float left, float right, float bottom, float top, float* m)
{
    // Column-major 4x4 orthographic projection matrix
    // Maps [left,right] x [bottom,top] to [-1,1] x [-1,1]
    std::memset(m, 0, 16 * sizeof(float));

    float rl = right - left;
    float tb = top - bottom;

    if (rl == 0.0f)
        rl = 1.0f;
    if (tb == 0.0f)
        tb = 1.0f;

    m[0] = 2.0f / rl;
    m[5] = -2.0f / tb;  // Negate for Vulkan Y-down clip space
    m[10] = -1.0f;
    m[12] = -(right + left) / rl;
    m[13] = (top + bottom) / tb;  // Flip sign for Vulkan
    m[15] = 1.0f;
}

void Renderer::build_ortho_projection_3d(
    float left, float right, float bottom, float top, float near_clip, float far_clip, float* m)
{
    // Column-major 4x4 orthographic projection with proper depth mapping.
    // Maps [left,right] x [bottom,top] x [near,far] to Vulkan clip space.
    std::memset(m, 0, 16 * sizeof(float));

    float rl = right - left;
    float tb = top - bottom;
    float fn = far_clip - near_clip;

    if (rl == 0.0f)
        rl = 1.0f;
    if (tb == 0.0f)
        tb = 1.0f;
    if (fn == 0.0f)
        fn = 1.0f;

    m[0] = 2.0f / rl;
    m[5] = -2.0f / tb;   // Negate for Vulkan Y-down
    m[10] = -1.0f / fn;  // Maps [near,far] → [0,1] for Vulkan depth
    m[12] = -(right + left) / rl;
    m[13] = (top + bottom) / tb;
    m[14] = -near_clip / fn;  // Depth offset
    m[15] = 1.0f;
}

}  // namespace spectra
