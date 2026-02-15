#include "renderer.hpp"
#include "../ui/theme.hpp"
#include "../ui/axes3d_renderer.hpp"

#include <plotix/axes.hpp>
#include <plotix/axes3d.hpp>
#include <plotix/camera.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>
#include <plotix/series3d.hpp>

#include <cstring>
#include <vector>

namespace plotix {

Renderer::Renderer(Backend& backend)
    : backend_(backend)
{}

Renderer::~Renderer() {
    // Wait for GPU to finish using all resources before destroying them
    backend_.wait_idle();
    
    // Clean up per-series GPU data
    for (auto& [ptr, data] : series_gpu_data_) {
        if (data.ssbo) {
            backend_.destroy_buffer(data.ssbo);
        }
        if (data.index_buffer) {
            backend_.destroy_buffer(data.index_buffer);
        }
    }
    series_gpu_data_.clear();

    // Clean up per-axes GPU data (grid + border + bbox + tick buffers)
    for (auto& [ptr, data] : axes_gpu_data_) {
        if (data.grid_buffer)   backend_.destroy_buffer(data.grid_buffer);
        if (data.border_buffer) backend_.destroy_buffer(data.border_buffer);
        if (data.bbox_buffer)   backend_.destroy_buffer(data.bbox_buffer);
        if (data.tick_buffer)   backend_.destroy_buffer(data.tick_buffer);
    }
    axes_gpu_data_.clear();

    if (frame_ubo_buffer_) {
        backend_.destroy_buffer(frame_ubo_buffer_);
    }
}

bool Renderer::init() {
    // Create pipelines for each series type
    line_pipeline_    = backend_.create_pipeline(PipelineType::Line);
    scatter_pipeline_ = backend_.create_pipeline(PipelineType::Scatter);
    grid_pipeline_    = backend_.create_pipeline(PipelineType::Grid);
    
    // Create 3D pipelines
    line3d_pipeline_    = backend_.create_pipeline(PipelineType::Line3D);
    scatter3d_pipeline_ = backend_.create_pipeline(PipelineType::Scatter3D);
    mesh3d_pipeline_    = backend_.create_pipeline(PipelineType::Mesh3D);
    surface3d_pipeline_ = backend_.create_pipeline(PipelineType::Surface3D);
    grid3d_pipeline_    = backend_.create_pipeline(PipelineType::Grid3D);
    grid_overlay3d_pipeline_ = backend_.create_pipeline(PipelineType::GridOverlay3D);
    
    // Create frame UBO buffer
    frame_ubo_buffer_ = backend_.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));

    return true;
}

void Renderer::begin_render_pass() {
    const auto& theme_colors = ui::ThemeManager::instance().colors();
    Color bg_color = Color(theme_colors.bg_primary.r, theme_colors.bg_primary.g,
                          theme_colors.bg_primary.b, theme_colors.bg_primary.a);
    backend_.begin_render_pass(bg_color);
}

void Renderer::render_figure_content(Figure& figure) {
    uint32_t w = figure.width();
    uint32_t h = figure.height();

    // Set full-figure viewport and scissor
    backend_.set_viewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    backend_.set_scissor(0, 0, w, h);

    // Render each 2D axes
    for (auto& axes_ptr : figure.axes()) {
        if (!axes_ptr) continue;
        auto& ax = *axes_ptr;
        const auto& vp = ax.viewport();

        render_axes(ax, vp, w, h);
    }

    // Render each 3D axes (stored in all_axes_)
    for (auto& axes_ptr : figure.all_axes()) {
        if (!axes_ptr) continue;
        auto& ax = *axes_ptr;
        const auto& vp = ax.viewport();

        render_axes(ax, vp, w, h);
    }

    // Legend and text labels are now rendered by ImGui (see imgui_integration.cpp)
}

void Renderer::end_render_pass() {
    backend_.end_render_pass();
}

void Renderer::render_figure(Figure& figure) {
    // Convenience: starts render pass, draws content, ends render pass.
    // Use begin_render_pass / render_figure_content / end_render_pass
    // separately when ImGui or other overlays need to render inside the
    // same render pass.
    begin_render_pass();
    render_figure_content(figure);
    end_render_pass();
}

void Renderer::update_frame_ubo(uint32_t width, uint32_t height, float time) {
    FrameUBO ubo {};
    // Identity projection (will be overridden per-axes)
    ubo.projection[0]  = 1.0f;
    ubo.projection[5]  = 1.0f;
    ubo.projection[10] = 1.0f;
    ubo.projection[15] = 1.0f;
    // Identity view matrix (2D default)
    ubo.view[0]  = 1.0f;
    ubo.view[5]  = 1.0f;
    ubo.view[10] = 1.0f;
    ubo.view[15] = 1.0f;
    // Identity model matrix (2D default)
    ubo.model[0]  = 1.0f;
    ubo.model[5]  = 1.0f;
    ubo.model[10] = 1.0f;
    ubo.model[15] = 1.0f;
    ubo.viewport_width  = static_cast<float>(width);
    ubo.viewport_height = static_cast<float>(height);
    ubo.time = time;
    // 3D defaults (unused in 2D, but must be initialized)
    ubo.near_plane = 0.01f;
    ubo.far_plane  = 1000.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
}

void Renderer::upload_series_data(Series& series) {
    // Try 2D series first
    auto* line = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);
    
    // Try 3D series
    auto* line3d = dynamic_cast<LineSeries3D*>(&series);
    auto* scatter3d = dynamic_cast<ScatterSeries3D*>(&series);
    auto* surface = dynamic_cast<SurfaceSeries*>(&series);
    auto* mesh = dynamic_cast<MeshSeries*>(&series);

    auto& gpu = series_gpu_data_[&series];

    // Handle 2D line/scatter (vec2 interleaved)
    if (line || scatter) {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t count = 0;

        if (line) {
            x_data = line->x_data().data();
            y_data = line->y_data().data();
            count  = line->point_count();
        } else if (scatter) {
            x_data = scatter->x_data().data();
            y_data = scatter->y_data().data();
            count  = scatter->point_count();
        }

        if (count == 0) return;

        size_t byte_size = count * 2 * sizeof(float);
        if (!gpu.ssbo || gpu.uploaded_count < count) {
            if (gpu.ssbo) backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_size = byte_size * 2;
            gpu.ssbo = backend_.create_buffer(BufferUsage::Storage, alloc_size);
        }

        std::vector<float> interleaved(count * 2);
        for (size_t i = 0; i < count; ++i) {
            interleaved[i * 2]     = x_data[i];
            interleaved[i * 2 + 1] = y_data[i];
        }

        backend_.upload_buffer(gpu.ssbo, interleaved.data(), byte_size);
        gpu.uploaded_count = count;
        series.clear_dirty();
    }
    // Handle 3D line/scatter (vec4 interleaved: x,y,z,pad)
    else if (line3d || scatter3d) {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        const float* z_data = nullptr;
        size_t count = 0;

        if (line3d) {
            x_data = line3d->x_data().data();
            y_data = line3d->y_data().data();
            z_data = line3d->z_data().data();
            count  = line3d->point_count();
        } else if (scatter3d) {
            x_data = scatter3d->x_data().data();
            y_data = scatter3d->y_data().data();
            z_data = scatter3d->z_data().data();
            count  = scatter3d->point_count();
        }

        if (count == 0) return;

        size_t byte_size = count * 4 * sizeof(float);
        if (!gpu.ssbo || gpu.uploaded_count < count) {
            if (gpu.ssbo) backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_size = byte_size * 2;
            gpu.ssbo = backend_.create_buffer(BufferUsage::Storage, alloc_size);
        }

        std::vector<float> interleaved(count * 4);
        for (size_t i = 0; i < count; ++i) {
            interleaved[i * 4]     = x_data[i];
            interleaved[i * 4 + 1] = y_data[i];
            interleaved[i * 4 + 2] = z_data[i];
            interleaved[i * 4 + 3] = 0.0f;  // padding
        }

        backend_.upload_buffer(gpu.ssbo, interleaved.data(), byte_size);
        gpu.uploaded_count = count;
        series.clear_dirty();
    }
    // Handle surface (vertex buffer + index buffer)
    else if (surface) {
        if (!surface->is_mesh_generated()) {
            surface->generate_mesh();
        }
        if (!surface->is_mesh_generated()) return;
        
        const auto& surf_mesh = surface->mesh();
        if (surf_mesh.vertices.empty() || surf_mesh.indices.empty()) return;

        size_t vert_byte_size = surf_mesh.vertices.size() * sizeof(float);
        size_t idx_byte_size = surf_mesh.indices.size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < surf_mesh.vertex_count) {
            if (gpu.ssbo) backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, surf_mesh.vertices.data(), vert_byte_size);
        gpu.uploaded_count = surf_mesh.vertex_count;

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < surf_mesh.indices.size()) {
            if (gpu.index_buffer) backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, surf_mesh.indices.data(), idx_byte_size);
        gpu.index_count = surf_mesh.indices.size();

        series.clear_dirty();
    }
    // Handle mesh (vertex buffer + index buffer)
    else if (mesh) {
        if (mesh->vertices().empty() || mesh->indices().empty()) return;

        size_t vert_byte_size = mesh->vertices().size() * sizeof(float);
        size_t idx_byte_size = mesh->indices().size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < mesh->vertex_count()) {
            if (gpu.ssbo) backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, mesh->vertices().data(), vert_byte_size);
        gpu.uploaded_count = mesh->vertex_count();

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < mesh->indices().size()) {
            if (gpu.index_buffer) backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, mesh->indices().data(), idx_byte_size);
        gpu.index_count = mesh->indices().size();

        series.clear_dirty();
    }
}

void Renderer::render_axes(AxesBase& axes, const Rect& viewport,
                            uint32_t fig_width, uint32_t fig_height) {
    // Set scissor to axes viewport
    backend_.set_scissor(
        static_cast<int32_t>(viewport.x),
        static_cast<int32_t>(viewport.y),
        static_cast<uint32_t>(viewport.w),
        static_cast<uint32_t>(viewport.h)
    );

    // Set viewport
    backend_.set_viewport(viewport.x, viewport.y, viewport.w, viewport.h);

    FrameUBO ubo {};
    
    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes)) {
        // 3D projection with camera
        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();
        
        // Build perspective projection
        float aspect = viewport.w / viewport.h;
        const auto& cam = axes3d->camera();
        
        if (cam.projection_mode == Camera::ProjectionMode::Perspective) {
            // Perspective projection matrix
            float fov_rad = cam.fov * 3.14159265f / 180.0f;
            float f = 1.0f / tanf(fov_rad * 0.5f);
            std::memset(ubo.projection, 0, 16 * sizeof(float));
            ubo.projection[0] = f / aspect;
            ubo.projection[5] = -f;  // Negative for Vulkan Y-down
            ubo.projection[10] = cam.far_clip / (cam.near_clip - cam.far_clip);
            ubo.projection[11] = -1.0f;
            ubo.projection[14] = (cam.far_clip * cam.near_clip) / (cam.near_clip - cam.far_clip);
        } else {
            // Orthographic projection
            float half_w = cam.ortho_size * aspect * 0.5f;
            float half_h = cam.ortho_size * 0.5f;
            build_ortho_projection(-half_w, half_w, -half_h, half_h, ubo.projection);
        }
        
        // Camera view matrix
        const mat4& view = cam.view_matrix();
        std::memcpy(ubo.view, view.m, 16 * sizeof(float));
        
        // Identity model matrix for now
        ubo.model[0] = 1.0f; ubo.model[5] = 1.0f;
        ubo.model[10] = 1.0f; ubo.model[15] = 1.0f;
        
        ubo.near_plane = cam.near_clip;
        ubo.far_plane = cam.far_clip;
        
        // Camera position for lighting
        ubo.camera_pos[0] = cam.position.x;
        ubo.camera_pos[1] = cam.position.y;
        ubo.camera_pos[2] = cam.position.z;
        
        // Default light direction (from top-right)
        ubo.light_dir[0] = 1.0f;
        ubo.light_dir[1] = 1.0f;
        ubo.light_dir[2] = 1.0f;
    } else if (auto* axes2d = dynamic_cast<Axes*>(&axes)) {
        // 2D orthographic projection
        auto xlim = axes2d->x_limits();
        auto ylim = axes2d->y_limits();
        
        build_ortho_projection(xlim.min, xlim.max, ylim.min, ylim.max, ubo.projection);
        // Identity view matrix (2D)
        ubo.view[0]  = 1.0f; ubo.view[5]  = 1.0f;
        ubo.view[10] = 1.0f; ubo.view[15] = 1.0f;
        // Identity model matrix (2D)
        ubo.model[0]  = 1.0f; ubo.model[5]  = 1.0f;
        ubo.model[10] = 1.0f; ubo.model[15] = 1.0f;
        
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
    
    ubo.viewport_width  = viewport.w;
    ubo.viewport_height = viewport.h;
    ubo.time = 0.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // Render axis border (2D only)
    if (axes.border_enabled() && !dynamic_cast<Axes3D*>(&axes))
        render_axis_border(axes, viewport, fig_width, fig_height);

    // Render 3D bounding box and tick marks
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes)) {
        render_bounding_box(*axes3d, viewport);
        render_tick_marks(*axes3d, viewport);
    }

    // Render grid BEFORE series so series appears on top (for 3D)
    render_grid(axes, viewport);

    // Render each series (skip hidden ones)
    for (auto& series_ptr : axes.series()) {
        if (!series_ptr || !series_ptr->visible()) continue;

        if (series_ptr->is_dirty()) {
            upload_series_data(*series_ptr);
        }

        render_series(*series_ptr, viewport);
    }

    // Tick labels, axis labels, and titles are now rendered by ImGui
}

void Renderer::render_grid(AxesBase& axes, const Rect& /*viewport*/) {
    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes)) {
        if (!axes3d->grid_enabled()) return;
        
        auto x_ticks = axes3d->compute_x_ticks();
        auto y_ticks = axes3d->compute_y_ticks();
        auto z_ticks = axes3d->compute_z_ticks();
        
        size_t num_x = x_ticks.positions.size();
        size_t num_y = y_ticks.positions.size();
        size_t num_z = z_ticks.positions.size();
        
        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();
        
        // Generate 3D grid vertices using Axes3DRenderer helper
        Axes3DRenderer::GridPlaneData grid_gen;
        vec3 min_corner = {xlim.min, ylim.min, zlim.min};
        vec3 max_corner = {xlim.max, ylim.max, zlim.max};
        int grid_planes = axes3d->grid_planes();
        
        // XY plane grid (z = zlim.min)
        if (grid_planes & static_cast<int>(Axes3D::GridPlane::XY)) {
            grid_gen.generate_xy_plane(min_corner, max_corner, zlim.min, 10);
        }
        
        // XZ plane grid (y = ylim.min)
        if (grid_planes & static_cast<int>(Axes3D::GridPlane::XZ)) {
            grid_gen.generate_xz_plane(min_corner, max_corner, ylim.min, 10);
        }
        
        // YZ plane grid (x = xlim.min)
        if (grid_planes & static_cast<int>(Axes3D::GridPlane::YZ)) {
            grid_gen.generate_yz_plane(min_corner, max_corner, xlim.min, 10);
        }

        if (grid_gen.vertices.empty()) return;

        // Convert vec3 vertices to float array
        std::vector<float> verts;
        verts.reserve(grid_gen.vertices.size() * 3);
        for (const auto& v : grid_gen.vertices) {
            verts.push_back(v.x);
            verts.push_back(v.y);
            verts.push_back(v.z);
        }
        
        if (verts.empty()) return;
        
        // Upload 3D grid vertices
        auto& gpu = axes_gpu_data_[&axes];
        size_t byte_size = verts.size() * sizeof(float);
        if (!gpu.grid_buffer || gpu.grid_capacity < byte_size) {
            if (gpu.grid_buffer) backend_.destroy_buffer(gpu.grid_buffer);
            gpu.grid_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
            gpu.grid_capacity = byte_size * 2;
        }
        backend_.upload_buffer(gpu.grid_buffer, verts.data(), byte_size);
        
        // Draw 3D grid as overlay (no depth test so it's always visible)
        backend_.bind_pipeline(grid_overlay3d_pipeline_);
        
        SeriesPushConstants pc {};
        const auto& theme_colors = ui::ThemeManager::instance().colors();
        // Blend grid color toward lighter gray for visibility on dark background
        float blend = 0.3f;
        pc.color[0] = theme_colors.grid_line.r * (1.0f - blend) + blend;
        pc.color[1] = theme_colors.grid_line.g * (1.0f - blend) + blend;
        pc.color[2] = theme_colors.grid_line.b * (1.0f - blend) + blend;
        pc.color[3] = 0.35f;  // Semi-transparent overlay
        pc.line_width = 1.0f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);
        
        backend_.bind_buffer(gpu.grid_buffer, 0);
        backend_.draw(static_cast<uint32_t>(verts.size() / 3)); // 3 floats per vertex
        
    } else if (auto* axes2d = dynamic_cast<Axes*>(&axes)) {
        // 2D grid rendering (original implementation)
        if (!axes2d->grid_enabled()) return;

        auto x_ticks = axes2d->compute_x_ticks();
        auto y_ticks = axes2d->compute_y_ticks();

        size_t num_x = x_ticks.positions.size();
        size_t num_y = y_ticks.positions.size();
        if (num_x == 0 && num_y == 0) return;

        auto xlim = axes2d->x_limits();
        auto ylim = axes2d->y_limits();

        // Generate line vertices in data space
        // Each line = 2 endpoints (vec2 each) = 4 floats per line
        size_t total_lines = num_x + num_y;
        std::vector<float> verts;
        verts.reserve(total_lines * 4);

        // Vertical grid lines (at each x tick, from ylim.min to ylim.max)
        for (size_t i = 0; i < num_x; ++i) {
            float x = x_ticks.positions[i];
            verts.push_back(x); verts.push_back(ylim.min);
            verts.push_back(x); verts.push_back(ylim.max);
        }

        // Horizontal grid lines (at each y tick, from xlim.min to xlim.max)
        for (size_t i = 0; i < num_y; ++i) {
            float y = y_ticks.positions[i];
            verts.push_back(xlim.min); verts.push_back(y);
            verts.push_back(xlim.max); verts.push_back(y);
        }

        // Upload grid vertices to per-axes buffer
        auto& gpu = axes_gpu_data_[&axes];
        size_t byte_size = verts.size() * sizeof(float);
        if (!gpu.grid_buffer || gpu.grid_capacity < byte_size) {
            if (gpu.grid_buffer) backend_.destroy_buffer(gpu.grid_buffer);
            gpu.grid_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
            gpu.grid_capacity = byte_size * 2;
        }
        backend_.upload_buffer(gpu.grid_buffer, verts.data(), byte_size);

        // Bind grid pipeline and draw
        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc {};
        const auto& theme_colors = ui::ThemeManager::instance().colors();
        pc.color[0] = theme_colors.grid_line.r;
        pc.color[1] = theme_colors.grid_line.g;
        pc.color[2] = theme_colors.grid_line.b;
        pc.color[3] = theme_colors.grid_line.a;
        pc.line_width = 1.0f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(gpu.grid_buffer, 0);
        backend_.draw(static_cast<uint32_t>(total_lines * 2));
    }
}

void Renderer::render_bounding_box(Axes3D& axes, const Rect& /*viewport*/) {
    if (!axes.show_bounding_box()) return;

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    vec3 min_corner = {xlim.min, ylim.min, zlim.min};
    vec3 max_corner = {xlim.max, ylim.max, zlim.max};

    Axes3DRenderer::BoundingBoxData bbox;
    bbox.generate(min_corner, max_corner);

    if (bbox.edge_vertices.empty()) return;

    // Convert vec3 vertices to float array
    std::vector<float> verts;
    verts.reserve(bbox.edge_vertices.size() * 3);
    for (const auto& v : bbox.edge_vertices) {
        verts.push_back(v.x);
        verts.push_back(v.y);
        verts.push_back(v.z);
    }

    auto& gpu = axes_gpu_data_[&axes];
    size_t byte_size = verts.size() * sizeof(float);
    if (!gpu.bbox_buffer || gpu.bbox_capacity < byte_size) {
        if (gpu.bbox_buffer) backend_.destroy_buffer(gpu.bbox_buffer);
        gpu.bbox_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        gpu.bbox_capacity = byte_size;
    }
    backend_.upload_buffer(gpu.bbox_buffer, verts.data(), byte_size);

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc {};
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
    backend_.draw(static_cast<uint32_t>(bbox.edge_vertices.size()));
}

void Renderer::render_tick_marks(Axes3D& axes, const Rect& /*viewport*/) {
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    vec3 min_corner = {xlim.min, ylim.min, zlim.min};
    vec3 max_corner = {xlim.max, ylim.max, zlim.max};

    Axes3DRenderer::TickMarkData tick_gen;
    tick_gen.generate_x_ticks(axes, min_corner, max_corner);
    tick_gen.generate_y_ticks(axes, min_corner, max_corner);
    tick_gen.generate_z_ticks(axes, min_corner, max_corner);

    if (tick_gen.positions.empty()) return;

    // TickMarkData.positions contains the start points of ticks.
    // The previous implementation generated LINE SEGMENTS.
    // Axes3DRenderer::TickMarkData seems to only generate POSITIONS (start points)?
    // Let's check Axes3DRenderer implementation.
    // ...
    // Ah, wait. Axes3DRenderer only generates POSITIONS and LABELS.
    // It does NOT generate the tick line geometry?
    // Let's check src/ui/axes3d_renderer.cpp again.
    // 
    // Yes: 
    // void Axes3DRenderer::TickMarkData::generate_x_ticks(...) {
    //     positions.push_back({x, min_corner.y, min_corner.z});
    //     labels.push_back(tick_result.labels[i]);
    // }
    //
    // It only generates the position for the label/tick start. Not the line itself?
    // The previous Renderer implementation generated LINES:
    // {x, y0, z0, x, y0 - len, z0}
    //
    // So Axes3DRenderer is INCOMPLETE for tick lines?
    // It seems so.
    // I should EXTEND Axes3DRenderer to generate tick LINES as well, or keep logic here.
    // 
    // Plan: I will use Axes3DRenderer for POSITIONS (which I need for labels anyway),
    // but I still need to generate lines here, OR I should update Axes3DRenderer to generate lines.
    // Updating Axes3DRenderer is better for separation.
    //
    // For now, to proceed with refactoring without changing Axes3DRenderer too much,
    // I will iterate the generated positions and extend them into lines.
    
    // Tick length: ~2% of axis range
    float x1 = xlim.max, x0 = xlim.min;
    float y1 = ylim.max, y0 = ylim.min;
    float x_tick_len = (y1 - y0) * 0.02f;
    float y_tick_len = (x1 - x0) * 0.02f;
    float z_tick_len = (x1 - x0) * 0.02f;

    std::vector<float> verts;

    // Re-generate using helper separately to distinguish axes?
    // The helper mixes them if I call all 3? No, TickMarkData accumulates them if I call all 3.
    // But I need to know which axis they belong to to know the tick direction!
    //
    // So I should call them intimately.
    
    Axes3DRenderer::TickMarkData x_data;
    x_data.generate_x_ticks(axes, min_corner, max_corner);
    for (const auto& pos : x_data.positions) {
        verts.insert(verts.end(), {pos.x, pos.y, pos.z, pos.x, pos.y - x_tick_len, pos.z});
    }

    Axes3DRenderer::TickMarkData y_data;
    y_data.generate_y_ticks(axes, min_corner, max_corner);
    for (const auto& pos : y_data.positions) {
        verts.insert(verts.end(), {pos.x, pos.y, pos.z, pos.x - y_tick_len, pos.y, pos.z});
    }

    Axes3DRenderer::TickMarkData z_data;
    z_data.generate_z_ticks(axes, min_corner, max_corner);
    for (const auto& pos : z_data.positions) {
        verts.insert(verts.end(), {pos.x, pos.y, pos.z, pos.x - z_tick_len, pos.y, pos.z});
    }

    if (verts.empty()) return;

    auto& gpu = axes_gpu_data_[&axes];
    size_t byte_size = verts.size() * sizeof(float);
    if (!gpu.tick_buffer || gpu.tick_capacity < byte_size) {
        if (gpu.tick_buffer) backend_.destroy_buffer(gpu.tick_buffer);
        gpu.tick_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
        gpu.tick_capacity = byte_size * 2;
    }
    backend_.upload_buffer(gpu.tick_buffer, verts.data(), byte_size);

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc {};
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
    uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 3);
    backend_.draw(vertex_count);
}

void Renderer::render_axis_border(AxesBase& axes, const Rect& viewport,
                                   uint32_t /*fig_width*/, uint32_t /*fig_height*/) {
    // Draw border in data space using the already-bound data-space UBO.
    // Inset vertices by a tiny fraction of the axis range so they don't
    // land exactly on the NDC ±1.0 clip boundary (which causes clipping
    // of the top/right edges on some GPUs).
    auto* axes2d = dynamic_cast<Axes*>(&axes);
    if (!axes2d) return;  // Border only for 2D axes
    auto xlim = axes2d->x_limits();
    auto ylim = axes2d->y_limits();

    float x_range = xlim.max - xlim.min;
    float y_range = ylim.max - ylim.min;
    if (x_range == 0.0f) x_range = 1.0f;
    if (y_range == 0.0f) y_range = 1.0f;

    // Use epsilon to prevent NDC boundary clipping
    // Slightly larger for symmetric ranges to ensure all borders visible
    float eps_x = 0.002f * x_range;  // 0.2% of x range
    float eps_y = 0.002f * y_range;  // 0.2% of y range
    const float MIN_EPS = 1e-8f;
    if (eps_x < MIN_EPS) eps_x = MIN_EPS;
    if (eps_y < MIN_EPS) eps_y = MIN_EPS;

    
    float x0 = xlim.min + eps_x;
    float y0 = ylim.min + eps_y;
    float x1 = xlim.max - eps_x;
    float y1 = ylim.max - eps_y;

    float border_verts[] = {
        // Bottom edge
        x0, y0,  x1, y0,
        // Top edge
        x0, y1,  x1, y1,
        // Left edge
        x0, y0,  x0, y1,
        // Right edge
        x1, y0,  x1, y1,
    };

    size_t byte_size = sizeof(border_verts);

    // Use per-axes border buffer so multi-subplot draws don't overwrite
    // each other within the same command buffer.
    auto& gpu = axes_gpu_data_[&axes];
    if (!gpu.border_buffer || gpu.border_capacity < byte_size) {
        if (gpu.border_buffer) {
            backend_.destroy_buffer(gpu.border_buffer);
        }
        gpu.border_buffer = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        gpu.border_capacity = byte_size;
    }
    backend_.upload_buffer(gpu.border_buffer, border_verts, byte_size);

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc {};
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
    backend_.draw(8); // 4 lines × 2 vertices
}

void Renderer::render_series(Series& series, const Rect& /*viewport*/) {
    auto it = series_gpu_data_.find(&series);
    if (it == series_gpu_data_.end()) return;

    auto& gpu = it->second;
    if (!gpu.ssbo) return;

    SeriesPushConstants pc {};
    const auto& c = series.color();
    pc.color[0] = c.r;
    pc.color[1] = c.g;
    pc.color[2] = c.b;
    pc.color[3] = c.a * series.opacity();

    // Populate style fields from PlotStyle
    const auto& style = series.plot_style();
    pc.line_style  = static_cast<uint32_t>(style.line_style);
    pc.marker_type = static_cast<uint32_t>(style.marker_style);
    pc.marker_size = style.marker_size;
    pc.opacity     = style.opacity;

    // Populate dash pattern for non-solid line styles
    if (style.line_style != LineStyle::Solid && style.line_style != LineStyle::None) {
        auto* line = dynamic_cast<LineSeries*>(&series);
        float lw = line ? line->width() : 2.0f;
        DashPattern dp = get_dash_pattern(style.line_style, lw);
        for (int i = 0; i < dp.count && i < 8; ++i)
            pc.dash_pattern[i] = dp.segments[i];
        pc.dash_total = dp.total;
        pc.dash_count = dp.count;
    }

    // Try 2D series
    auto* line = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);
    
    // Try 3D series
    auto* line3d = dynamic_cast<LineSeries3D*>(&series);
    auto* scatter3d = dynamic_cast<ScatterSeries3D*>(&series);
    auto* surface = dynamic_cast<SurfaceSeries*>(&series);
    auto* mesh = dynamic_cast<MeshSeries*>(&series);

    if (line) {
        // Draw line segments if line style is not None
        if (style.has_line() && line->point_count() > 1) {
            backend_.bind_pipeline(line_pipeline_);
            pc.line_width = line->width();
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            uint32_t segments = static_cast<uint32_t>(line->point_count()) - 1;
            backend_.draw(segments * 6);
        }

        // Draw markers at each data point if marker style is not None
        if (style.has_marker()) {
            backend_.bind_pipeline(scatter_pipeline_);
            pc.point_size = style.marker_size;
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(line->point_count()));
        }
    } else if (scatter) {
        backend_.bind_pipeline(scatter_pipeline_);
        pc.point_size = scatter->size();
        pc.marker_type = static_cast<uint32_t>(style.marker_style);
        if (pc.marker_type == 0) {
            // ScatterSeries defaults to Circle if no marker explicitly set
            pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
        }
        backend_.push_constants(pc);
        backend_.bind_buffer(gpu.ssbo, 0);
        backend_.draw_instanced(6, static_cast<uint32_t>(scatter->point_count()));
    }
    // 3D series rendering
    else if (line3d) {
        if (line3d->point_count() > 1) {
            backend_.bind_pipeline(line3d_pipeline_);
            pc.line_width = line3d->width();
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            uint32_t segments = static_cast<uint32_t>(line3d->point_count()) - 1;
            backend_.draw(segments * 6);
        }
    } else if (scatter3d) {
        backend_.bind_pipeline(scatter3d_pipeline_);
        pc.point_size = scatter3d->size();
        pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
        backend_.push_constants(pc);
        backend_.bind_buffer(gpu.ssbo, 0);
        backend_.draw_instanced(6, static_cast<uint32_t>(scatter3d->point_count()));
    } else if (surface) {
        if (surface->is_mesh_generated() && gpu.index_buffer) {
            const auto& surf_mesh = surface->mesh();
            backend_.bind_pipeline(surface3d_pipeline_);
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.bind_index_buffer(gpu.index_buffer);
            backend_.draw_indexed(static_cast<uint32_t>(surf_mesh.indices.size()));
        }
    } else if (mesh) {
        if (gpu.index_buffer) {
            backend_.bind_pipeline(mesh3d_pipeline_);
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.bind_index_buffer(gpu.index_buffer);
            backend_.draw_indexed(static_cast<uint32_t>(mesh->indices().size()));
        }
    }
}

void Renderer::build_ortho_projection(float left, float right, float bottom, float top, float* m) {
    // Column-major 4x4 orthographic projection matrix
    // Maps [left,right] x [bottom,top] to [-1,1] x [-1,1]
    std::memset(m, 0, 16 * sizeof(float));

    float rl = right - left;
    float tb = top - bottom;

    if (rl == 0.0f) rl = 1.0f;
    if (tb == 0.0f) tb = 1.0f;

    m[0]  =  2.0f / rl;
    m[5]  = -2.0f / tb;   // Negate for Vulkan Y-down clip space
    m[10] = -1.0f;
    m[12] = -(right + left) / rl;
    m[13] =  (top + bottom) / tb;  // Flip sign for Vulkan
    m[15] =  1.0f;
}


} // namespace plotix
