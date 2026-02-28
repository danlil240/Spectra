#pragma once

#include <spectra/fwd.hpp>
#include <spectra/series.hpp>

#include "backend.hpp"
#include "text_renderer.hpp"

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

    Renderer(const Renderer&)            = delete;
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

    // Upload series data to GPU if dirty
    void upload_series_data(Series& series);
    // Upload series data relative to a double-precision origin (camera-relative rendering).
    // Subtracts (origin_x, origin_y) from each data point in double before converting to float.
    void upload_series_data(Series& series, double origin_x, double origin_y);

    // Notify the renderer that a Series object is about to be destroyed.
    // Moves its GPU resources into a deferred-deletion queue so they are
    // not freed while the GPU is still using them.
    void notify_series_removed(const Series* series);

    // Flush the deferred-deletion queue.  Call this at a point where the
    // GPU is guaranteed to have finished all previously submitted work
    // (e.g. right after begin_frame / fence wait).
    void flush_pending_deletions();

    Backend& backend() { return backend_; }

    // Text renderer for Vulkan-based plot text
    TextRenderer&       text_renderer() { return text_renderer_; }
    const TextRenderer& text_renderer() const { return text_renderer_; }

    // Queue all plot text (tick labels, axis labels, titles) for a figure.
    // Uses Vulkan TextRenderer — no ImGui dependency.
    // Must be called after render_figure_content() and before render_text().
    void render_plot_text(Figure& figure);

    // Render screen-space plot geometry (2D tick marks only).
    // Uses Vulkan grid pipeline with a screen-space ortho projection.
    // 3D arrows are rendered inside render_axes() with depth testing.
    void render_plot_geometry(Figure& figure);

    // Render queued text (call after render_figure_content, before end_render_pass)
    void render_text(float screen_width, float screen_height);

    // Set the currently selected series for highlight rendering.
    // The renderer draws a thin accent-colored line on top of selected series
    // using the same GPU pipeline — no ImGui overlay needed.
    void set_selected_series(const std::vector<const Series*>& selected);
    void clear_selected_series();

   private:
    void render_axes(AxesBase& axes, const Rect& viewport, uint32_t fig_width, uint32_t fig_height);
    void render_grid(AxesBase& axes, const Rect& viewport);
    void render_bounding_box(Axes3D& axes, const Rect& viewport);
    void render_tick_marks(Axes3D& axes, const Rect& viewport);
    // Visible x-range for 2D culling (nullopt = draw all)
    struct VisibleRange
    {
        double x_min;
        double x_max;
    };
    void render_series(Series&            series,
                       const Rect&         viewport,
                       const VisibleRange* visible    = nullptr,
                       double              view_cx    = 0.0,
                       double              view_cy    = 0.0);
    void render_selection_highlight(AxesBase& axes, const Rect& viewport);

    // Build orthographic projection matrix for given axis limits
    void build_ortho_projection(double left, double right, double bottom, double top, float* out_mat4);
    // Build orthographic projection with proper near/far depth (for 3D ortho views)
    void build_ortho_projection_3d(float  left,
                                   float  right,
                                   float  bottom,
                                   float  top,
                                   float  near_clip,
                                   float  far_clip,
                                   float* out_mat4);

    void render_arrows(Axes3D& axes, const Rect& viewport);

    void         render_axis_border(AxesBase&   axes,
                                    const Rect& viewport,
                                    uint32_t    fig_width,
                                    uint32_t    fig_height);
    Backend&     backend_;
    TextRenderer text_renderer_;

    PipelineHandle line_pipeline_;
    PipelineHandle scatter_pipeline_;
    PipelineHandle grid_pipeline_;
    PipelineHandle overlay_pipeline_;   // Triangle-list topology for filled shapes (2D arrowheads)
    PipelineHandle stat_fill_pipeline_;   // Triangle-list, vec2+alpha, per-vertex gradient fills
    PipelineHandle arrow3d_pipeline_;     // Triangle-list, vec3, depth test ON (3D arrowheads)

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
        size_t       grid_capacity     = 0;
        uint32_t     grid_vertex_count = 0;
        BufferHandle border_buffer;
        size_t       border_capacity = 0;
        // 3D bounding box edges (12 lines = 24 vec3 vertices)
        BufferHandle bbox_buffer;
        size_t       bbox_capacity     = 0;
        uint32_t     bbox_vertex_count = 0;
        // 3D tick mark lines
        BufferHandle tick_buffer;
        size_t       tick_capacity     = 0;
        uint32_t     tick_vertex_count = 0;
        // 3D arrow shaft lines + arrowhead triangles
        BufferHandle arrow_line_buffer;
        size_t       arrow_line_capacity     = 0;
        uint32_t     arrow_line_vertex_count = 0;
        BufferHandle arrow_tri_buffer;
        size_t       arrow_tri_capacity     = 0;
        uint32_t     arrow_tri_vertex_count = 0;

        // Per-function cached axis limits — each render function (grid, bbox,
        // ticks) needs its own cache because they run sequentially and would
        // clobber a shared cache, causing later functions to skip regeneration.
        struct CachedLimits
        {
            double xmin = 0, xmax = 0;
            double ymin = 0, ymax = 0;
            double zmin = 0, zmax = 0;
            bool   valid = false;
        };
        CachedLimits grid_cache;
        CachedLimits bbox_cache;
        CachedLimits tick_cache;
        int          cached_grid_planes = 0;   // for 3D grid plane mask
        // Camera-relative rendering: current view center for 2D axes.
        // Grid and border vertices are generated relative to this origin.
        double view_center_x = 0.0;
        double view_center_y = 0.0;
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
        BoxPlot2D,
        Violin2D,
        Histogram2D,
        Bar2D,
    };

    // Per-series GPU buffers (keyed by series pointer address)
    struct SeriesGpuData
    {
        BufferHandle ssbo;
        size_t       uploaded_count = 0;
        BufferHandle index_buffer;
        size_t       index_count = 0;
        BufferHandle fill_buffer;   // Vertex buffer for filled triangles
        size_t       fill_vertex_count = 0;
        BufferHandle outlier_buffer;   // SSBO for box plot outlier points
        size_t       outlier_count = 0;
        SeriesType   type          = SeriesType::Unknown;
        // Camera-relative rendering: double-precision origin subtracted
        // from data during upload.  Eliminates catastrophic cancellation
        // at deep zoom by keeping GPU floats small.
        double origin_x = 0.0;
        double origin_y = 0.0;
    };
    std::unordered_map<const Series*, SeriesGpuData> series_gpu_data_;

    // Currently selected series for GPU-rendered highlight.
    std::vector<const Series*> selected_series_;

    // Reusable scratch buffer for interleaving series data before GPU upload.
    // Avoids per-frame heap allocations for animated/streaming series.
    std::vector<float> upload_scratch_;

    // Reusable scratch buffers for per-frame vertex generation.
    // Avoids heap allocations in render_grid, render_bounding_box, render_tick_marks.
    std::vector<float> grid_scratch_;
    std::vector<float> bbox_scratch_;
    std::vector<float> tick_scratch_;
    std::vector<float> arrow_line_scratch_;
    std::vector<float> arrow_tri_scratch_;

    // Screen-space overlay geometry buffers (tick marks, arrow lines, arrowheads)
    BufferHandle       overlay_line_buffer_;   // Line-list vertices (tick marks + arrow shafts)
    size_t             overlay_line_capacity_ = 0;
    BufferHandle       overlay_tri_buffer_;   // Triangle-list vertices (arrowheads)
    size_t             overlay_tri_capacity_ = 0;
    std::vector<float> overlay_line_scratch_;
    std::vector<float> overlay_tri_scratch_;

    // Double-buffered deferred deletion: resources removed in frame N are
    // destroyed in frame N+2 (after MAX_FRAMES_IN_FLIGHT fence waits).
    static constexpr uint32_t DELETION_RING_SIZE = 4;   // MAX_FRAMES_IN_FLIGHT + 2
    std::array<std::vector<SeriesGpuData>, DELETION_RING_SIZE> deletion_ring_;
    uint32_t                                                   deletion_ring_write_ = 0;
};

}   // namespace spectra
