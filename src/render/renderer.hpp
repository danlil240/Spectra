#pragma once

#include <plotix/fwd.hpp>
#include <plotix/series.hpp>

#include "backend.hpp"

#include <cstdint>
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
    Backend& backend_;

    PipelineHandle line_pipeline_;
    PipelineHandle scatter_pipeline_;
    PipelineHandle grid_pipeline_;
    
    // 3D pipelines
    PipelineHandle line3d_pipeline_;
    PipelineHandle scatter3d_pipeline_;
    PipelineHandle mesh3d_pipeline_;
    PipelineHandle surface3d_pipeline_;
    PipelineHandle grid3d_pipeline_;

    BufferHandle frame_ubo_buffer_;
    // Per-axes GPU buffers for grid and border vertices.
    // A single shared buffer is unsafe because all subplot draws are recorded
    // into one command buffer — the host-visible upload for subplot N overwrites
    // the data that subplot N-1's draw command still references.
    struct AxesGpuData {
        BufferHandle grid_buffer;
        size_t       grid_capacity = 0;
        BufferHandle border_buffer;
        size_t       border_capacity = 0;
    };
    std::unordered_map<const Axes*, AxesGpuData> axes_gpu_data_;

    // Per-series GPU buffers (keyed by series pointer address)
    struct SeriesGpuData {
        BufferHandle ssbo;
        size_t       uploaded_count = 0;
    };
    std::unordered_map<const Series*, SeriesGpuData> series_gpu_data_;
};

} // namespace plotix
