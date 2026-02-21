#pragma once

#include <spectra/fwd.hpp>
#include <spectra/series.hpp>

#include "backend.hpp"

// Forward declarations
namespace spectra
{
class Axes3D;
}

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace spectra
{

class Renderer
{
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

    // Notify the renderer that a Series object is about to be destroyed.
    // Moves its GPU resources into a deferred-deletion queue so they are
    // not freed while the GPU is still using them.
    void notify_series_removed(const Series* series);

    // Flush the deferred-deletion queue.  Call this at a point where the
    // GPU is guaranteed to have finished all previously submitted work
    // (e.g. right after begin_frame / fence wait).
    void flush_pending_deletions();

    Backend& backend() { return backend_; }

   private:
    void render_axes(AxesBase& axes, const Rect& viewport, uint32_t fig_width, uint32_t fig_height);
    void render_grid(AxesBase& axes, const Rect& viewport);
    void render_bounding_box(Axes3D& axes, const Rect& viewport);
    void render_tick_marks(Axes3D& axes, const Rect& viewport);
    void render_series(Series& series, const Rect& viewport);

    // Build orthographic projection matrix for given axis limits
    void build_ortho_projection(float left, float right, float bottom, float top, float* out_mat4);
    // Build orthographic projection with proper near/far depth (for 3D ortho views)
    void build_ortho_projection_3d(float left,
                                   float right,
                                   float bottom,
                                   float top,
                                   float near_clip,
                                   float far_clip,
                                   float* out_mat4);

    void render_axis_border(AxesBase& axes,
                            const Rect& viewport,
                            uint32_t fig_width,
                            uint32_t fig_height);
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
    PipelineHandle grid_overlay3d_pipeline_;

    // Wireframe 3D pipelines (line topology)
    PipelineHandle surface_wireframe3d_pipeline_;
    PipelineHandle surface_wireframe3d_transparent_pipeline_;

    // Transparent 3D pipelines (depth test ON, depth write OFF)
    PipelineHandle line3d_transparent_pipeline_;
    PipelineHandle scatter3d_transparent_pipeline_;
    PipelineHandle mesh3d_transparent_pipeline_;
    PipelineHandle surface3d_transparent_pipeline_;

    BufferHandle frame_ubo_buffer_;
    // Per-axes GPU buffers for grid and border vertices.
    // A single shared buffer is unsafe because all subplot draws are recorded
    // into one command buffer — the host-visible upload for subplot N overwrites
    // the data that subplot N-1's draw command still references.
    struct AxesGpuData
    {
        BufferHandle grid_buffer;
        size_t grid_capacity = 0;
        uint32_t grid_vertex_count = 0;
        BufferHandle border_buffer;
        size_t border_capacity = 0;
        // 3D bounding box edges (12 lines = 24 vec3 vertices)
        BufferHandle bbox_buffer;
        size_t bbox_capacity = 0;
        uint32_t bbox_vertex_count = 0;
        // 3D tick mark lines
        BufferHandle tick_buffer;
        size_t tick_capacity = 0;
        uint32_t tick_vertex_count = 0;

        // Cached axis limits — skip regeneration when unchanged.
        float cached_xmin = 0, cached_xmax = 0;
        float cached_ymin = 0, cached_ymax = 0;
        float cached_zmin = 0, cached_zmax = 0;
        bool grid_valid = false;
        bool bbox_valid = false;
        bool tick_valid = false;
        bool grid_enabled_cached = false;
        int cached_grid_planes = 0;  // for 3D grid plane mask
    };
    std::unordered_map<const AxesBase*, AxesGpuData> axes_gpu_data_;

    // Cached series type — avoids 6x dynamic_cast per series per frame.
    enum class SeriesType : uint8_t
    {
        Unknown = 0,
        Line2D,
        Scatter2D,
        Line3D,
        Scatter3D,
        Surface3D,
        Mesh3D,
    };

    // Per-series GPU buffers (keyed by series pointer address)
    struct SeriesGpuData
    {
        BufferHandle ssbo;
        size_t uploaded_count = 0;
        BufferHandle index_buffer;
        size_t index_count = 0;
        SeriesType type = SeriesType::Unknown;
    };
    std::unordered_map<const Series*, SeriesGpuData> series_gpu_data_;

    // Reusable scratch buffer for interleaving series data before GPU upload.
    // Avoids per-frame heap allocations for animated/streaming series.
    std::vector<float> upload_scratch_;

    // Reusable scratch buffers for per-frame vertex generation.
    // Avoids heap allocations in render_grid, render_bounding_box, render_tick_marks.
    std::vector<float> grid_scratch_;
    std::vector<float> bbox_scratch_;
    std::vector<float> tick_scratch_;

    // Double-buffered deferred deletion: resources removed in frame N are
    // destroyed in frame N+2 (after MAX_FRAMES_IN_FLIGHT fence waits).
    static constexpr uint32_t DELETION_RING_SIZE = 4;  // MAX_FRAMES_IN_FLIGHT + 2
    std::array<std::vector<SeriesGpuData>, DELETION_RING_SIZE> deletion_ring_;
    uint32_t deletion_ring_write_ = 0;
};

}  // namespace spectra
