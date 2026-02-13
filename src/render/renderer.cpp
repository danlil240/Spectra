#include "renderer.hpp"

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

    if (frame_ubo_buffer_) {
        backend_.destroy_buffer(frame_ubo_buffer_);
    }
}

bool Renderer::init() {
    // Create pipelines for each series type
    line_pipeline_    = backend_.create_pipeline(PipelineType::Line);
    scatter_pipeline_ = backend_.create_pipeline(PipelineType::Scatter);
    grid_pipeline_    = backend_.create_pipeline(PipelineType::Grid);
    text_pipeline_    = backend_.create_pipeline(PipelineType::Text);

    // Create frame UBO buffer
    frame_ubo_buffer_ = backend_.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));

    return true;
}

void Renderer::render_figure(Figure& figure) {
    uint32_t w = figure.width();
    uint32_t h = figure.height();

    // NOTE: begin_frame()/end_frame() are managed by App, not the renderer.
    // The renderer only records drawing commands within an active frame.

    backend_.begin_render_pass(colors::white);

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

    backend_.end_render_pass();
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

    // Render grid
    render_grid(axes, viewport);

    // Render each series
    for (auto& series_ptr : axes.series()) {
        if (!series_ptr) continue;

        if (series_ptr->is_dirty()) {
            upload_series_data(*series_ptr);
        }

        render_series(*series_ptr, viewport);
    }

    // TODO: Render tick labels, axis labels, title via TextRenderer
}

void Renderer::render_grid(Axes& axes, const Rect& /*viewport*/) {
    if (!axes.grid_enabled()) return;

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc {};
    pc.color[0] = 0.85f;
    pc.color[1] = 0.85f;
    pc.color[2] = 0.85f;
    pc.color[3] = 1.0f;
    pc.line_width = 1.0f;
    backend_.push_constants(pc);

    // Grid lines are generated from tick positions
    auto x_ticks = axes.compute_x_ticks();
    auto y_ticks = axes.compute_y_ticks();

    // Each grid line = 2 vertices (start, end) = 1 line segment
    // Total vertices = (x_ticks + y_ticks) * 2
    uint32_t total_lines = static_cast<uint32_t>(x_ticks.positions.size() + y_ticks.positions.size());
    if (total_lines == 0) return;

    // TODO: Upload grid line vertices to a buffer and draw
    // For now, this is a placeholder — grid rendering requires
    // the grid pipeline to be fully wired with shaders (Agent 3)
    (void)total_lines;
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
    m[5]  =  2.0f / tb;
    m[10] = -1.0f;
    m[12] = -(right + left) / rl;
    m[13] = -(top + bottom) / tb;
    m[15] =  1.0f;
}

} // namespace plotix
