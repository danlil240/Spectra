#include "renderer.hpp"

#include <cmath>

#include <algorithm>
#include <cstring>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_shapes3d.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "core/axis_scale.hpp"
#include "ui/imgui/axes3d_renderer.hpp"
#include "ui/theme/theme.hpp"
#include "ui/viewmodel/axes_view_model.hpp"
#include "ui/viewmodel/figure_view_model.hpp"
#include "ui/viewmodel/series_view_model.hpp"
#include "ui/workspace/plugin_guard.hpp"

namespace spectra
{

Renderer::Renderer(Backend& backend, ui::ThemeManager& theme_mgr)
    : backend_(backend), theme_mgr_(theme_mgr)
{
}

void Renderer::destroy_series_buffers(SeriesGpuData& gpu)
{
    if (gpu.ssbo)
        backend_.destroy_buffer(gpu.ssbo);
    if (gpu.index_buffer)
        backend_.destroy_buffer(gpu.index_buffer);
    if (gpu.fill_buffer)
        backend_.destroy_buffer(gpu.fill_buffer);
    if (gpu.outlier_buffer)
        backend_.destroy_buffer(gpu.outlier_buffer);

    // Clean up plugin GPU state for custom series types.
    if (gpu.type == SeriesType::Custom && gpu.plugin_gpu_state && series_type_registry_)
    {
        // Owning lookup keeps the callbacks alive for the duration of the call.
        auto entry = series_type_registry_->find_shared(gpu.custom_type_name);
        if (entry && entry->cleanup_fn)
        {
            auto result =
                plugin_guard_invoke(entry->type_name.c_str(),
                                    [&]() { entry->cleanup_fn(backend_, gpu.plugin_gpu_state); });
            if (result != PluginCallResult::Success)
            {
                entry->faulted = true;
            }
        }
        gpu.plugin_gpu_state = nullptr;
    }
}

void Renderer::set_series_type_registry(SeriesTypeRegistry* reg)
{
    if (reg == series_type_registry_)
        return;
    if (series_type_registry_)
    {
        series_type_registry_->remove_unregister_listener(series_type_listener_id_);
        series_type_listener_id_ = 0;
        // Plugins whose types live in the old registry may be unloaded once we
        // detach: release their per-series state while their code is loaded.
        release_all_custom_state();
    }
    series_type_registry_ = reg;
    if (series_type_registry_)
    {
        series_type_listener_id_ = series_type_registry_->add_unregister_listener(
            [this](SeriesTypeEntry& entry) { release_custom_type_state(entry); });
    }
}

void Renderer::release_custom_type_state(std::string_view type_name)
{
    if (!series_type_registry_)
        return;
    auto entry = series_type_registry_->find_shared(std::string(type_name));
    if (entry)
        release_custom_type_state(*entry);
}

void Renderer::release_custom_type_state(SeriesTypeEntry& entry)
{
    auto release = [&](SeriesGpuData& gpu)
    {
        if (gpu.plugin_gpu_state && entry.cleanup_fn)
        {
            auto result =
                plugin_guard_invoke(entry.type_name.c_str(),
                                    [&]() { entry.cleanup_fn(backend_, gpu.plugin_gpu_state); });
            if (result != PluginCallResult::Success)
                entry.faulted = true;
        }
        gpu.plugin_gpu_state = nullptr;
    };
    auto matches = [&](const SeriesGpuData& gpu)
    { return gpu.type == SeriesType::Custom && gpu.custom_type_name == entry.type_name; };

    for (auto& [series, gpu] : series_gpu_data_)
    {
        if (!matches(gpu))
            continue;
        release(gpu);
        // Host-owned buffers (if any) go through the deferred deletion ring.
        if (gpu.ssbo || gpu.index_buffer || gpu.fill_buffer || gpu.outlier_buffer)
        {
            SeriesGpuData retired;
            retired.ssbo           = gpu.ssbo;
            retired.index_buffer   = gpu.index_buffer;
            retired.fill_buffer    = gpu.fill_buffer;
            retired.outlier_buffer = gpu.outlier_buffer;
            deletion_ring_[deletion_ring_write_].push_back(std::move(retired));
            gpu.ssbo                 = BufferHandle{};
            gpu.index_buffer         = BufferHandle{};
            gpu.fill_buffer          = BufferHandle{};
            gpu.outlier_buffer       = BufferHandle{};
            gpu.ssbo_capacity        = 0;
            gpu.uploaded_count       = 0;
            gpu.index_count          = 0;
            gpu.fill_vertex_count    = 0;
            gpu.fill_vertex_capacity = 0;
            gpu.outlier_count        = 0;
            gpu.outlier_capacity     = 0;
        }
        gpu.plugin_state_released = true;
    }

    // Series removed earlier whose state is still waiting in the deletion
    // ring: their cleanup must run now too.  Otherwise the flush would run
    // after the plugin is unloaded (leak), or would hand this plugin's state
    // to a different plugin that registered the same type name.
    for (auto& slot : deletion_ring_)
    {
        for (auto& gpu : slot)
        {
            if (matches(gpu))
                release(gpu);
        }
    }
}

void Renderer::release_all_custom_state()
{
    if (!series_type_registry_)
        return;
    for (const auto& name : series_type_registry_->type_names())
        release_custom_type_state(name);
}

void Renderer::destroy_axes_buffers(AxesGpuData& gpu)
{
    for (uint32_t s = 0; s < FRAME_BUFFER_SLOTS; ++s)
    {
        if (gpu.grid_buffer[s])
            backend_.destroy_buffer(gpu.grid_buffer[s]);
        if (gpu.minor_grid_buffer[s])
            backend_.destroy_buffer(gpu.minor_grid_buffer[s]);
        if (gpu.bbox_buffer[s])
            backend_.destroy_buffer(gpu.bbox_buffer[s]);
        if (gpu.tick_buffer[s])
            backend_.destroy_buffer(gpu.tick_buffer[s]);
        if (gpu.arrow_tri_buffer[s])
            backend_.destroy_buffer(gpu.arrow_tri_buffer[s]);
        if (gpu.annotation_buffer[s])
            backend_.destroy_buffer(gpu.annotation_buffer[s]);
    }
    if (gpu.border_buffer)
        backend_.destroy_buffer(gpu.border_buffer);
    if (gpu.arrow_line_buffer)
        backend_.destroy_buffer(gpu.arrow_line_buffer);
}

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

void Renderer::notify_axes_removed(const AxesBase* axes)
{
    auto it = axes_gpu_data_.find(axes);
    if (it != axes_gpu_data_.end())
    {
        // Axes GPU buffers (grid, bbox, ticks, arrows) are not referenced by
        // in-flight command buffers once the frame has been submitted — they
        // are re-uploaded every frame and never held across frames by the GPU.
        // It is safe to destroy them immediately (no deferred deletion needed).
        destroy_axes_buffers(it->second);
        axes_gpu_data_.erase(it);
    }
    legend_best_corner_.erase(axes);
}

void Renderer::notify_figure_removed(const Figure* figure)
{
    // Drop "Best" legend hysteresis for the figure's 2D axes too, so a later
    // axes allocated at a recycled address does not inherit a stale corner.
    if (figure)
    {
        for (const auto& axes_ptr : figure->axes())
            legend_best_corner_.erase(axes_ptr.get());
    }
    auto it = figure_gpu_data_.find(figure);
    if (it != figure_gpu_data_.end())
    {
        destroy_figure_buffers(it->second);
        figure_gpu_data_.erase(it);
    }
}

void Renderer::destroy_figure_buffers(FigureGpuData& gpu)
{
    for (uint32_t i = 0; i < FRAME_BUFFER_SLOTS; ++i)
    {
        for (BufferHandle* b : {&gpu.overlay_line_buffer[i],
                                &gpu.annotation_vertex_buffer[i],
                                &gpu.annotation_point_buffer[i],
                                &gpu.background_buffer[i]})
        {
            if (*b)
                backend_.destroy_buffer(*b);
            *b = BufferHandle{};
        }
    }
}

void Renderer::flush_pending_deletions()
{
    // Destroy the oldest slot — these resources were queued DELETION_RING_SIZE
    // frames ago, so the GPU is guaranteed to be done with them.
    uint32_t destroy_slot = (deletion_ring_write_ + 1) % DELETION_RING_SIZE;
    auto&    slot         = deletion_ring_[destroy_slot];
    for (auto& gpu : slot)
    {
        destroy_series_buffers(gpu);
    }
    slot.clear();

    auto& pipeline_slot = pipeline_deletion_ring_[destroy_slot];
    for (auto& pipeline : pipeline_slot)
        backend_.destroy_pipeline(pipeline);
    pipeline_slot.clear();

    // Advance write pointer to the slot we just freed.
    deletion_ring_write_ = destroy_slot;

    // Custom-series pipelines retired since the last flush may still be
    // referenced by in-flight command buffers: queue them in the new write
    // slot so they are destroyed DELETION_RING_SIZE - 1 frames from now.
    if (series_type_registry_)
    {
        for (auto& pipeline : series_type_registry_->take_retired_pipelines())
            pipeline_deletion_ring_[deletion_ring_write_].push_back(pipeline);
    }
}

void Renderer::render_text(float screen_width, float screen_height)
{
    if (!text_renderer_.is_initialized())
        return;

    // Set full-screen viewport and scissor for text rendering
    backend_.set_viewport(0, 0, screen_width, screen_height);
    backend_.set_scissor(0,
                         0,
                         static_cast<uint32_t>(screen_width),
                         static_cast<uint32_t>(screen_height));

    // Flush depth-tested 3D text first (uses depth buffer from 3D geometry)
    text_renderer_.flush_depth(backend_, screen_width, screen_height);

    // Then flush 2D text (no depth test, always on top)
    text_renderer_.flush(backend_, screen_width, screen_height);
}

Renderer::~Renderer() noexcept
{
    try
    {
        // Wait for GPU to finish using all resources before destroying them
        backend_.wait_idle();

        // Stop receiving unregister notifications (the registry may outlive us).
        if (series_type_registry_ && series_type_listener_id_ != 0)
        {
            series_type_registry_->remove_unregister_listener(series_type_listener_id_);
            series_type_listener_id_ = 0;
        }

        // Shutdown text renderer
        text_renderer_.shutdown(backend_);

        // Flush ALL deferred deletion ring slots
        for (auto& slot : deletion_ring_)
        {
            for (auto& gpu : slot)
                destroy_series_buffers(gpu);
            slot.clear();
        }
        for (auto& slot : pipeline_deletion_ring_)
        {
            for (auto& pipeline : slot)
                backend_.destroy_pipeline(pipeline);
            slot.clear();
        }

        // Clean up per-series GPU data
        for (auto& [ptr, data] : series_gpu_data_)
            destroy_series_buffers(data);
        series_gpu_data_.clear();

        // Clean up per-axes GPU data (grid + border + bbox + tick buffers)
        for (auto& [ptr, data] : axes_gpu_data_)
            destroy_axes_buffers(data);
        axes_gpu_data_.clear();

        // Clean up per-figure overlay buffers
        for (auto& [ptr, data] : figure_gpu_data_)
            destroy_figure_buffers(data);
        figure_gpu_data_.clear();

        // Destroy custom series type pipelines
        if (series_type_registry_)
        {
            series_type_registry_->destroy_pipelines(backend_);
        }

        if (frame_ubo_buffer_)
        {
            backend_.destroy_buffer(frame_ubo_buffer_);
        }

    }   // try
    catch (const std::exception& e)
    {
        // Swallow exceptions during renderer cleanup.
        // On CI with lavapipe (software Vulkan), non-deterministic
        // std::system_error can occur during Vulkan resource teardown.
        (void)e;
    }
    catch (...)
    {
    }
}

bool Renderer::init()
{
    // Create pipelines for each series type
    line_pipeline_             = backend_.create_pipeline(PipelineType::Line);
    scatter_pipeline_          = backend_.create_pipeline(PipelineType::Scatter);
    scatter_colormap_pipeline_ = backend_.create_pipeline(PipelineType::ScatterColormap);
    grid_pipeline_             = backend_.create_pipeline(PipelineType::Grid);
    overlay_pipeline_          = backend_.create_pipeline(PipelineType::Overlay);
    stat_fill_pipeline_        = backend_.create_pipeline(PipelineType::StatFill);

    // Create 3D pipelines
    line3d_pipeline_         = backend_.create_pipeline(PipelineType::Line3D);
    scatter3d_pipeline_      = backend_.create_pipeline(PipelineType::Scatter3D);
    mesh3d_pipeline_         = backend_.create_pipeline(PipelineType::Mesh3D);
    surface3d_pipeline_      = backend_.create_pipeline(PipelineType::Surface3D);
    grid3d_pipeline_         = backend_.create_pipeline(PipelineType::Grid3D);
    grid_overlay3d_pipeline_ = backend_.create_pipeline(PipelineType::GridOverlay3D);
    arrow3d_pipeline_        = backend_.create_pipeline(PipelineType::Arrow3D);

    // Create wireframe 3D pipelines (line topology)
    surface_wireframe3d_pipeline_ = backend_.create_pipeline(PipelineType::SurfaceWireframe3D);
    surface_wireframe3d_transparent_pipeline_ =
        backend_.create_pipeline(PipelineType::SurfaceWireframe3D_Transparent);

    // Create transparent 3D pipelines (depth test ON, depth write OFF)
    line3d_transparent_pipeline_    = backend_.create_pipeline(PipelineType::Line3D_Transparent);
    scatter3d_transparent_pipeline_ = backend_.create_pipeline(PipelineType::Scatter3D_Transparent);
    mesh3d_transparent_pipeline_    = backend_.create_pipeline(PipelineType::Mesh3D_Transparent);
    surface3d_transparent_pipeline_ = backend_.create_pipeline(PipelineType::Surface3D_Transparent);

    // Create pipelines for custom plugin series types
    if (series_type_registry_)
    {
        series_type_registry_->create_pipelines(backend_);
    }

    // Create frame UBO buffer
    frame_ubo_buffer_ = backend_.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));

    // Initialize text renderer — prefer embedded font data (zero file dependencies),
    // fall back to disk paths for development builds.
#if __has_include("inter_font_embedded.hpp")
    {
    #include "inter_font_embedded.hpp"
        if (text_renderer_.init(backend_, InterFont_ttf_data, InterFont_ttf_size))
        {
            SPECTRA_LOG_INFO("renderer", "TextRenderer initialized from embedded font data");
        }
        else
        {
            SPECTRA_LOG_WARN("renderer",
                             "TextRenderer init from embedded data failed — trying disk");
        }
    }
#endif
    if (!text_renderer_.is_initialized())
    {
        const char* font_paths[] = {
            "third_party/Inter-Regular.ttf",
            "../third_party/Inter-Regular.ttf",
            "../../third_party/Inter-Regular.ttf",
            "../../../third_party/Inter-Regular.ttf",
        };
        for (const char* path : font_paths)
        {
            if (text_renderer_.init_from_file(backend_, path))
            {
                SPECTRA_LOG_INFO("renderer", std::string("TextRenderer initialized from ") + path);
                break;
            }
        }
    }
    if (!text_renderer_.is_initialized())
    {
        SPECTRA_LOG_WARN("renderer", "TextRenderer init failed — plot text will not be rendered");
    }

    return true;
}

void Renderer::begin_render_pass()
{
    // NOTE: flush_pending_deletions() is called from App::run() right after
    // begin_frame() succeeds, NOT here.  This ensures the fence wait has
    // completed before any GPU resources are freed.

    text_renderer_.begin_frame_recording(backend_.current_flight_frame());
    truncated_text_regions_.clear();

    const auto& theme_colors = theme_mgr_.colors();
    Color       bg_color     = Color(theme_colors.bg_canvas.r,
                           theme_colors.bg_canvas.g,
                           theme_colors.bg_canvas.b,
                           theme_colors.bg_canvas.a);
    backend_.begin_render_pass(bg_color);
    backend_.set_line_width(1.0f);   // Set default for VK_DYNAMIC_STATE_LINE_WIDTH
}

void Renderer::begin_render_pass(const Color& clear_color)
{
    text_renderer_.begin_frame_recording(backend_.current_flight_frame());
    truncated_text_regions_.clear();
    backend_.begin_render_pass(clear_color);
    backend_.set_line_width(1.0f);
}

void Renderer::render_figure_content(Figure& figure)
{
    render_figure_content(figure, nullptr);
}

void Renderer::render_figure_content(Figure& figure, FigureViewModel* fig_vm)
{
    figure_vm_ = fig_vm;
    unsupported_series_.clear();

    // Auto-coloured series follow the active theme's data palette (P0-6) and
    // format-letter colours ("r--") its dark / light shade set (P1-5).
    // Auto colours are kept visible on the canvas the plot area renders with (S07).
    ui::resolve_auto_series_colors(
        figure,
        theme_mgr_.current_data_palette(),
        ui::is_light_plot_background(theme_mgr_.colors()),
        ui::effective_figure_background(figure.style(), theme_mgr_.colors()));

    uint32_t w = figure.width();
    uint32_t h = figure.height();

    // Set full-figure viewport and scissor
    backend_.set_viewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    backend_.set_scissor(0, 0, w, h);

    // A figure that overrides the theme canvas colour paints its own region
    // before any axes content.
    render_figure_background(figure);

    // Wire up deferred-deletion callback on every axes so that
    // clear_series() / remove_series() safely defer GPU cleanup.
    // Only install the renderer-only fallback if no callback is already set;
    // WindowRuntime::wire_series_callbacks() installs a richer callback that
    // also notifies DataInteraction and ImGuiIntegration, and we must not
    // overwrite it — doing so would leave stale Series* pointers in the UI.
    auto removal_cb = [this](const Series* s) { notify_series_removed(s); };

    // Render each 2D axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto& ax = *axes_ptr;
        if (!ax.has_series_removed_callback())
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
        if (!ax.has_series_removed_callback())
            ax.set_series_removed_callback(removal_cb);
        const auto& vp = ax.viewport();

        // Box, grid and ticks of a 3D axes need the 3D grid pipeline.
        if (dynamic_cast<Axes3D*>(&ax))
            require_pipeline(grid3d_pipeline_, "3D axes");

        render_axes(ax, vp, w, h);
    }

    // Queue all plot text (tick labels, axis labels, titles) via Vulkan TextRenderer.
    // Flushed later by render_text().
    render_plot_text(figure);
    render_unsupported_notice(figure);

    // Render screen-space plot geometry (2D grid, border, tick marks) via Vulkan grid pipeline.
    // 3D arrows are rendered inside render_axes() with depth testing.
    render_plot_geometry(figure);

    // Legends and colorbars on top of everything else (text is queued for
    // render_text()).
    render_annotations(figure);
}

bool Renderer::require_pipeline(PipelineHandle pipeline, std::string_view kind)
{
    if (pipeline)
        return true;
    note_unsupported(kind);
    return false;
}

void Renderer::note_unsupported(std::string_view kind)
{
    if (std::find(unsupported_series_.begin(), unsupported_series_.end(), kind)
        == unsupported_series_.end())
        unsupported_series_.push_back(kind);

    if (std::find(unsupported_warned_.begin(), unsupported_warned_.end(), kind)
        == unsupported_warned_.end())
    {
        unsupported_warned_.push_back(kind);
        SPECTRA_LOG_WARN("renderer",
                         "{} not supported by this render backend (no pipeline); "
                         "see the in-canvas notice",
                         kind);
    }
}

std::string Renderer::format_unsupported_notice(std::span<const std::string_view> kinds)
{
    if (kinds.empty())
        return {};
    std::string text = "Not supported by this render backend: ";
    for (size_t i = 0; i < kinds.size(); ++i)
    {
        if (i > 0)
            text += ", ";
        text += kinds[i];
    }
    return text;
}

void Renderer::render_unsupported_notice(Figure& figure)
{
    if (unsupported_series_.empty() || !text_renderer_.is_initialized())
        return;

    const auto& c = theme_mgr_.colors().warning;
    auto        r = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
    auto        g = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
    auto        b = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    uint32_t    rgba =
        r | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (0xFFu << 24);

    // Bottom-left corner of the figure, clear of titles and axis labels.
    constexpr float margin = 8.0f;
    text_renderer_.draw_text(format_unsupported_notice(unsupported_series_),
                             margin,
                             static_cast<float>(figure.height()) - margin,
                             FontSize::Label,
                             rgba,
                             TextAlign::Left,
                             TextVAlign::Bottom);
}

// render_plot_text() — moved to render_geometry.cpp
// render_plot_geometry() — moved to render_geometry.cpp

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
    // Nothing else draws into this pass, so the legend comes from the renderer
    // even if the interactive window's ImGui overlay normally provides it.
    const bool overlay_legend = legend_overlay_active_;
    legend_overlay_active_    = false;
    begin_render_pass();
    render_figure_content(figure);
    render_text(static_cast<float>(figure.width()), static_cast<float>(figure.height()));
    end_render_pass();
    legend_overlay_active_ = overlay_legend;
}

ExportViewport compute_export_viewport(uint32_t figure_width,
                                       uint32_t figure_height,
                                       uint32_t export_width,
                                       uint32_t export_height)
{
    ExportViewport ev{export_width, export_height, 1.0f};
    if (figure_width == 0 || figure_height == 0 || export_width == 0 || export_height == 0)
        return ev;

    const float scale = std::min(static_cast<float>(export_width) / figure_width,
                                 static_cast<float>(export_height) / figure_height);
    if (scale <= 1.0f)
        return ev;

    ev.pixel_scale   = scale;
    ev.layout_width  = std::max(1u, static_cast<uint32_t>(std::lround(export_width / scale)));
    ev.layout_height = std::max(1u, static_cast<uint32_t>(std::lround(export_height / scale)));
    return ev;
}

// upload_series_data() — moved to render_upload.cpp

void Renderer::render_axes(AxesBase&   axes,
                           const Rect& viewport,
                           uint32_t    fig_width,
                           uint32_t    fig_height)
{
    // Skip axes entirely outside the framebuffer (scrolled off-screen)
    if (viewport.y + viewport.h < 0.0f || viewport.y > static_cast<float>(fig_height))
        return;
    if (viewport.x + viewport.w < 0.0f || viewport.x > static_cast<float>(fig_width))
        return;
    // Skip degenerate (collapsed) axes: a zero-size VkViewport is invalid usage
    // and a zero height makes the 3D aspect ratio inf/NaN.  Nothing would be
    // visible anyway since the scissor is empty.
    if (!(viewport.w >= 1.0f) || !(viewport.h >= 1.0f))
        return;

    // Clamp scissor to framebuffer bounds to avoid negative offsets
    float sx  = std::max(0.0f, viewport.x);
    float sy  = std::max(0.0f, viewport.y);
    float sx2 = std::min(static_cast<float>(fig_width), viewport.x + viewport.w);
    float sy2 = std::min(static_cast<float>(fig_height), viewport.y + viewport.h);
    float sw  = std::max(0.0f, sx2 - sx);
    float sh  = std::max(0.0f, sy2 - sy);

    // Set scissor to clamped axes viewport
    backend_.set_scissor(static_cast<int32_t>(sx),
                         static_cast<int32_t>(sy),
                         static_cast<uint32_t>(sw),
                         static_cast<uint32_t>(sh));

    // Set viewport (Vulkan viewport can have negative origin for proper rendering)
    backend_.set_viewport(viewport.x, viewport.y, viewport.w, viewport.h);

    FrameUBO ubo{};

    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        // 3D projection with camera
        // Build perspective projection
        float aspect = viewport.w / viewport.h;
        // Pick up azimuth/elevation/distance assigned directly on the camera.
        axes3d->camera().apply_orbit_changes();
        const auto& cam = axes3d->camera();
        // Widen the view so the box, arrows and labels fit this viewport.
        const float fit = axes3d->projection_fit_scale(viewport);

        if (cam.projection_mode == Camera::ProjectionMode::Perspective)
        {
            // Perspective projection matrix
            float fov_rad      = cam.fov * 3.14159265f / 180.0f;
            float f            = 1.0f / (tanf(fov_rad * 0.5f) * fit);
            float y_sign       = backend_.clip_y_down() ? -1.0f : 1.0f;
            ubo.projection[0]  = f / aspect;
            ubo.projection[5]  = y_sign * f;
            ubo.projection[10] = cam.far_clip / (cam.near_clip - cam.far_clip);
            ubo.projection[11] = -1.0f;
            ubo.projection[14] = (cam.far_clip * cam.near_clip) / (cam.near_clip - cam.far_clip);
        }
        else
        {
            // Orthographic projection with proper near/far depth
            // Must match Camera::projection_matrix() convention:
            // half_w = ortho_size * aspect, half_h = ortho_size (times the fit)
            float half_w = cam.ortho_size * aspect * fit;
            float half_h = cam.ortho_size * fit;
            build_ortho_projection_3d(-half_w,
                                      half_w,
                                      -half_h,
                                      half_h,
                                      cam.near_clip,
                                      cam.far_clip,
                                      ubo.projection);
        }

        // Camera view matrix
        const mat4& view = cam.view_matrix();
        std::memcpy(ubo.view, view.m, 16 * sizeof(float));

        // Model matrix maps data coordinates into fixed-size normalized cube
        mat4 model = axes3d->data_to_normalized_matrix();
        std::memcpy(ubo.model, model.m, 16 * sizeof(float));

        ubo.near_plane = cam.near_clip;
        ubo.far_plane  = cam.far_clip;

        // Camera position for lighting
        ubo.camera_pos[0] = cam.position.x;
        ubo.camera_pos[1] = cam.position.y;
        ubo.camera_pos[2] = cam.position.z;

        // Light direction from Axes3D (configurable)
        if (axes3d->lighting_enabled())
        {
            vec3 ld          = axes3d->light_dir();
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
        // 2D orthographic projection — camera-relative rendering.
        // Build projection centered at the view midpoint so that the
        // translation terms (m[12], m[13]) are zero (or near-zero).
        // Data on the GPU is stored relative to a per-series origin,
        // and the small gap between origin and view center is bridged
        // by the data_offset push constants.

        // Phase 2 (LT-5): read limits via AxesViewModel when available,
        // enabling per-view overrides and validation.
        AxisLimits xlim;
        AxisLimits ylim;
        if (figure_vm_)
        {
            auto& axes_vm = figure_vm_->get_or_create_axes_vm(axes2d);
            xlim          = axes_vm.visual_xlim();
            ylim          = axes_vm.visual_ylim();
        }
        else
        {
            xlim = axes2d->x_limits();
            ylim = axes2d->y_limits();
        }

        // The projection works in axis space (log10/log2 of the data on log
        // axes) so a log axis is drawn in evenly spaced decades.
        const ScaleType  xscale  = axes2d->xscale();
        const ScaleType  yscale  = axes2d->yscale();
        const AxisLimits ax_x    = axis_space_limits(xscale, xlim);
        const AxisLimits ax_y    = axis_space_limits(yscale, ylim);
        double           view_cx = (ax_x.min + ax_x.max) * 0.5;
        double           view_cy = (ax_y.min + ax_y.max) * 0.5;
        double           half_rx = (ax_x.max - ax_x.min) * 0.5;
        double           half_ry = (ax_y.max - ax_y.min) * 0.5;

        // Cache limits and view center for all sub-functions this frame.
        // Reading x_limits()/y_limits() only once prevents mid-frame
        // inconsistency when external threads modify series data.
        auto& axes_gpu           = axes_gpu_data_[&axes];
        axes_gpu.view_center_x   = view_cx;
        axes_gpu.view_center_y   = view_cy;
        axes_gpu.cached_xlim_min = xlim.min;
        axes_gpu.cached_xlim_max = xlim.max;
        axes_gpu.cached_ylim_min = ylim.min;
        axes_gpu.cached_ylim_max = ylim.max;
        axes_gpu.xscale            = xscale;
        axes_gpu.yscale            = yscale;
        axes_gpu.axis_half_range_x = half_rx;
        axes_gpu.axis_half_range_y = half_ry;

        // Centered projection: maps [-half_range, +half_range] → NDC.
        // Translation term m[12] = 0 exactly, avoiding catastrophic cancellation.
        build_ortho_projection(-half_rx, half_rx, -half_ry, half_ry, ubo.projection);

        // Identity view matrix (2D)
        ubo.view[0]  = 1.0f;
        ubo.view[5]  = 1.0f;
        ubo.view[10] = 1.0f;
        ubo.view[15] = 1.0f;
        // Identity model matrix (2D)
        ubo.model[0]  = 1.0f;
        ubo.model[5]  = 1.0f;
        ubo.model[10] = 1.0f;
        ubo.model[15] = 1.0f;

        ubo.near_plane = 0.01f;
        ubo.far_plane  = 1000.0f;

        // Default camera position and light for 2D
        ubo.camera_pos[0] = 0.0f;
        ubo.camera_pos[1] = 0.0f;
        ubo.camera_pos[2] = 1.0f;
        ubo.light_dir[0]  = 0.0f;
        ubo.light_dir[1]  = 0.0f;
        ubo.light_dir[2]  = 1.0f;
    }

    ubo.viewport_width  = viewport.w;
    ubo.viewport_height = viewport.h;
    ubo.time            = 0.0f;

    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // 2D border is now rendered in screen-space by render_plot_geometry()
    // to avoid a GPU buffer race condition (the per-axes border vertex buffer
    // could be overwritten by a later flight frame while a previous frame's
    // GPU work was still reading it, causing border misalignment during fast zoom).

    // Render 3D bounding box, tick marks, and axis arrows (all depth-tested)
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        render_bounding_box(*axes3d, viewport);
        render_tick_marks(*axes3d, viewport);
        render_arrows(*axes3d, viewport);
    }

    // Render 3D grid BEFORE series so series appears on top.
    // 2D grid is rendered in screen-space by render_plot_geometry().
    if (dynamic_cast<Axes3D*>(&axes))
        render_grid(axes, viewport);

    // For 3D axes, sort series by distance from camera for correct transparency.
    // Opaque series render first (front-to-back for early-Z benefit),
    // then transparent series render back-to-front (painter's algorithm).
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        const auto& cam       = axes3d->camera();
        vec3        cam_pos   = cam.position;
        mat4        model_mat = axes3d->data_to_normalized_matrix();

        // Collect visible series with their distances.
        struct SortEntry
        {
            Series* series;
            float   distance;
        };
        std::vector<SortEntry> opaque_entries;
        std::vector<SortEntry> transparent_entries;
        const size_t           series_count = axes.series().size();
        opaque_entries.reserve(series_count);
        transparent_entries.reserve(series_count);

        for (auto& series_ptr : axes.series())
        {
            if (!series_ptr)
                continue;

            // Phase 2 (LT-5): check visibility via SeriesViewModel when available.
            bool             visible = false;
            SeriesViewModel* svm_ptr = nullptr;
            if (figure_vm_)
            {
                auto& svm = figure_vm_->get_or_create_series_vm(series_ptr.get());
                visible   = svm.effective_visible();
                svm_ptr   = &svm;
            }
            else
            {
                visible = series_ptr->visible();
            }
            if (!visible)
                continue;

            auto gpu_it = series_gpu_data_.find(series_ptr.get());
            if (series_ptr->is_dirty() || gpu_it == series_gpu_data_.end()
                || gpu_it->second.type == SeriesType::Unknown
                || gpu_it->second.plugin_state_released)
            {
                upload_series_data(*series_ptr);
                gpu_it = series_gpu_data_.find(series_ptr.get());
            }

            // Compute centroid distance from camera using cached series type.
            vec3 centroid{0.0f, 0.0f, 0.0f};
            if (gpu_it != series_gpu_data_.end())
            {
                switch (gpu_it->second.type)
                {
                    case SeriesType::Line3D:
                        centroid = static_cast<LineSeries3D*>(series_ptr.get())->compute_centroid();
                        break;
                    case SeriesType::Scatter3D:
                        centroid =
                            static_cast<ScatterSeries3D*>(series_ptr.get())->compute_centroid();
                        break;
                    case SeriesType::Surface3D:
                        centroid =
                            static_cast<SurfaceSeries*>(series_ptr.get())->compute_centroid();
                        break;
                    case SeriesType::Mesh3D:
                        centroid = static_cast<MeshSeries*>(series_ptr.get())->compute_centroid();
                        break;
                    case SeriesType::Shape3D:
                        centroid =
                            static_cast<ShapeSeries3D*>(series_ptr.get())->compute_centroid();
                        break;
                    default:
                        break;
                }
            }

            // Transform centroid to world space via model matrix
            vec4 world_c   = mat4_mul_vec4(model_mat, {centroid.x, centroid.y, centroid.z, 1.0f});
            vec3 world_pos = {world_c.x, world_c.y, world_c.z};
            auto dist      = static_cast<float>(vec3_length(world_pos - cam_pos));

            // Phase 2 (LT-5): read color/opacity via SeriesViewModel when available.
            float effective_alpha = NAN;
            if (svm_ptr)
            {
                Color eff_color = svm_ptr->effective_color();
                effective_alpha = eff_color.a * svm_ptr->effective_opacity();
            }
            else
            {
                effective_alpha = series_ptr->color().a * series_ptr->opacity();
            }
            bool is_transparent = effective_alpha < 0.99f;

            if (is_transparent)
            {
                transparent_entries.push_back({series_ptr.get(), dist});
            }
            else
            {
                opaque_entries.push_back({series_ptr.get(), dist});
            }
        }

        // Sort opaque front-to-back (for early-Z optimization)
        // stable_sort preserves insertion order for equal-distance series, ensuring
        // deterministic rendering when opaque series share the same centroid depth.
        std::stable_sort(opaque_entries.begin(),
                         opaque_entries.end(),
                         [](const SortEntry& a, const SortEntry& b)
                         { return a.distance < b.distance; });

        // Sort transparent back-to-front (painter's algorithm)
        std::stable_sort(transparent_entries.begin(),
                         transparent_entries.end(),
                         [](const SortEntry& a, const SortEntry& b)
                         { return a.distance > b.distance; });

        // Render opaque first, then transparent
        for (auto& entry : opaque_entries)
        {
            render_series(*entry.series, viewport);
        }
        for (auto& entry : transparent_entries)
        {
            render_series(*entry.series, viewport);
        }

        // Selection highlight on top of all 3D series
        render_selection_highlight(axes, viewport);
    }
    else
    {
        // 2D: render in order (no sorting needed)
        // Camera-relative rendering: use the cached view center and limits
        // from projection setup for re-uploads and data_offset push constants.
        auto*  axes2d  = dynamic_cast<Axes*>(&axes);
        double view_cx = 0.0;
        double view_cy = 0.0;
        double half_rx = 0.0;
        double    half_ry = 0.0;
        ScaleType xscale  = ScaleType::Linear;
        ScaleType yscale  = ScaleType::Linear;
        if (axes2d)
        {
            auto& agpu = axes_gpu_data_[&axes];
            view_cx    = agpu.view_center_x;
            view_cy    = agpu.view_center_y;
            half_rx    = agpu.axis_half_range_x;
            half_ry    = agpu.axis_half_range_y;
            xscale     = agpu.xscale;
            yscale     = agpu.yscale;
        }

        // Pass visible x-range for draw-call culling on large series.
        // Uses the cached limits for consistency within this frame.
        VisibleRange        vis{};
        const VisibleRange* vis_ptr = nullptr;
        if (axes2d)
        {
            auto& agpu = axes_gpu_data_[&axes];
            vis.x_min  = agpu.cached_xlim_min;
            vis.x_max  = agpu.cached_xlim_max;
            vis_ptr    = &vis;
        }

        // Re-upload threshold: when the series' upload origin drifts more
        // than this factor × view range from the current view center,
        // re-upload to prevent float precision loss in the data_offset sum.
        constexpr double ORIGIN_DRIFT_THRESHOLD = 100.0;

        for (auto& series_ptr : axes.series())
        {
            if (!series_ptr)
                continue;

            // Phase 2 (LT-5): check visibility via SeriesViewModel when available
            bool visible = false;
            if (figure_vm_)
            {
                auto& svm = figure_vm_->get_or_create_series_vm(series_ptr.get());
                visible   = svm.effective_visible();
            }
            else
            {
                visible = series_ptr->visible();
            }
            if (!visible)
                continue;

            auto& gpu = series_gpu_data_[series_ptr.get()];

            // Determine if camera-relative re-upload is needed
            bool need_upload = series_ptr->is_dirty() || gpu.plugin_state_released
                               || gpu.upload_xscale != xscale || gpu.upload_yscale != yscale;

            // Shape arrowheads are sized in screen pixels: rebuild them when the
            // data-per-pixel scale changes (zoom, pan across limits, resize).
            if (axes2d && (gpu.type == SeriesType::Shape2D || gpu.type == SeriesType::Unknown))
            {
                if (auto* shape = dynamic_cast<ShapeSeries*>(series_ptr.get()))
                {
                    const auto& agpu = axes_gpu_data_[&axes];
                    float dppx = static_cast<float>((agpu.cached_xlim_max - agpu.cached_xlim_min)
                                                    / viewport.w);
                    float dppy = static_cast<float>((agpu.cached_ylim_max - agpu.cached_ylim_min)
                                                    / viewport.h);
                    if (shape->set_pixel_scale(dppx, dppy))
                        need_upload = true;
                }
            }
            if (!need_upload && axes2d && gpu.uploaded_count > 0)
            {
                double drift_x  = std::abs(gpu.origin_x - view_cx);
                double drift_y  = std::abs(gpu.origin_y - view_cy);
                double thresh_x = std::max(half_rx, 1e-30) * ORIGIN_DRIFT_THRESHOLD;
                double thresh_y = std::max(half_ry, 1e-30) * ORIGIN_DRIFT_THRESHOLD;
                if (drift_x > thresh_x || drift_y > thresh_y)
                    need_upload = true;
            }

            if (need_upload)
            {
                upload_series_data(*series_ptr, view_cx, view_cy, xscale, yscale);
            }

            render_series(*series_ptr, viewport, vis_ptr, view_cx, view_cy);
        }

        // Selection highlight on top of all 2D series
        render_selection_highlight(axes, viewport);
    }

    // Tick labels, axis labels, and titles are now rendered by ImGui
}

// render_grid() — moved to render_geometry.cpp
// render_bounding_box() — moved to render_3d.cpp
// render_tick_marks() — moved to render_3d.cpp
// render_arrows() — moved to render_3d.cpp
// render_axis_border() — moved to render_geometry.cpp
// render_series() — moved to render_2d.cpp
// render_selection_highlight() — moved to render_2d.cpp
// upload_series_data() — moved to render_upload.cpp
// render_plot_text() — moved to render_geometry.cpp
// render_plot_geometry() — moved to render_geometry.cpp

void Renderer::set_selected_series(const std::vector<const Series*>& selected)
{
    selected_series_ = selected;
}

void Renderer::clear_selected_series()
{
    selected_series_.clear();
}

void Renderer::build_ortho_projection(double left,
                                      double right,
                                      double bottom,
                                      double top,
                                      float* m)
{
    // Column-major 4x4 orthographic projection matrix
    // Maps [left,right] x [bottom,top] to [-1,1] x [-1,1]
    // Computed in double precision to avoid collapse at deep zoom,
    // then stored as float for the GPU.
    std::memset(m, 0, 16 * sizeof(float));

    double rl = right - left;
    double tb = top - bottom;

    if (rl == 0.0)
        rl = 1.0;
    if (tb == 0.0)
        tb = 1.0;

    // Y sign: Vulkan clip Y points down, WebGPU/OpenGL clip Y points up.
    double y_sign = backend_.clip_y_down() ? -1.0 : 1.0;

    m[0]  = static_cast<float>(2.0 / rl);
    m[5]  = static_cast<float>(y_sign * 2.0 / tb);
    m[10] = -1.0f;
    m[12] = static_cast<float>(-(right + left) / rl);
    m[13] = static_cast<float>(-y_sign * (top + bottom) / tb);
    m[15] = 1.0f;
}

void Renderer::build_ortho_projection_3d(float  left,
                                         float  right,
                                         float  bottom,
                                         float  top,
                                         float  near_clip,
                                         float  far_clip,
                                         float* m)
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

    float y_sign = backend_.clip_y_down() ? -1.0f : 1.0f;

    m[0]  = 2.0f / rl;
    m[5]  = y_sign * 2.0f / tb;
    m[10] = -1.0f / fn;   // Maps [near,far] → [0,1] for Vulkan depth
    m[12] = -(right + left) / rl;
    m[13] = -y_sign * (top + bottom) / tb;
    m[14] = -near_clip / fn;   // Depth offset
    m[15] = 1.0f;
}

}   // namespace spectra
