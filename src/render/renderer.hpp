#pragma once

#include <plotix/fwd.hpp>
#include <plotix/series.hpp>

#include "backend.hpp"
#include "../text/font_atlas.hpp"
#include "../text/text_renderer.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace plotix {

class Renderer {
public:
    explicit Renderer(Backend& backend);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize pipelines for all series types
    bool init();

    // Render a complete figure (starts and ends its own render pass)
    void render_figure(Figure& figure);

    // Split render pass management: call begin, then render_figure_content,
    // then optionally render ImGui, then end.
    void begin_render_pass();
    void render_figure_content(Figure& figure);
    void end_render_pass();

    // Update frame UBO (projection, viewport, time)
    void update_frame_ubo(uint32_t width, uint32_t height, float time);

    // Upload series data to GPU if dirty
    void upload_series_data(Series& series);

    Backend& backend() { return backend_; }

private:
    void render_axes(Axes& axes, const Rect& viewport, uint32_t fig_width, uint32_t fig_height);
    void render_grid(Axes& axes, const Rect& viewport);
    void render_series(Series& series, const Rect& viewport);

    // Build orthographic projection matrix for given axis limits
    void build_ortho_projection(float left, float right, float bottom, float top, float* out_mat4);

    void render_axis_border(Axes& axes, const Rect& viewport,
                            uint32_t fig_width, uint32_t fig_height);
    void render_text_labels(Axes& axes, const Rect& viewport,
                           uint32_t fig_width, uint32_t fig_height);
    void render_legend(Figure& figure, uint32_t fig_width, uint32_t fig_height);

    // Upload text quads and issue draw call; text_color applied via push constants
    void draw_text_batch(const std::vector<TextVertex>& verts,
                         const std::vector<uint32_t>& indices,
                         const Color& text_color);

    Backend& backend_;
    FontAtlas     font_atlas_;
    TextRenderer  text_renderer_;
    TextureHandle font_texture_;

    PipelineHandle line_pipeline_;
    PipelineHandle scatter_pipeline_;
    PipelineHandle grid_pipeline_;
    PipelineHandle text_pipeline_;

    BufferHandle frame_ubo_buffer_;
    BufferHandle grid_vertex_buffer_;
    size_t       grid_buffer_capacity_ = 0;

    BufferHandle border_vertex_buffer_;
    size_t       border_buffer_capacity_ = 0;

    BufferHandle text_vertex_buffer_;
    size_t       text_buffer_capacity_ = 0;
    BufferHandle text_index_buffer_;
    size_t       text_index_capacity_ = 0;

    BufferHandle legend_vertex_buffer_;
    size_t       legend_buffer_capacity_ = 0;

    // Per-series GPU buffers (keyed by series pointer address)
    struct SeriesGpuData {
        BufferHandle ssbo;
        size_t       uploaded_count = 0;
    };
    std::unordered_map<const Series*, SeriesGpuData> series_gpu_data_;
};

} // namespace plotix
