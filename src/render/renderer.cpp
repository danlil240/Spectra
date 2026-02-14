#include "renderer.hpp"
#include "../ui/theme.hpp"

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include <cstring>
#include <vector>

namespace plotix {

Renderer::Renderer(Backend& backend)
    : backend_(backend)
{}

Renderer::~Renderer() {
    // Clean up per-series GPU data
    for (auto& [ptr, data] : series_gpu_data_) {
        if (data.ssbo) {
            backend_.destroy_buffer(data.ssbo);
        }
    }
    series_gpu_data_.clear();

    // Clean up per-axes GPU data (grid + border buffers)
    for (auto& [ptr, data] : axes_gpu_data_) {
        if (data.grid_buffer)   backend_.destroy_buffer(data.grid_buffer);
        if (data.border_buffer) backend_.destroy_buffer(data.border_buffer);
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

    // Render each axes
    for (auto& axes_ptr : figure.axes()) {
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
    ubo.viewport_width  = static_cast<float>(width);
    ubo.viewport_height = static_cast<float>(height);
    ubo.time = time;

    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
}

void Renderer::upload_series_data(Series& series) {
    auto* line = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);

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

    auto& gpu = series_gpu_data_[&series];

    // Interleave x,y into vec2 array
    size_t byte_size = count * 2 * sizeof(float);

    // Reallocate if needed
    if (!gpu.ssbo || gpu.uploaded_count < count) {
        if (gpu.ssbo) backend_.destroy_buffer(gpu.ssbo);
        // Allocate with some headroom for streaming appends
        size_t alloc_size = byte_size * 2;  // 2x headroom
        gpu.ssbo = backend_.create_buffer(BufferUsage::Storage, alloc_size);
    }

    // Interleave and upload
    std::vector<float> interleaved(count * 2);
    for (size_t i = 0; i < count; ++i) {
        interleaved[i * 2]     = x_data[i];
        interleaved[i * 2 + 1] = y_data[i];
    }

    backend_.upload_buffer(gpu.ssbo, interleaved.data(), byte_size);
    gpu.uploaded_count = count;
    series.clear_dirty();
}

void Renderer::render_axes(Axes& axes, const Rect& viewport,
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

    // Update frame UBO with axes-specific projection
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();

    FrameUBO ubo {};
    build_ortho_projection(xlim.min, xlim.max, ylim.min, ylim.max, ubo.projection);
    ubo.viewport_width  = viewport.w;
    ubo.viewport_height = viewport.h;
    ubo.time = 0.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // Render axis border (uses same data-space projection, drawn before grid)
    if (axes.border_enabled())
        render_axis_border(axes, viewport, fig_width, fig_height);

    // Render grid
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

void Renderer::render_grid(Axes& axes, const Rect& /*viewport*/) {
    if (!axes.grid_enabled()) return;

    auto x_ticks = axes.compute_x_ticks();
    auto y_ticks = axes.compute_y_ticks();

    size_t num_x = x_ticks.positions.size();
    size_t num_y = y_ticks.positions.size();
    if (num_x == 0 && num_y == 0) return;

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();

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

void Renderer::render_axis_border(Axes& axes, const Rect& viewport,
                                   uint32_t /*fig_width*/, uint32_t /*fig_height*/) {
    // Draw border in data space using the already-bound data-space UBO.
    // Inset vertices by a tiny fraction of the axis range so they don't
    // land exactly on the NDC ±1.0 clip boundary (which causes clipping
    // of the top/right edges on some GPUs).
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();

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
    pc.color[3] = c.a;

    auto* line = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);

    if (line) {
        backend_.bind_pipeline(line_pipeline_);
        pc.line_width = line->width();
        backend_.push_constants(pc);
        backend_.bind_buffer(gpu.ssbo, 0);
        // Each line segment = 6 vertices (2 triangles from quad expansion)
        // Total segments = point_count - 1
        uint32_t segments = static_cast<uint32_t>(line->point_count()) - 1;
        backend_.draw(segments * 6);
    } else if (scatter) {
        backend_.bind_pipeline(scatter_pipeline_);
        pc.point_size = scatter->size();
        backend_.push_constants(pc);
        backend_.bind_buffer(gpu.ssbo, 0);
        // Each point = 6 vertices (quad), instanced
        backend_.draw_instanced(6, static_cast<uint32_t>(scatter->point_count()));
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
