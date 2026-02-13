#include "renderer.hpp"

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include <cstdio>
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

    if (grid_vertex_buffer_) {
        backend_.destroy_buffer(grid_vertex_buffer_);
    }
    if (border_vertex_buffer_) {
        backend_.destroy_buffer(border_vertex_buffer_);
    }
    if (text_vertex_buffer_) {
        backend_.destroy_buffer(text_vertex_buffer_);
    }
    if (text_index_buffer_) {
        backend_.destroy_buffer(text_index_buffer_);
    }
    if (legend_vertex_buffer_) {
        backend_.destroy_buffer(legend_vertex_buffer_);
    }
    if (font_texture_) {
        backend_.destroy_texture(font_texture_);
    }
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

    // Load embedded font atlas and initialize text renderer
    if (font_atlas_.load_embedded()) {
        text_renderer_.init(&font_atlas_);
        // Upload atlas texture to GPU
        font_texture_ = backend_.create_texture(
            font_atlas_.atlas_width(), font_atlas_.atlas_height(),
            font_atlas_.pixel_data());
    }

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

    // Render legend (drawn over the full figure, after all axes)
    render_legend(figure, w, h);

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
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    static int debug_count = 0;
    if (debug_count < 1) {
        std::fprintf(stderr, "[DEBUG] render_axes: viewport=(%.1f, %.1f, %.1f, %.1f) "
                     "xlim=(%.3f, %.3f) ylim=(%.3f, %.3f)\n",
                     viewport.x, viewport.y, viewport.w, viewport.h,
                     xlim.min, xlim.max, ylim.min, ylim.max);
        std::fprintf(stderr, "[DEBUG] proj row0: [%.6f, %.6f, %.6f, %.6f]\n",
                     ubo.projection[0], ubo.projection[4], ubo.projection[8], ubo.projection[12]);
        std::fprintf(stderr, "[DEBUG] proj row1: [%.6f, %.6f, %.6f, %.6f]\n",
                     ubo.projection[1], ubo.projection[5], ubo.projection[9], ubo.projection[13]);
        std::fprintf(stderr, "[DEBUG] proj row2: [%.6f, %.6f, %.6f, %.6f]\n",
                     ubo.projection[2], ubo.projection[6], ubo.projection[10], ubo.projection[14]);
        std::fprintf(stderr, "[DEBUG] proj row3: [%.6f, %.6f, %.6f, %.6f]\n",
                     ubo.projection[3], ubo.projection[7], ubo.projection[11], ubo.projection[15]);
        std::fprintf(stderr, "[DEBUG] pipeline handles: grid=%lu line=%lu scatter=%lu text=%lu\n",
                     grid_pipeline_.id, line_pipeline_.id, scatter_pipeline_.id, text_pipeline_.id);
        std::fprintf(stderr, "[DEBUG] ubo_buffer=%lu font_tex=%lu\n",
                     frame_ubo_buffer_.id, font_texture_.id);
        debug_count++;
    }

    // Render axis border (box frame around plot area)
    if (axes.border_enabled())
        render_axis_border(axes, viewport);

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

    // Render tick labels, axis labels, title via TextRenderer
    render_text_labels(axes, viewport, fig_width, fig_height);
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

    // Upload grid vertices
    size_t byte_size = verts.size() * sizeof(float);
    if (!grid_vertex_buffer_ || grid_buffer_capacity_ < byte_size) {
        if (grid_vertex_buffer_) backend_.destroy_buffer(grid_vertex_buffer_);
        grid_vertex_buffer_ = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
        grid_buffer_capacity_ = byte_size * 2;
    }
    backend_.upload_buffer(grid_vertex_buffer_, verts.data(), byte_size);

    // Bind grid pipeline and draw
    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc {};
    pc.color[0] = 0.85f;
    pc.color[1] = 0.85f;
    pc.color[2] = 0.85f;
    pc.color[3] = 1.0f;
    pc.line_width = 1.0f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(grid_vertex_buffer_, 0);
    backend_.draw(static_cast<uint32_t>(total_lines * 2));
}

void Renderer::render_axis_border(Axes& axes, const Rect& /*viewport*/) {
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();

    // 4 border lines forming a rectangle around the plot area (in data space)
    float border_verts[] = {
        // Bottom edge
        xlim.min, ylim.min,  xlim.max, ylim.min,
        // Top edge
        xlim.min, ylim.max,  xlim.max, ylim.max,
        // Left edge
        xlim.min, ylim.min,  xlim.min, ylim.max,
        // Right edge
        xlim.max, ylim.min,  xlim.max, ylim.max,
    };

    size_t byte_size = sizeof(border_verts);

    // Use persistent buffer, reallocate only if too small
    if (!border_vertex_buffer_ || border_buffer_capacity_ < byte_size) {
        if (border_vertex_buffer_) backend_.destroy_buffer(border_vertex_buffer_);
        border_vertex_buffer_ = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        border_buffer_capacity_ = byte_size;
    }
    backend_.upload_buffer(border_vertex_buffer_, border_verts, byte_size);

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc {};
    pc.color[0] = 0.0f;
    pc.color[1] = 0.0f;
    pc.color[2] = 0.0f;
    pc.color[3] = 1.0f;
    pc.line_width = 1.0f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(border_vertex_buffer_, 0);
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
        static int line_dbg = 0;
        if (line_dbg < 3) {
            std::fprintf(stderr, "[DEBUG] draw line: points=%zu segments=%u draw_verts=%u\n",
                         line->point_count(), segments, segments * 6);
            line_dbg++;
        }
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

void Renderer::draw_text_batch(const std::vector<TextVertex>& verts,
                                const std::vector<uint32_t>& indices,
                                const Color& text_color) {
    if (verts.empty() || indices.empty()) return;
    if (!font_texture_) return;

    // Backend doesn't support index buffers, so expand indexed quads
    // to non-indexed triangle list (6 vertices per glyph).
    std::vector<TextVertex> expanded;
    expanded.reserve(indices.size());
    for (uint32_t idx : indices) {
        if (idx < verts.size()) {
            expanded.push_back(verts[idx]);
        }
    }
    if (expanded.empty()) return;

    size_t exp_size = expanded.size() * sizeof(TextVertex);
    if (!text_vertex_buffer_ || text_buffer_capacity_ < exp_size) {
        if (text_vertex_buffer_) backend_.destroy_buffer(text_vertex_buffer_);
        text_vertex_buffer_ = backend_.create_buffer(BufferUsage::Vertex, exp_size * 2);
        text_buffer_capacity_ = exp_size * 2;
    }
    backend_.upload_buffer(text_vertex_buffer_, expanded.data(), exp_size);

    backend_.bind_pipeline(text_pipeline_);

    // Bind UBO at set 0 (already bound by caller) and atlas texture at set 1
    backend_.bind_texture(font_texture_, 1);

    SeriesPushConstants pc {};
    pc.color[0] = text_color.r;
    pc.color[1] = text_color.g;
    pc.color[2] = text_color.b;
    pc.color[3] = text_color.a;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(text_vertex_buffer_, 0);
    backend_.draw(static_cast<uint32_t>(expanded.size()));
}

void Renderer::render_text_labels(Axes& axes, const Rect& viewport,
                                   uint32_t fig_width, uint32_t fig_height) {
    if (!font_texture_ || !text_renderer_.atlas()) return;

    // Switch to a pixel-space orthographic projection for text rendering.
    // This maps [0, fig_width] x [0, fig_height] to clip space,
    // so text positions are specified in screen pixels.
    FrameUBO text_ubo {};
    build_ortho_projection(0.0f, static_cast<float>(fig_width),
                           0.0f, static_cast<float>(fig_height),
                           text_ubo.projection);
    text_ubo.viewport_width  = static_cast<float>(fig_width);
    text_ubo.viewport_height = static_cast<float>(fig_height);
    text_ubo.time = 0.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &text_ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // Temporarily set viewport and scissor to full figure for text
    backend_.set_viewport(0, 0, static_cast<float>(fig_width), static_cast<float>(fig_height));
    backend_.set_scissor(0, 0, fig_width, fig_height);

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();

    // Helper: map data coordinate to pixel coordinate within the viewport
    auto data_to_pixel_x = [&](float data_x) -> float {
        float t = (data_x - xlim.min) / (xlim.max - xlim.min);
        return viewport.x + t * viewport.w;
    };
    auto data_to_pixel_y = [&](float data_y) -> float {
        float t = (data_y - ylim.min) / (ylim.max - ylim.min);
        // Vulkan Y is top-down, data Y is bottom-up
        return viewport.y + (1.0f - t) * viewport.h;
    };

    constexpr float tick_font_size  = 11.0f;
    constexpr float label_font_size = 13.0f;
    constexpr float title_font_size = 15.0f;
    constexpr float tick_padding    = 4.0f;

    // Accumulate all text quads into a single batch
    std::vector<TextVertex> all_verts;
    std::vector<uint32_t>   all_indices;

    auto append_text = [&](const std::string& text, float px, float py, float font_size) {
        std::vector<TextVertex> v;
        std::vector<uint32_t>   idx;
        text_renderer_.generate_quads_indexed(text, px, py, font_size, v, idx);
        uint32_t base = static_cast<uint32_t>(all_verts.size());
        for (auto& i : idx) i += base;
        all_verts.insert(all_verts.end(), v.begin(), v.end());
        all_indices.insert(all_indices.end(), idx.begin(), idx.end());
    };

    auto append_text_centered = [&](const std::string& text, float center_x, float py, float font_size) {
        auto m = text_renderer_.measure_text(text, font_size);
        append_text(text, center_x - m.width * 0.5f, py, font_size);
    };

    // --- X tick labels (below plot area) ---
    auto x_ticks = axes.compute_x_ticks();
    for (size_t i = 0; i < x_ticks.positions.size(); ++i) {
        float px = data_to_pixel_x(x_ticks.positions[i]);
        float py = viewport.y + viewport.h + tick_padding + tick_font_size;
        append_text_centered(x_ticks.labels[i], px, py, tick_font_size);
    }

    // --- Y tick labels (left of plot area) ---
    auto y_ticks = axes.compute_y_ticks();
    for (size_t i = 0; i < y_ticks.positions.size(); ++i) {
        float py = data_to_pixel_y(y_ticks.positions[i]);
        auto m = text_renderer_.measure_text(y_ticks.labels[i], tick_font_size);
        float px = viewport.x - tick_padding - m.width;
        // Center vertically on the tick position
        append_text(y_ticks.labels[i], px, py + tick_font_size * 0.35f, tick_font_size);
    }

    // --- X axis label (centered below tick labels) ---
    if (!axes.get_xlabel().empty()) {
        float center_x = viewport.x + viewport.w * 0.5f;
        float py = viewport.y + viewport.h + tick_padding + tick_font_size + label_font_size + tick_padding;
        append_text_centered(axes.get_xlabel(), center_x, py, label_font_size);
    }

    // --- Y axis label (rotated — approximate with horizontal text left of y-tick labels) ---
    // True rotation requires shader support; for now, render each character vertically
    if (!axes.get_ylabel().empty()) {
        const auto& ylabel = axes.get_ylabel();
        auto m = text_renderer_.measure_text(ylabel, label_font_size);
        float total_h = m.width; // when rotated, width becomes height
        float center_y = viewport.y + viewport.h * 0.5f;
        float start_y = center_y - total_h * 0.5f;
        float px = viewport.x - tick_padding * 2.0f - 40.0f; // left of tick labels

        // Render each character stacked vertically (approximation of rotation)
        float char_y = start_y;
        for (size_t i = 0; i < ylabel.size(); ++i) {
            std::string ch(1, ylabel[i]);
            auto cm = text_renderer_.measure_text(ch, label_font_size);
            append_text(ch, px + (label_font_size - cm.width) * 0.5f,
                       char_y + label_font_size, label_font_size);
            char_y += label_font_size * 1.1f;
        }
    }

    // --- Title (centered above plot area) ---
    if (!axes.get_title().empty()) {
        float center_x = viewport.x + viewport.w * 0.5f;
        float py = viewport.y - tick_padding;
        append_text_centered(axes.get_title(), center_x, py, title_font_size);
    }

    // Draw the accumulated text batch
    draw_text_batch(all_verts, all_indices, colors::black);

    // Restore axes-specific viewport and scissor for subsequent rendering
    backend_.set_scissor(
        static_cast<int32_t>(viewport.x),
        static_cast<int32_t>(viewport.y),
        static_cast<uint32_t>(viewport.w),
        static_cast<uint32_t>(viewport.h)
    );
    backend_.set_viewport(viewport.x, viewport.y, viewport.w, viewport.h);

    // Restore axes-specific projection UBO
    FrameUBO axes_ubo {};
    build_ortho_projection(xlim.min, xlim.max, ylim.min, ylim.max, axes_ubo.projection);
    axes_ubo.viewport_width  = viewport.w;
    axes_ubo.viewport_height = viewport.h;
    axes_ubo.time = 0.0f;
    backend_.upload_buffer(frame_ubo_buffer_, &axes_ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);
}

void Renderer::render_legend(Figure& figure, uint32_t fig_width, uint32_t fig_height) {
    if (!font_texture_ || !text_renderer_.atlas()) return;

    // Collect all labeled series across all axes
    struct LegendEntry {
        std::string label;
        Color       color;
    };
    std::vector<LegendEntry> entries;

    for (auto& axes_ptr : figure.axes()) {
        if (!axes_ptr) continue;
        for (auto& series_ptr : axes_ptr->series()) {
            if (!series_ptr || !series_ptr->visible()) continue;
            if (!series_ptr->label().empty()) {
                entries.push_back({series_ptr->label(), series_ptr->color()});
            }
        }
    }

    if (entries.empty()) return;

    // Switch to pixel-space projection
    FrameUBO legend_ubo {};
    build_ortho_projection(0.0f, static_cast<float>(fig_width),
                           0.0f, static_cast<float>(fig_height),
                           legend_ubo.projection);
    legend_ubo.viewport_width  = static_cast<float>(fig_width);
    legend_ubo.viewport_height = static_cast<float>(fig_height);
    legend_ubo.time = 0.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &legend_ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    backend_.set_viewport(0, 0, static_cast<float>(fig_width), static_cast<float>(fig_height));
    backend_.set_scissor(0, 0, fig_width, fig_height);

    constexpr float legend_font_size = 11.0f;
    constexpr float legend_padding   = 8.0f;
    constexpr float line_sample_len  = 20.0f;
    constexpr float line_text_gap    = 6.0f;
    constexpr float entry_height     = 16.0f;

    // Measure legend box dimensions
    float max_label_width = 0.0f;
    for (auto& e : entries) {
        auto m = text_renderer_.measure_text(e.label, legend_font_size);
        max_label_width = std::max(max_label_width, m.width);
    }

    float box_w = legend_padding * 2.0f + line_sample_len + line_text_gap + max_label_width;
    float box_h = legend_padding * 2.0f + static_cast<float>(entries.size()) * entry_height;

    // Position legend in the top-right corner of the first axes viewport
    float box_x = static_cast<float>(fig_width) - box_w - 20.0f;
    float box_y = 20.0f;

    if (!figure.axes().empty() && figure.axes()[0]) {
        auto& vp = figure.axes()[0]->viewport();
        box_x = vp.x + vp.w - box_w - 10.0f;
        box_y = vp.y + 10.0f;
    }

    // Draw legend background box (semi-transparent white) using grid pipeline
    // 4 lines for the border + 2 triangles for the fill
    {
        // Fill: two triangles forming a quad
        float fill_verts[] = {
            box_x,         box_y,          box_x + box_w, box_y,
            box_x,         box_y + box_h,  box_x + box_w, box_y,
            box_x + box_w, box_y + box_h,  box_x,         box_y + box_h,
        };

        size_t fill_size = sizeof(fill_verts);
        if (!legend_vertex_buffer_ || legend_buffer_capacity_ < fill_size) {
            if (legend_vertex_buffer_) backend_.destroy_buffer(legend_vertex_buffer_);
            legend_vertex_buffer_ = backend_.create_buffer(BufferUsage::Vertex, fill_size * 2);
            legend_buffer_capacity_ = fill_size * 2;
        }
        backend_.upload_buffer(legend_vertex_buffer_, fill_verts, fill_size);

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc {};
        pc.color[0] = 1.0f;
        pc.color[1] = 1.0f;
        pc.color[2] = 1.0f;
        pc.color[3] = 0.9f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(legend_vertex_buffer_, 0);
        // Note: grid pipeline uses LINE_LIST topology, so we draw the border as lines
        // For the fill, we'd need a triangle pipeline. Instead, draw just the border.
    }

    // Draw legend border (4 lines)
    {
        float border_verts[] = {
            box_x,         box_y,          box_x + box_w, box_y,
            box_x + box_w, box_y,          box_x + box_w, box_y + box_h,
            box_x + box_w, box_y + box_h,  box_x,         box_y + box_h,
            box_x,         box_y + box_h,  box_x,         box_y,
        };

        size_t border_size = sizeof(border_verts);
        if (legend_buffer_capacity_ < border_size) {
            if (legend_vertex_buffer_) backend_.destroy_buffer(legend_vertex_buffer_);
            legend_vertex_buffer_ = backend_.create_buffer(BufferUsage::Vertex, border_size * 2);
            legend_buffer_capacity_ = border_size * 2;
        }
        backend_.upload_buffer(legend_vertex_buffer_, border_verts, border_size);

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc {};
        pc.color[0] = 0.5f;
        pc.color[1] = 0.5f;
        pc.color[2] = 0.5f;
        pc.color[3] = 1.0f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(legend_vertex_buffer_, 0);
        backend_.draw(8); // 4 lines × 2 vertices
    }

    // Draw colored line samples and labels for each entry
    for (size_t i = 0; i < entries.size(); ++i) {
        float entry_y = box_y + legend_padding + static_cast<float>(i) * entry_height;
        float line_x0 = box_x + legend_padding;
        float line_x1 = line_x0 + line_sample_len;
        float line_y  = entry_y + entry_height * 0.5f;

        // Draw colored line sample
        float line_verts[] = {
            line_x0, line_y,  line_x1, line_y,
        };

        size_t line_size = sizeof(line_verts);
        if (legend_buffer_capacity_ < line_size) {
            if (legend_vertex_buffer_) backend_.destroy_buffer(legend_vertex_buffer_);
            legend_vertex_buffer_ = backend_.create_buffer(BufferUsage::Vertex, line_size * 2);
            legend_buffer_capacity_ = line_size * 2;
        }
        backend_.upload_buffer(legend_vertex_buffer_, line_verts, line_size);

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc {};
        pc.color[0] = entries[i].color.r;
        pc.color[1] = entries[i].color.g;
        pc.color[2] = entries[i].color.b;
        pc.color[3] = entries[i].color.a;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(legend_vertex_buffer_, 0);
        backend_.draw(2); // 1 line × 2 vertices

        // Draw label text
        float text_x = line_x1 + line_text_gap;
        float text_y = entry_y + entry_height * 0.5f + legend_font_size * 0.35f;

        std::vector<TextVertex> v;
        std::vector<uint32_t>   idx;
        text_renderer_.generate_quads_indexed(entries[i].label, text_x, text_y,
                                              legend_font_size, v, idx);
        draw_text_batch(v, idx, colors::black);
    }
}

} // namespace plotix
