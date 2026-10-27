#pragma once

#include <spectra/figure.hpp>
#include <spectra/axes.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>

#include "backend.hpp"
#include "series_type_registry.hpp"
#include "text_fit.hpp"
#include "text_renderer.hpp"

// Forward declarations
namespace spectra
{
class Axes3D;
class FigureViewModel;
}   // namespace spectra

namespace spectra::ui
{
class ThemeManager;
struct ThemeColors;
}

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace spectra
{

// Draw range (in segments and points) for a 2D polyline SSBO.
struct LineDrawRange
{
    uint32_t first_seg = 0;
    uint32_t seg_count = 0;
    uint32_t first_pt  = 0;
    uint32_t pt_count  = 0;
};

// Compute which segments/points of a polyline to draw.
//   x          — series-local x values (x_data()).
//   n          — number of points actually present on the GPU; clamped to x.size().
//   x_sorted   — whether x is monotonically non-decreasing (enables culling).
//   cull       — whether to cull to [local_x_min, local_x_max].
//   local_x_*  — visible x range in series-local coordinates (absolute - x_offset).
// Without culling (or when the range is degenerate) every point/segment is drawn.
// The result never references indices >= min(n, x.size()).
LineDrawRange compute_line_draw_range(std::span<const float> x,
                                      size_t                 n,
                                      bool                   x_sorted,
                                      bool                   cull,
                                      float                  local_x_min,
                                      float                  local_x_max);

// Layout size and pixel scale for exporting a figure at export_width x export_height.
// An export larger than the figure is a HiDPI export: the figure keeps (about) its own
// layout size and everything is drawn pixel_scale times larger, so line widths, markers
// and text keep their proportions.  An export at or below the figure size re-lays the
// figure out at the export size with pixel_scale 1 (text stays legible in thumbnails).
// When the aspect ratios differ the smaller of the two ratios is used.
struct ExportViewport
{
    uint32_t layout_width  = 0;
    uint32_t layout_height = 0;
    float    pixel_scale   = 1.0f;
};
ExportViewport compute_export_viewport(uint32_t figure_width,
                                       uint32_t figure_height,
                                       uint32_t export_width,
                                       uint32_t export_height);

class Renderer
{
   public:
    explicit Renderer(Backend& backend, ui::ThemeManager& theme_mgr);
    ~Renderer() noexcept;

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize pipelines for all series types
    bool init();

    // Render a complete figure including its text (starts and ends its own render pass)
    void render_figure(Figure& figure);

    // Split render pass management: call begin, then render_figure_content,
    // then optionally render ImGui, then end.
    void begin_render_pass();
    void begin_render_pass(const Color& clear_color);
    void render_figure_content(Figure& figure);
    void render_figure_content(Figure& figure, FigureViewModel* fig_vm);
    void end_render_pass();

    // Upload series data to GPU if dirty
    void upload_series_data(Series& series);
    // Upload series data relative to a double-precision origin (camera-relative rendering).
    // Subtracts (origin_x, origin_y) from each data point in double before converting to float.
    // The origin is in axis space: for log scales, x/y are mapped through
    // to_axis_space() (core/axis_scale.hpp) before the origin is subtracted.
    void upload_series_data(Series&   series,
                            double    origin_x,
                            double    origin_y,
                            ScaleType xscale = ScaleType::Linear,
                            ScaleType yscale = ScaleType::Linear);

    // Notify the renderer that a Series object is about to be destroyed.
    // Moves its GPU resources into a deferred-deletion queue so they are
    // not freed while the GPU is still using them.
    void notify_series_removed(const Series* series);

    // Notify the renderer that an AxesBase object is about to be destroyed.
    // Immediately destroys its GPU buffers (grid, bbox, ticks, arrows) and
    // removes its entry from axes_gpu_data_, preventing stale pointer reuse
    // when the allocator recycles the address for a new AxesBase.
    void notify_axes_removed(const AxesBase* axes);

    // Notify the renderer that a Figure object is about to be destroyed.
    // Immediately destroys its per-figure overlay GPU buffers and removes
    // the entry from figure_gpu_data_, preventing stale pointer reuse.
    void notify_figure_removed(const Figure* figure);

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

    // Labels ellipsized by render_plot_text() since the last begin_render_pass().
    const std::vector<TruncatedTextRegion>& truncated_text_regions() const
    {
        return truncated_text_regions_;
    }

    // Render screen-space plot geometry (2D grid, border, tick marks).
    // Uses Vulkan grid pipeline with a screen-space ortho projection.
    // 3D arrows are rendered inside render_axes() with depth testing.
    void render_plot_geometry(Figure& figure);
    // Text-annotation arrows of one 2D axes (screen-space, called by render_plot_geometry).
    void render_annotation_arrows(Axes& axes, uint32_t slot, float fw, float fh);

    // Screen-space region a figure's background covers: the bounding box of its
    // axes viewports grown by the figure margins, clamped to the figure size
    // (the whole figure when it has no axes).
    static Rect figure_background_region(const Figure& figure);

    // Render queued text (call after render_figure_content, before end_render_pass)
    void render_text(float screen_width, float screen_height);

    // Legends are drawn by the renderer (quads + TextRenderer) unless an
    // interactive legend overlay (the ImGui LegendInteraction) draws them on
    // top of this frame.  Runtimes that build the ImGui overlay set this to
    // true for the frames they render with it; headless export, canvas-only
    // embeds and other overlay-less paths leave it false.  Colorbars are always
    // drawn by the renderer (there is no overlay version).
    void set_legend_overlay_active(bool active) { legend_overlay_active_ = active; }
    bool legend_overlay_active() const { return legend_overlay_active_; }

    // Set the currently selected series for highlight rendering.
    // The renderer draws a thin accent-colored line on top of selected series
    // using the same GPU pipeline — no ImGui overlay needed.
    void set_selected_series(const std::vector<const Series*>& selected);
    void clear_selected_series();

    // Set the series type registry for custom plugin series types.
    // The Renderer subscribes to the registry's unregister notifications so a
    // plugin's per-series GPU state is released (via its cleanup_fn) before the
    // type is removed, i.e. while the plugin library is still loaded.  When
    // detaching from a registry (reg differs from the current one), the state
    // of every custom series of the old registry is released first.
    void set_series_type_registry(SeriesTypeRegistry* reg);

    // Release the plugin-owned GPU state of every custom series (live or
    // pending deferred deletion) of the given type by calling its cleanup_fn
    // now.  The plugin frees its buffers through Backend::destroy_*, which
    // defers the actual GPU destruction until in-flight frames are done;
    // host-owned buffers go through the deletion ring.  Live series are
    // re-uploaded if a type of the same name is registered again.
    // Called automatically by SeriesTypeRegistry::unregister_type().
    // Render thread only.
    void release_custom_type_state(std::string_view type_name);

    // Series kinds the backend could not draw (or drew degraded) during the
    // most recent render_figure_content() call, e.g. 3D series on the
    // experimental WebGPU backend, which has no pipeline for them.  Entries are
    // static strings, in first-seen order.  Always empty on Vulkan.
    const std::vector<std::string_view>& unsupported_series() const { return unsupported_series_; }

    // Text of the in-canvas notice listing the given kinds ("" when empty).
    static std::string format_unsupported_notice(std::span<const std::string_view> kinds);

   private:
    // Returns true if `pipeline` is valid.  Otherwise records `kind` as
    // unsupported for this figure (one log warning per kind) so the caller can
    // skip the draw and render_figure_content() can show a notice.
    bool require_pipeline(PipelineHandle pipeline, std::string_view kind);
    void note_unsupported(std::string_view kind);
    // Queue the in-canvas notice text for unsupported_series_ (if any).
    void render_unsupported_notice(Figure& figure);

    void release_custom_type_state(SeriesTypeEntry& entry);
    void release_all_custom_state();

    // Paint FigureStyle::background over the figure region when the figure
    // overrides the theme canvas colour (the render-pass clear always uses the theme).
    void render_figure_background(Figure& figure);
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
    void render_series(Series&             series,
                       const Rect&         viewport,
                       const VisibleRange* visible = nullptr,
                       double              view_cx = 0.0,
                       double              view_cy = 0.0);
    void render_selection_highlight(AxesBase& axes, const Rect& viewport);

    // Build orthographic projection matrix for given axis limits
    void build_ortho_projection(double left, double right, double bottom, double top, float* m);
    // Build orthographic projection with proper near/far depth (for 3D ortho views)
    void build_ortho_projection_3d(float  left,
                                   float  right,
                                   float  bottom,
                                   float  top,
                                   float  near_clip,
                                   float  far_clip,
                                   float* m);

    void render_arrows(Axes3D& axes, const Rect& viewport);

    // Screen-space plot annotations (render_annotations.cpp): per-axes legend
    // and colorbar, drawn after the series and plot geometry so they sit on top.
    void render_annotations(Figure& figure);
    void build_legend(Axes& axes, const LegendConfig& config);
    void build_colorbar(AxesBase& axes);

    // Upload a screen-space (pixel, Y-down) orthographic projection into the
    // frame UBO and bind it.
    void bind_screen_space_ubo(float fig_width, float fig_height);

    void                render_axis_border(AxesBase&   axes,
                                           const Rect& viewport,
                                           uint32_t    fig_width,
                                           uint32_t    fig_height);
    Backend&            backend_;
    ui::ThemeManager&   theme_mgr_;
    TextRenderer        text_renderer_;
    SeriesTypeRegistry* series_type_registry_    = nullptr;
    uint64_t            series_type_listener_id_ = 0;

    // Labels ellipsized during the current frame (see truncated_text_regions()).
    std::vector<TruncatedTextRegion> truncated_text_regions_;

    // Active FigureViewModel for the current render pass (set by render_figure_content).
    // Enables Phase 2 ViewModel-based rendering: axes limits read via AxesViewModel,
    // series properties (visible, color, opacity) read via SeriesViewModel.
    FigureViewModel* figure_vm_ = nullptr;

    // Colours of the figure's plot surface for the current render_figure_content()
    // call (ThemeManager::plot_surface_colors()); null outside of it.
    const ui::ThemeColors* plot_colors_ = nullptr;
    const ui::ThemeColors& plot_colors() const;

    // See unsupported_series().  unsupported_warned_ persists across frames so
    // each kind is logged only once.
    std::vector<std::string_view> unsupported_series_;
    std::vector<std::string_view> unsupported_warned_;

    PipelineHandle line_pipeline_;
    PipelineHandle scatter_pipeline_;
    PipelineHandle scatter_colormap_pipeline_;
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
    // Must match VulkanBackend::MAX_FRAMES_IN_FLIGHT so each in-flight frame
    // reads from its own GPU buffer instead of racing with the next frame's upload.
    static constexpr uint32_t FRAME_BUFFER_SLOTS = 2;

    // A single shared buffer is unsafe because all subplot draws are recorded
    // into one command buffer — the host-visible upload for subplot N overwrites
    // the data that subplot N-1's draw command still references.
    struct AxesGpuData
    {
        // Per-slot to avoid CPU/GPU hazard: with MAX_FRAMES_IN_FLIGHT=2, frame N
        // reads slot[0] while frame N+1 writes slot[1], and vice-versa.
        BufferHandle grid_buffer[FRAME_BUFFER_SLOTS];
        size_t       grid_capacity[FRAME_BUFFER_SLOTS]     = {};
        uint32_t     grid_vertex_count[FRAME_BUFFER_SLOTS] = {};
        BufferHandle minor_grid_buffer[FRAME_BUFFER_SLOTS];
        size_t       minor_grid_capacity[FRAME_BUFFER_SLOTS] = {};
        BufferHandle border_buffer;
        size_t       border_capacity = 0;
        // 2D text-annotation arrows (screen-space triangles, per-slot)
        BufferHandle annotation_buffer[FRAME_BUFFER_SLOTS];
        size_t       annotation_capacity[FRAME_BUFFER_SLOTS] = {};
        // 3D bounding box edges (12 lines = 24 vec3 vertices).
        // Per-slot (like grid_buffer) so an in-flight frame never reads a
        // buffer that the next frame is overwriting.
        BufferHandle bbox_buffer[FRAME_BUFFER_SLOTS];
        size_t       bbox_capacity[FRAME_BUFFER_SLOTS]     = {};
        uint32_t     bbox_vertex_count[FRAME_BUFFER_SLOTS] = {};
        // 3D tick mark lines (per-slot)
        BufferHandle tick_buffer[FRAME_BUFFER_SLOTS];
        size_t       tick_capacity[FRAME_BUFFER_SLOTS]     = {};
        uint32_t     tick_vertex_count[FRAME_BUFFER_SLOTS] = {};
        // 3D arrow shaft lines + arrowhead triangles
        BufferHandle arrow_line_buffer;
        size_t       arrow_line_capacity     = 0;
        uint32_t     arrow_line_vertex_count = 0;
        BufferHandle arrow_tri_buffer[FRAME_BUFFER_SLOTS];
        size_t       arrow_tri_capacity[FRAME_BUFFER_SLOTS]     = {};
        uint32_t     arrow_tri_vertex_count[FRAME_BUFFER_SLOTS] = {};

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
        // Per-slot caches: each in-flight slot independently tracks whether its
        // buffer contents are still valid, allowing cache reuse when limits are
        // stable while still performing the upload when a slot first activates.
        CachedLimits grid_cache[FRAME_BUFFER_SLOTS];
        CachedLimits bbox_cache[FRAME_BUFFER_SLOTS];
        CachedLimits tick_cache[FRAME_BUFFER_SLOTS];
        int          cached_grid_planes[FRAME_BUFFER_SLOTS] = {};   // for 3D grid plane mask
        // Camera-relative rendering: current view center for 2D axes.
        // Grid and border vertices are generated relative to this origin.
        double view_center_x = 0.0;
        double view_center_y = 0.0;
        // Cached per-frame axis limits — read once at the top of render_axes
        // and reused by all sub-functions to prevent mid-frame inconsistency
        // (e.g. when a ROS subscriber appends data on another thread).
        double cached_xlim_min = 0.0;
        double cached_xlim_max = 1.0;
        double cached_ylim_min = 0.0;
        double cached_ylim_max = 1.0;
        // Scale types and axis-space half ranges used for this frame's
        // projection (equal to the data half ranges on linear axes).
        ScaleType xscale            = ScaleType::Linear;
        ScaleType yscale            = ScaleType::Linear;
        double    axis_half_range_x = 0.5;
        double    axis_half_range_y = 0.5;
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
        Band2D,
        Stem2D,
        Shape2D,
        Shape3D,
        ChunkedLine2D,   // ChunkedLineSeries — demand-loaded, LoD-aware
        Custom,          // Plugin-defined custom series type
    };

    // Per-series GPU buffers (keyed by series pointer address)
    struct SeriesGpuData
    {
        BufferHandle ssbo;
        size_t       uploaded_count = 0;
        size_t       ssbo_capacity =
            0;   // Allocated element capacity (>= uploaded_count); floats for Mesh/Shape3D
        size_t       ssbo_stride_floats = 0;
        BufferHandle index_buffer;
        size_t       index_count = 0;
        BufferHandle fill_buffer;   // Vertex buffer for filled triangles
        size_t       fill_vertex_count    = 0;
        size_t       fill_vertex_capacity = 0;   // Allocated vertex capacity
        BufferHandle outlier_buffer;             // SSBO for box plot outlier points
        size_t       outlier_count    = 0;
        size_t       outlier_capacity = 0;   // Allocated outlier capacity
        SeriesType   type             = SeriesType::Unknown;
        // Camera-relative rendering: double-precision origin subtracted
        // from data during upload.  Eliminates catastrophic cancellation
        // at deep zoom by keeping GPU floats small.
        double origin_x = 0.0;
        double origin_y = 0.0;
        // Scale types the data was mapped through at upload; a change of axis
        // scale forces a re-upload.
        ScaleType upload_xscale = ScaleType::Linear;
        ScaleType upload_yscale = ScaleType::Linear;
        // Upload skip optimisation (WS-5.2)
        uint64_t data_generation = 0;   // Monotonic counter; incremented when data changes
        // Plugin custom series type fields
        std::string custom_type_name;   // Links to SeriesTypeRegistry entry
        // Opaque per-series state owned by the plugin.  The plugin allocates it
        // in upload_fn (which receives this slot by reference) and frees it in
        // cleanup_fn when the series' GPU data is destroyed.
        void* plugin_gpu_state = nullptr;
        // Set when plugin_gpu_state was released because the type was
        // unregistered: forces a re-upload so a type registered again under
        // the same name rebuilds its state from scratch.
        bool plugin_state_released = false;
    };
    std::unordered_map<const Series*, SeriesGpuData> series_gpu_data_;

    // Destroy all GPU buffer handles held by a SeriesGpuData or AxesGpuData.
    void destroy_series_buffers(SeriesGpuData& gpu);
    void destroy_axes_buffers(AxesGpuData& gpu);

    // Currently selected series for GPU-rendered highlight.
    std::vector<const Series*> selected_series_;

    // Reusable scratch buffer for interleaving series data before GPU upload.
    // Avoids per-frame heap allocations for animated/streaming series.
    std::vector<float> upload_scratch_;

    // Reusable scratch buffers for per-frame vertex generation.
    // Avoids heap allocations in render_grid, render_bounding_box, render_tick_marks.
    std::vector<float> grid_scratch_;
    std::vector<float> minor_grid_scratch_;
    std::vector<float> bbox_scratch_;
    std::vector<float> tick_scratch_;
    std::vector<float> arrow_line_scratch_;
    std::vector<float> arrow_tri_scratch_;

    // Per-figure GPU buffers for screen-space overlay (border + tick marks).
    // A single shared buffer is unsafe in split view: all figures render inside
    // ONE command buffer, so figure N's upload overwrites the data that figure
    // N-1's draw command still references.
    struct FigureGpuData
    {
        // Per-slot to avoid CPU/GPU hazard during continuous zoom/resize.
        BufferHandle overlay_line_buffer[FRAME_BUFFER_SLOTS];
        size_t       overlay_line_capacity[FRAME_BUFFER_SLOTS] = {};
        // Legend / colorbar geometry: vec2 vertices (triangles + line lists)
        // and vec2 SSBO points (line and marker swatches).
        BufferHandle annotation_vertex_buffer[FRAME_BUFFER_SLOTS];
        size_t       annotation_vertex_capacity[FRAME_BUFFER_SLOTS] = {};
        BufferHandle annotation_point_buffer[FRAME_BUFFER_SLOTS];
        size_t       annotation_point_capacity[FRAME_BUFFER_SLOTS] = {};
        // Two triangles covering the figure background (override colour only).
        BufferHandle background_buffer[FRAME_BUFFER_SLOTS];
    };
    void                                             destroy_figure_buffers(FigureGpuData& gpu);
    std::unordered_map<const Figure*, FigureGpuData> figure_gpu_data_;

    // Reusable scratch buffer for screen-space overlay vertex generation.
    std::vector<float> overlay_line_scratch_;
    std::vector<float> annotation_tri_scratch_;
    // Per-annotation {first_vertex, vertex_count, rgba} for the arrow draws of one axes.
    struct AnnotationArrowDraw
    {
        uint32_t first;
        uint32_t count;
        float    color[4];
    };
    std::vector<AnnotationArrowDraw> annotation_arrow_draws_;

    // Legend / colorbar annotation state (render_annotations.cpp).
    bool legend_overlay_active_ = false;
    // One recorded draw of the annotation pass.  Vertex kinds index
    // annotation_vertex_scratch_ (in vertices); Segment / Marker index
    // annotation_point_scratch_ (in points).
    struct AnnotationDraw
    {
        enum class Kind : uint8_t
        {
            Triangles,   // overlay pipeline, triangle list
            Lines,       // grid pipeline, line list
            Segment,     // line pipeline, one segment points[first]..points[first+1]
            Marker,      // scatter pipeline, one marker at points[first]
        };
        Kind                kind  = Kind::Triangles;
        uint32_t            first = 0;
        uint32_t            count = 0;
        SeriesPushConstants pc{};
    };
    std::vector<AnnotationDraw> annotation_draws_;
    std::vector<float>          annotation_vertex_scratch_;
    std::vector<float>          annotation_point_scratch_;
    // Last corner chosen for LegendPosition::Best per axes (hysteresis, so a
    // streaming plot's legend does not jump between corners every frame).
    std::unordered_map<const AxesBase*, LegendPosition> legend_best_corner_;

    // Double-buffered deferred deletion: resources removed in frame N are
    // destroyed in frame N+2 (after MAX_FRAMES_IN_FLIGHT fence waits).
    static constexpr uint32_t DELETION_RING_SIZE = 4;   // MAX_FRAMES_IN_FLIGHT + 2
    std::array<std::vector<SeriesGpuData>, DELETION_RING_SIZE> deletion_ring_;
    uint32_t                                                   deletion_ring_write_ = 0;
    // Custom-series pipelines retired by SeriesTypeRegistry::unregister_type,
    // destroyed with the same frame delay as deletion_ring_.
    std::array<std::vector<PipelineHandle>, DELETION_RING_SIZE> pipeline_deletion_ring_;
};

}   // namespace spectra
