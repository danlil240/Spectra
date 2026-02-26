#include "renderer.hpp"

#include <algorithm>
#include <cstring>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "ui/imgui/axes3d_renderer.hpp"
#include "ui/theme/theme.hpp"

namespace spectra
{

Renderer::Renderer(Backend& backend) : backend_(backend) {}

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

void Renderer::flush_pending_deletions()
{
    // Destroy the oldest slot — these resources were queued DELETION_RING_SIZE
    // frames ago, so the GPU is guaranteed to be done with them.
    uint32_t destroy_slot = (deletion_ring_write_ + 1) % DELETION_RING_SIZE;
    auto&    slot         = deletion_ring_[destroy_slot];
    for (auto& gpu : slot)
    {
        if (gpu.ssbo)
            backend_.destroy_buffer(gpu.ssbo);
        if (gpu.index_buffer)
            backend_.destroy_buffer(gpu.index_buffer);
        if (gpu.fill_buffer)
            backend_.destroy_buffer(gpu.fill_buffer);
        if (gpu.outlier_buffer)
            backend_.destroy_buffer(gpu.outlier_buffer);
    }
    slot.clear();

    // Advance write pointer to the slot we just freed.
    deletion_ring_write_ = destroy_slot;
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

Renderer::~Renderer()
{
    // Wait for GPU to finish using all resources before destroying them
    backend_.wait_idle();

    // Shutdown text renderer
    text_renderer_.shutdown(backend_);

    // Flush ALL deferred deletion ring slots
    for (auto& slot : deletion_ring_)
    {
        for (auto& gpu : slot)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
        }
        slot.clear();
    }

    // Clean up per-series GPU data
    for (auto& [ptr, data] : series_gpu_data_)
    {
        if (data.ssbo)
        {
            backend_.destroy_buffer(data.ssbo);
        }
        if (data.index_buffer)
        {
            backend_.destroy_buffer(data.index_buffer);
        }
    }
    series_gpu_data_.clear();

    // Clean up per-axes GPU data (grid + border + bbox + tick buffers)
    for (auto& [ptr, data] : axes_gpu_data_)
    {
        if (data.grid_buffer)
            backend_.destroy_buffer(data.grid_buffer);
        if (data.border_buffer)
            backend_.destroy_buffer(data.border_buffer);
        if (data.bbox_buffer)
            backend_.destroy_buffer(data.bbox_buffer);
        if (data.tick_buffer)
            backend_.destroy_buffer(data.tick_buffer);
        if (data.arrow_line_buffer)
            backend_.destroy_buffer(data.arrow_line_buffer);
        if (data.arrow_tri_buffer)
            backend_.destroy_buffer(data.arrow_tri_buffer);
    }
    axes_gpu_data_.clear();

    if (overlay_line_buffer_)
        backend_.destroy_buffer(overlay_line_buffer_);
    if (overlay_tri_buffer_)
        backend_.destroy_buffer(overlay_tri_buffer_);

    if (frame_ubo_buffer_)
    {
        backend_.destroy_buffer(frame_ubo_buffer_);
    }
}

bool Renderer::init()
{
    // Create pipelines for each series type
    line_pipeline_      = backend_.create_pipeline(PipelineType::Line);
    scatter_pipeline_   = backend_.create_pipeline(PipelineType::Scatter);
    grid_pipeline_      = backend_.create_pipeline(PipelineType::Grid);
    overlay_pipeline_   = backend_.create_pipeline(PipelineType::Overlay);
    stat_fill_pipeline_ = backend_.create_pipeline(PipelineType::StatFill);

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

    const auto& theme_colors = ui::ThemeManager::instance().colors();
    Color       bg_color     = Color(theme_colors.bg_primary.r,
                           theme_colors.bg_primary.g,
                           theme_colors.bg_primary.b,
                           theme_colors.bg_primary.a);
    backend_.begin_render_pass(bg_color);
    backend_.set_line_width(1.0f);   // Set default for VK_DYNAMIC_STATE_LINE_WIDTH
}

void Renderer::render_figure_content(Figure& figure)
{
    uint32_t w = figure.width();
    uint32_t h = figure.height();

    // Set full-figure viewport and scissor
    backend_.set_viewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    backend_.set_scissor(0, 0, w, h);

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

        render_axes(ax, vp, w, h);
    }

    // Queue all plot text (tick labels, axis labels, titles) via Vulkan TextRenderer.
    // Flushed later by render_text().
    render_plot_text(figure);

    // Render screen-space plot geometry (2D tick marks) via Vulkan grid pipeline.
    // 3D arrows are rendered inside render_axes() with depth testing.
    render_plot_geometry(figure);
}

void Renderer::render_plot_text(Figure& figure)
{
    if (!text_renderer_.is_initialized())
        return;

    const auto& colors = ui::ThemeManager::instance().colors();

    auto color_to_rgba = [](const ui::Color& c) -> uint32_t
    {
        uint8_t r = static_cast<uint8_t>(c.r * 255);
        uint8_t g = static_cast<uint8_t>(c.g * 255);
        uint8_t b = static_cast<uint8_t>(c.b * 255);
        uint8_t a = static_cast<uint8_t>(c.a * 255);
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
               | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    };

    uint32_t tick_col  = color_to_rgba(colors.tick_label);
    uint32_t label_col = color_to_rgba(colors.text_primary);
    uint32_t title_col = label_col;

    constexpr float tick_padding = 5.0f;

    // ── 2D Axes: tick labels, axis labels, title ──
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto&       axes = *axes_ptr;
        const auto& vp   = axes.viewport();
        auto        xlim = axes.x_limits();
        auto        ylim = axes.y_limits();

        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f)
            x_range = 1.0f;
        if (y_range == 0.0f)
            y_range = 1.0f;

        auto data_to_px_x = [&](float dx) -> float
        { return vp.x + (dx - xlim.min) / x_range * vp.w; };
        auto data_to_px_y = [&](float dy) -> float
        { return vp.y + (1.0f - (dy - ylim.min) / y_range) * vp.h; };

        const auto& as = axes.axis_style();
        float       tl = as.tick_length;

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();

        // X tick labels
        for (size_t i = 0; i < x_ticks.positions.size(); ++i)
        {
            float px = data_to_px_x(x_ticks.positions[i]);
            text_renderer_.draw_text(x_ticks.labels[i],
                                     px,
                                     vp.y + vp.h + tl + tick_padding,
                                     FontSize::Tick,
                                     tick_col,
                                     TextAlign::Center,
                                     TextVAlign::Top);
        }

        // Y tick labels
        for (size_t i = 0; i < y_ticks.positions.size(); ++i)
        {
            float py = data_to_px_y(y_ticks.positions[i]);
            text_renderer_.draw_text(y_ticks.labels[i],
                                     vp.x - tl - tick_padding,
                                     py,
                                     FontSize::Tick,
                                     tick_col,
                                     TextAlign::Right,
                                     TextVAlign::Middle);
        }

        // X axis label
        if (!axes.get_xlabel().empty())
        {
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y + vp.h + tick_padding + 16.0f + tick_padding;
            text_renderer_.draw_text(axes.get_xlabel(),
                                     cx,
                                     py,
                                     FontSize::Label,
                                     label_col,
                                     TextAlign::Center);
        }

        // Y axis label (rotated -90°)
        if (!axes.get_ylabel().empty())
        {
            float           center_x   = vp.x - tick_padding * 2.0f - 20.0f;
            float           center_y   = vp.y + vp.h * 0.5f;
            constexpr float neg_90_deg = -1.5707963f;   // -π/2
            text_renderer_.draw_text_rotated(axes.get_ylabel(),
                                             center_x,
                                             center_y,
                                             neg_90_deg,
                                             FontSize::Label,
                                             label_col);
        }

        // Title
        if (!axes.get_title().empty())
        {
            auto  ext = text_renderer_.measure_text(axes.get_title(), FontSize::Title);
            float cx  = vp.x + vp.w * 0.5f;
            float py  = vp.y - ext.height - tick_padding;
            if (py < vp.y + 2.0f)
                py = vp.y + 2.0f;
            text_renderer_
                .draw_text(axes.get_title(), cx, py, FontSize::Title, title_col, TextAlign::Center);
        }
    }

    // ── 3D Axes: billboarded tick labels, axis labels, title ──
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
        if (!axes3d)
            continue;

        const auto& vp  = axes3d->viewport();
        const auto& cam = axes3d->camera();

        // Build MVP matrix: projection * view * model
        float aspect = vp.w / std::max(vp.h, 1.0f);
        mat4  proj   = cam.projection_matrix(aspect);
        mat4  view   = cam.view_matrix();
        mat4  model  = axes3d->data_to_normalized_matrix();
        mat4  mvp    = mat4_mul(proj, mat4_mul(view, model));

        // Helper: project a 3D world point to screen coords within the viewport
        // Also outputs NDC depth in [0,1] for depth-tested text rendering.
        auto world_to_screen = [&](vec3 world_pos, float& sx, float& sy, float& ndc_depth) -> bool
        {
            float clip_x = mvp.m[0] * world_pos.x + mvp.m[4] * world_pos.y + mvp.m[8] * world_pos.z
                           + mvp.m[12];
            float clip_y = mvp.m[1] * world_pos.x + mvp.m[5] * world_pos.y + mvp.m[9] * world_pos.z
                           + mvp.m[13];
            float clip_z = mvp.m[2] * world_pos.x + mvp.m[6] * world_pos.y + mvp.m[10] * world_pos.z
                           + mvp.m[14];
            float clip_w = mvp.m[3] * world_pos.x + mvp.m[7] * world_pos.y + mvp.m[11] * world_pos.z
                           + mvp.m[15];

            if (clip_w <= 0.001f)
                return false;

            float ndc_x = clip_x / clip_w;
            float ndc_y = clip_y / clip_w;
            ndc_depth   = clip_z / clip_w;   // Vulkan depth range [0,1]

            sx = vp.x + (ndc_x + 1.0f) * 0.5f * vp.w;
            sy = vp.y + (ndc_y + 1.0f) * 0.5f * vp.h;

            float margin = 200.0f;
            if (sx < vp.x - margin || sx > vp.x + vp.w + margin || sy < vp.y - margin
                || sy > vp.y + vp.h + margin)
                return false;

            return true;
        };

        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();

        float x0 = xlim.min, y0 = ylim.min, z0 = zlim.min;

        vec3 view_dir       = vec3_normalize(cam.target - cam.position);
        bool looking_down_y = std::abs(view_dir.y) > 0.98f;
        bool looking_down_z = std::abs(view_dir.z) > 0.98f;

        float x_tick_offset = (ylim.max - ylim.min) * 0.04f;
        float y_tick_offset = (xlim.max - xlim.min) * 0.04f;
        float z_tick_offset = (xlim.max - xlim.min) * 0.04f;
        constexpr float tick_label_min_spacing_px = 18.0f;
        auto should_skip_overlapping_tick =
            [&](float sx, float sy, float& last_sx, float& last_sy, bool& has_last) -> bool
        {
            if (!has_last)
            {
                last_sx = sx;
                last_sy = sy;
                has_last = true;
                return false;
            }
            float dx = sx - last_sx;
            float dy = sy - last_sy;
            if ((dx * dx + dy * dy) < (tick_label_min_spacing_px * tick_label_min_spacing_px))
                return true;
            last_sx = sx;
            last_sy = sy;
            return false;
        };

        // --- X-axis tick labels ---
        {
            auto x_ticks = axes3d->compute_x_ticks();
            float last_sx = 0.0f, last_sy = 0.0f;
            bool  has_last = false;
            for (size_t i = 0; i < x_ticks.positions.size(); ++i)
            {
                float sx, sy, depth;
                vec3  pos = {x_ticks.positions[i], y0 - x_tick_offset, z0};
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                text_renderer_.draw_text_depth(x_ticks.labels[i],
                                               sx,
                                               sy,
                                               depth,
                                               FontSize::Tick,
                                               tick_col,
                                               TextAlign::Center,
                                               TextVAlign::Top);
            }
        }

        // --- Y-axis tick labels ---
        if (!looking_down_y)
        {
            auto y_ticks = axes3d->compute_y_ticks();
            float last_sx = 0.0f, last_sy = 0.0f;
            bool  has_last = false;
            for (size_t i = 0; i < y_ticks.positions.size(); ++i)
            {
                float sx, sy, depth;
                vec3  pos = {x0 - y_tick_offset, y_ticks.positions[i], z0};
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                text_renderer_.draw_text_depth(y_ticks.labels[i],
                                               sx,
                                               sy,
                                               depth,
                                               FontSize::Tick,
                                               tick_col,
                                               TextAlign::Right,
                                               TextVAlign::Middle);
            }
        }

        // --- Z-axis tick labels ---
        if (!looking_down_z)
        {
            auto z_ticks = axes3d->compute_z_ticks();
            float last_sx = 0.0f, last_sy = 0.0f;
            bool  has_last = false;
            for (size_t i = 0; i < z_ticks.positions.size(); ++i)
            {
                float sx, sy, depth;
                vec3  pos = {x0 - z_tick_offset, y0, z_ticks.positions[i]};
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                text_renderer_.draw_text_depth(z_ticks.labels[i],
                                               sx - tick_padding,
                                               sy,
                                               depth,
                                               FontSize::Tick,
                                               tick_col,
                                               TextAlign::Right,
                                               TextVAlign::Middle);
            }
        }

        // --- 3D axis arrow labels ---
        {
            float x1 = xlim.max, y1 = ylim.max, z1 = zlim.max;
            float arrow_len_x = (xlim.max - xlim.min) * 0.18f;
            float arrow_len_y = (ylim.max - ylim.min) * 0.18f;
            float arrow_len_z = (zlim.max - zlim.min) * 0.18f;

            auto pack_rgba = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t
            {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
                       | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };
            uint32_t vk_x_arrow_col = pack_rgba(230, 70, 70, 220);
            uint32_t vk_y_arrow_col = pack_rgba(70, 200, 70, 220);
            uint32_t vk_z_arrow_col = pack_rgba(80, 130, 255, 220);

            // Helper: draw arrow label text at the tip of an axis arrow
            auto draw_arrow_label = [&](vec3               start,
                                        vec3               end,
                                        uint32_t           vk_col,
                                        const char*        default_lbl,
                                        const std::string& user_lbl)
            {
                float sx0, sy0, d0, sx1, sy1, d1;
                if (!world_to_screen(start, sx0, sy0, d0) || !world_to_screen(end, sx1, sy1, d1))
                    return;
                const char* lbl          = user_lbl.empty() ? default_lbl : user_lbl.c_str();
                float       label_offset = 8.0f;
                float       dir_x        = sx1 - sx0;
                float       dir_y        = sy1 - sy0;
                float       dir_len      = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                float lx = sx1 + (dir_len > 1.0f ? dir_x / dir_len * label_offset : label_offset);
                float ly_center = sy1 + (dir_len > 1.0f ? dir_y / dir_len * label_offset : 0.0f);
                text_renderer_.draw_text_depth(lbl,
                                               lx,
                                               ly_center,
                                               d1,
                                               FontSize::Label,
                                               vk_col,
                                               TextAlign::Left,
                                               TextVAlign::Middle);
            };

            draw_arrow_label({x1, y0, z0},
                             {x1 + arrow_len_x, y0, z0},
                             vk_x_arrow_col,
                             "X",
                             axes3d->get_xlabel());
            draw_arrow_label({x0, y1, z0},
                             {x0, y1 + arrow_len_y, z0},
                             vk_y_arrow_col,
                             "Y",
                             axes3d->get_ylabel());
            draw_arrow_label({x0, y0, z1},
                             {x0, y0, z1 + arrow_len_z},
                             vk_z_arrow_col,
                             "Z",
                             axes3d->get_zlabel());
        }

        // --- 3D Title ---
        if (!axes3d->get_title().empty())
        {
            float cx  = vp.x + vp.w * 0.5f;
            auto  ext = text_renderer_.measure_text(axes3d->get_title(), FontSize::Title);
            float py  = vp.y - ext.height - tick_padding;
            if (py < vp.y + 2.0f)
                py = vp.y + 2.0f;
            text_renderer_.draw_text(axes3d->get_title(),
                                     cx,
                                     py,
                                     FontSize::Title,
                                     title_col,
                                     TextAlign::Center);
        }
    }
}

void Renderer::render_plot_geometry(Figure& figure)
{
    uint32_t fig_w = figure.width();
    uint32_t fig_h = figure.height();
    float    fw    = static_cast<float>(fig_w);
    float    fh    = static_cast<float>(fig_h);

    const auto& colors = ui::ThemeManager::instance().colors();

    overlay_line_scratch_.clear();
    overlay_tri_scratch_.clear();

    // ── 2D Axes: tick mark lines ──
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto&       axes = *axes_ptr;
        const auto& vp   = axes.viewport();
        auto        xlim = axes.x_limits();
        auto        ylim = axes.y_limits();

        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f)
            x_range = 1.0f;
        if (y_range == 0.0f)
            y_range = 1.0f;

        auto data_to_px_x = [&](float dx) -> float
        { return vp.x + (dx - xlim.min) / x_range * vp.w; };
        auto data_to_px_y = [&](float dy) -> float
        { return vp.y + (1.0f - (dy - ylim.min) / y_range) * vp.h; };

        const auto& as = axes.axis_style();
        float       tl = as.tick_length;
        if (tl <= 0.0f)
            continue;

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();

        // X tick marks (at bottom edge of viewport)
        for (size_t i = 0; i < x_ticks.positions.size(); ++i)
        {
            float px = data_to_px_x(x_ticks.positions[i]);
            overlay_line_scratch_.push_back(px);
            overlay_line_scratch_.push_back(vp.y + vp.h);
            overlay_line_scratch_.push_back(px);
            overlay_line_scratch_.push_back(vp.y + vp.h + tl);
        }

        // Y tick marks (at left edge of viewport)
        for (size_t i = 0; i < y_ticks.positions.size(); ++i)
        {
            float py = data_to_px_y(y_ticks.positions[i]);
            overlay_line_scratch_.push_back(vp.x);
            overlay_line_scratch_.push_back(py);
            overlay_line_scratch_.push_back(vp.x - tl);
            overlay_line_scratch_.push_back(py);
        }
    }

    // NOTE: 3D axis arrows are now rendered by render_arrows() inside render_axes()
    // with depth testing, so they are properly occluded by 3D geometry.

    // ── Upload and draw 2D tick marks ──
    // Set up screen-space ortho projection in UBO.
    // Screen coordinates are Y-down (0=top, h=bottom), matching Vulkan clip space,
    // so use positive Y scale (same as TextRenderer::flush).
    // Do NOT use build_ortho_projection() — that negates Y for data-space (Y-up).
    FrameUBO ubo{};                   // Value-initializes all members to zero
    ubo.projection[0]  = 2.0f / fw;   // X: [0, fw] → [-1, +1]
    ubo.projection[5]  = 2.0f / fh;   // Y: [0, fh] → [-1, +1] (positive = Y-down)
    ubo.projection[10] = -1.0f;
    ubo.projection[12] = -1.0f;
    ubo.projection[13] = -1.0f;
    ubo.projection[15] = 1.0f;
    // Identity view + model
    ubo.view[0]         = 1.0f;
    ubo.view[5]         = 1.0f;
    ubo.view[10]        = 1.0f;
    ubo.view[15]        = 1.0f;
    ubo.model[0]        = 1.0f;
    ubo.model[5]        = 1.0f;
    ubo.model[10]       = 1.0f;
    ubo.model[15]       = 1.0f;
    ubo.viewport_width  = fw;
    ubo.viewport_height = fh;

    backend_.set_viewport(0, 0, fw, fh);
    backend_.set_scissor(0, 0, fig_w, fig_h);
    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // Draw 2D tick mark lines
    uint32_t line_vert_count = static_cast<uint32_t>(overlay_line_scratch_.size() / 2);
    if (line_vert_count > 0)
    {
        size_t line_bytes = overlay_line_scratch_.size() * sizeof(float);
        if (!overlay_line_buffer_ || overlay_line_capacity_ < line_bytes)
        {
            if (overlay_line_buffer_)
                backend_.destroy_buffer(overlay_line_buffer_);
            overlay_line_buffer_   = backend_.create_buffer(BufferUsage::Vertex, line_bytes * 2);
            overlay_line_capacity_ = line_bytes * 2;
        }
        backend_.upload_buffer(overlay_line_buffer_, overlay_line_scratch_.data(), line_bytes);

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc{};
        pc.color[0]   = colors.axis_line.r;
        pc.color[1]   = colors.axis_line.g;
        pc.color[2]   = colors.axis_line.b;
        pc.color[3]   = colors.axis_line.a;
        pc.line_width = 1.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(overlay_line_buffer_, 0);
        backend_.draw(line_vert_count);
    }
}

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
    begin_render_pass();
    render_figure_content(figure);
    end_render_pass();
}

void Renderer::upload_series_data(Series& series)
{
    // Try 2D series first
    auto* line    = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);

    // Try 3D series
    auto* line3d    = dynamic_cast<LineSeries3D*>(&series);
    auto* scatter3d = dynamic_cast<ScatterSeries3D*>(&series);
    auto* surface   = dynamic_cast<SurfaceSeries*>(&series);
    auto* mesh      = dynamic_cast<MeshSeries*>(&series);

    // Try statistical series
    auto* boxplot   = dynamic_cast<BoxPlotSeries*>(&series);
    auto* violin    = dynamic_cast<ViolinSeries*>(&series);
    auto* histogram = dynamic_cast<HistogramSeries*>(&series);
    auto* bar       = dynamic_cast<BarSeries*>(&series);

    auto& gpu = series_gpu_data_[&series];

    // Tag series type on first encounter (avoids dynamic_cast in render_series)
    if (gpu.type == SeriesType::Unknown)
    {
        if (line)
            gpu.type = SeriesType::Line2D;
        else if (scatter)
            gpu.type = SeriesType::Scatter2D;
        else if (line3d)
            gpu.type = SeriesType::Line3D;
        else if (scatter3d)
            gpu.type = SeriesType::Scatter3D;
        else if (surface)
            gpu.type = SeriesType::Surface3D;
        else if (mesh)
            gpu.type = SeriesType::Mesh3D;
        else if (boxplot)
            gpu.type = SeriesType::BoxPlot2D;
        else if (violin)
            gpu.type = SeriesType::Violin2D;
        else if (histogram)
            gpu.type = SeriesType::Histogram2D;
        else if (bar)
            gpu.type = SeriesType::Bar2D;
    }

    // Handle 2D line/scatter and statistical series (vec2 interleaved)
    if (line || scatter || boxplot || violin || histogram || bar)
    {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t       count  = 0;

        if (line)
        {
            x_data = line->x_data().data();
            y_data = line->y_data().data();
            count  = line->point_count();
        }
        else if (scatter)
        {
            x_data = scatter->x_data().data();
            y_data = scatter->y_data().data();
            count  = scatter->point_count();
        }
        else if (boxplot)
        {
            boxplot->rebuild_geometry();
            x_data = boxplot->x_data().data();
            y_data = boxplot->y_data().data();
            count  = boxplot->point_count();
        }
        else if (violin)
        {
            violin->rebuild_geometry();
            x_data = violin->x_data().data();
            y_data = violin->y_data().data();
            count  = violin->point_count();
        }
        else if (histogram)
        {
            histogram->rebuild_geometry();
            x_data = histogram->x_data().data();
            y_data = histogram->y_data().data();
            count  = histogram->point_count();
        }
        else if (bar)
        {
            bar->rebuild_geometry();
            x_data = bar->x_data().data();
            y_data = bar->y_data().data();
            count  = bar->point_count();
        }

        if (count == 0)
            return;

        size_t byte_size = count * 2 * sizeof(float);
        if (!gpu.ssbo || gpu.uploaded_count < count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_size = byte_size * 2;
            gpu.ssbo          = backend_.create_buffer(BufferUsage::Storage, alloc_size);
        }

        size_t floats_needed = count * 2;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);
        for (size_t i = 0; i < count; ++i)
        {
            upload_scratch_[i * 2]     = x_data[i];
            upload_scratch_[i * 2 + 1] = y_data[i];
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count = count;

        // Upload fill geometry for statistical series (interleaved {x,y,alpha} vertex buffer)
        std::span<const float> fill_verts;
        size_t                 fill_count = 0;
        if (boxplot && boxplot->fill_vertex_count() > 0)
        {
            fill_verts = boxplot->fill_verts();
            fill_count = boxplot->fill_vertex_count();
        }
        else if (violin && violin->fill_vertex_count() > 0)
        {
            fill_verts = violin->fill_verts();
            fill_count = violin->fill_vertex_count();
        }
        else if (histogram && histogram->fill_vertex_count() > 0)
        {
            fill_verts = histogram->fill_verts();
            fill_count = histogram->fill_vertex_count();
        }
        else if (bar && bar->fill_vertex_count() > 0)
        {
            fill_verts = bar->fill_verts();
            fill_count = bar->fill_vertex_count();
        }

        if (fill_count > 0)
        {
            // 3 floats per vertex: x, y, alpha
            size_t fill_bytes = fill_count * 3 * sizeof(float);
            if (!gpu.fill_buffer || gpu.fill_vertex_count < fill_count)
            {
                if (gpu.fill_buffer)
                    backend_.destroy_buffer(gpu.fill_buffer);
                gpu.fill_buffer = backend_.create_buffer(BufferUsage::Vertex, fill_bytes * 2);
            }

            backend_.upload_buffer(gpu.fill_buffer, fill_verts.data(), fill_bytes);
            gpu.fill_vertex_count = fill_count;
        }

        // Upload outlier data for box plots (persistent buffer, avoids in-flight destruction)
        if (boxplot && boxplot->outlier_count() > 0)
        {
            size_t out_count     = boxplot->outlier_count();
            size_t out_byte_size = out_count * 2 * sizeof(float);
            if (!gpu.outlier_buffer || gpu.outlier_count < out_count)
            {
                if (gpu.outlier_buffer)
                    backend_.destroy_buffer(gpu.outlier_buffer);
                gpu.outlier_buffer =
                    backend_.create_buffer(BufferUsage::Storage, out_byte_size * 2);
            }
            size_t out_floats = out_count * 2;
            if (upload_scratch_.size() < out_floats)
                upload_scratch_.resize(out_floats);
            for (size_t i = 0; i < out_count; ++i)
            {
                upload_scratch_[i * 2]     = boxplot->outlier_x().data()[i];
                upload_scratch_[i * 2 + 1] = boxplot->outlier_y().data()[i];
            }
            backend_.upload_buffer(gpu.outlier_buffer, upload_scratch_.data(), out_byte_size);
            gpu.outlier_count = out_count;
        }
        else if (boxplot)
        {
            gpu.outlier_count = 0;
        }

        series.clear_dirty();
    }
    // Handle 3D line/scatter (vec4 interleaved: x,y,z,pad)
    else if (line3d || scatter3d)
    {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        const float* z_data = nullptr;
        size_t       count  = 0;

        if (line3d)
        {
            x_data = line3d->x_data().data();
            y_data = line3d->y_data().data();
            z_data = line3d->z_data().data();
            count  = line3d->point_count();
        }
        else if (scatter3d)
        {
            x_data = scatter3d->x_data().data();
            y_data = scatter3d->y_data().data();
            z_data = scatter3d->z_data().data();
            count  = scatter3d->point_count();
        }

        if (count == 0)
            return;

        size_t byte_size = count * 4 * sizeof(float);
        if (!gpu.ssbo || gpu.uploaded_count < count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_size = byte_size * 2;
            gpu.ssbo          = backend_.create_buffer(BufferUsage::Storage, alloc_size);
        }

        size_t floats_needed = count * 4;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);
        for (size_t i = 0; i < count; ++i)
        {
            upload_scratch_[i * 4]     = x_data[i];
            upload_scratch_[i * 4 + 1] = y_data[i];
            upload_scratch_[i * 4 + 2] = z_data[i];
            upload_scratch_[i * 4 + 3] = 0.0f;   // padding
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count = count;
        series.clear_dirty();
    }
    // Handle surface (vertex buffer + index buffer)
    else if (surface)
    {
        // Choose between wireframe and solid mesh
        const SurfaceMesh* active_mesh = nullptr;
        if (surface->wireframe())
        {
            if (!surface->is_wireframe_mesh_generated())
            {
                surface->generate_wireframe_mesh();
            }
            if (!surface->is_wireframe_mesh_generated())
                return;
            active_mesh = &surface->wireframe_mesh();
        }
        else
        {
            if (!surface->is_mesh_generated())
            {
                surface->generate_mesh();
            }
            if (!surface->is_mesh_generated())
                return;
            active_mesh = &surface->mesh();
        }

        if (active_mesh->vertices.empty() || active_mesh->indices.empty())
            return;

        size_t vert_byte_size = active_mesh->vertices.size() * sizeof(float);
        size_t idx_byte_size  = active_mesh->indices.size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < active_mesh->vertex_count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, active_mesh->vertices.data(), vert_byte_size);
        gpu.uploaded_count = active_mesh->vertex_count;

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < active_mesh->indices.size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, active_mesh->indices.data(), idx_byte_size);
        gpu.index_count = active_mesh->indices.size();

        series.clear_dirty();
    }
    // Handle mesh (vertex buffer + index buffer)
    else if (mesh)
    {
        if (mesh->vertices().empty() || mesh->indices().empty())
            return;

        size_t vert_byte_size = mesh->vertices().size() * sizeof(float);
        size_t idx_byte_size  = mesh->indices().size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < mesh->vertex_count())
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, mesh->vertices().data(), vert_byte_size);
        gpu.uploaded_count = mesh->vertex_count();

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < mesh->indices().size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, mesh->indices().data(), idx_byte_size);
        gpu.index_count = mesh->indices().size();

        series.clear_dirty();
    }
}

void Renderer::render_axes(AxesBase&   axes,
                           const Rect& viewport,
                           uint32_t    fig_width,
                           uint32_t    fig_height)
{
    // Set scissor to axes viewport
    backend_.set_scissor(static_cast<int32_t>(viewport.x),
                         static_cast<int32_t>(viewport.y),
                         static_cast<uint32_t>(viewport.w),
                         static_cast<uint32_t>(viewport.h));

    // Set viewport
    backend_.set_viewport(viewport.x, viewport.y, viewport.w, viewport.h);

    FrameUBO ubo{};

    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        // 3D projection with camera
        // Build perspective projection
        float       aspect = viewport.w / viewport.h;
        const auto& cam    = axes3d->camera();

        if (cam.projection_mode == Camera::ProjectionMode::Perspective)
        {
            // Perspective projection matrix
            float fov_rad      = cam.fov * 3.14159265f / 180.0f;
            float f            = 1.0f / tanf(fov_rad * 0.5f);
            ubo.projection[0]  = f / aspect;
            ubo.projection[5]  = -f;   // Negative for Vulkan Y-down
            ubo.projection[10] = cam.far_clip / (cam.near_clip - cam.far_clip);
            ubo.projection[11] = -1.0f;
            ubo.projection[14] = (cam.far_clip * cam.near_clip) / (cam.near_clip - cam.far_clip);
        }
        else
        {
            // Orthographic projection with proper near/far depth
            // Must match Camera::projection_matrix() convention:
            // half_w = ortho_size * aspect, half_h = ortho_size
            float half_w = cam.ortho_size * aspect;
            float half_h = cam.ortho_size;
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
        // 2D orthographic projection
        auto xlim = axes2d->x_limits();
        auto ylim = axes2d->y_limits();

        build_ortho_projection(xlim.min, xlim.max, ylim.min, ylim.max, ubo.projection);
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

    // Render axis border (2D only)
    if (axes.border_enabled() && !dynamic_cast<Axes3D*>(&axes))
        render_axis_border(axes, viewport, fig_width, fig_height);

    // Render 3D bounding box, tick marks, and axis arrows (all depth-tested)
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        render_bounding_box(*axes3d, viewport);
        render_tick_marks(*axes3d, viewport);
        render_arrows(*axes3d, viewport);
    }

    // Render grid BEFORE series so series appears on top (for 3D)
    render_grid(axes, viewport);

    // For 3D axes, sort series by distance from camera for correct transparency.
    // Opaque series render first (front-to-back for early-Z benefit),
    // then transparent series render back-to-front (painter's algorithm).
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        const auto& cam       = axes3d->camera();
        vec3        cam_pos   = cam.position;
        mat4        model_mat = axes3d->data_to_normalized_matrix();

        // Collect visible series with their distances
        struct SortEntry
        {
            Series* series;
            float   distance;
            bool    transparent;
        };
        std::vector<SortEntry> opaque_entries;
        std::vector<SortEntry> transparent_entries;

        for (auto& series_ptr : axes.series())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            if (series_ptr->is_dirty())
            {
                upload_series_data(*series_ptr);
            }

            // Compute centroid distance from camera
            vec3  centroid{0.0f, 0.0f, 0.0f};
            auto* line3d    = dynamic_cast<LineSeries3D*>(series_ptr.get());
            auto* scatter3d = dynamic_cast<ScatterSeries3D*>(series_ptr.get());
            auto* surface   = dynamic_cast<SurfaceSeries*>(series_ptr.get());
            auto* mesh_s    = dynamic_cast<MeshSeries*>(series_ptr.get());

            if (line3d)
                centroid = line3d->compute_centroid();
            else if (scatter3d)
                centroid = scatter3d->compute_centroid();
            else if (surface)
                centroid = surface->compute_centroid();
            else if (mesh_s)
                centroid = mesh_s->compute_centroid();

            // Transform centroid to world space via model matrix
            vec4  world_c   = mat4_mul_vec4(model_mat, {centroid.x, centroid.y, centroid.z, 1.0f});
            vec3  world_pos = {world_c.x, world_c.y, world_c.z};
            float dist      = vec3_length(world_pos - cam_pos);

            bool is_transparent = (series_ptr->color().a * series_ptr->opacity()) < 0.99f;

            if (is_transparent)
            {
                transparent_entries.push_back({series_ptr.get(), dist, true});
            }
            else
            {
                opaque_entries.push_back({series_ptr.get(), dist, false});
            }
        }

        // Sort opaque front-to-back (for early-Z optimization)
        std::sort(opaque_entries.begin(),
                  opaque_entries.end(),
                  [](const SortEntry& a, const SortEntry& b) { return a.distance < b.distance; });

        // Sort transparent back-to-front (painter's algorithm)
        std::sort(transparent_entries.begin(),
                  transparent_entries.end(),
                  [](const SortEntry& a, const SortEntry& b) { return a.distance > b.distance; });

        // Render opaque first, then transparent
        for (auto& entry : opaque_entries)
        {
            render_series(*entry.series, viewport);
        }
        for (auto& entry : transparent_entries)
        {
            render_series(*entry.series, viewport);
        }
    }
    else
    {
        // 2D: render in order (no sorting needed)
        // Pass visible x-range for draw-call culling on large series
        VisibleRange vis{};
        const VisibleRange* vis_ptr = nullptr;
        if (auto* axes2d = dynamic_cast<Axes*>(&axes))
        {
            auto xlim = axes2d->x_limits();
            vis.x_min = xlim.min;
            vis.x_max = xlim.max;
            vis_ptr   = &vis;
        }

        for (auto& series_ptr : axes.series())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            if (series_ptr->is_dirty())
            {
                upload_series_data(*series_ptr);
            }

            render_series(*series_ptr, viewport, vis_ptr);
        }
    }

    // Tick labels, axis labels, and titles are now rendered by ImGui
}

void Renderer::render_grid(AxesBase& axes, const Rect& /*viewport*/)
{
    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        if (!axes3d->grid_enabled())
            return;

        auto  xlim = axes3d->x_limits();
        auto  ylim = axes3d->y_limits();
        auto  zlim = axes3d->z_limits();
        auto  gp   = axes3d->grid_planes();
        auto& gpu  = axes_gpu_data_[&axes];

        // Check if limits/planes changed — skip regeneration if cached
        auto& gc             = gpu.grid_cache;
        bool  limits_changed = !gc.valid || gc.xmin != xlim.min || gc.xmax != xlim.max
                              || gc.ymin != ylim.min || gc.ymax != ylim.max || gc.zmin != zlim.min
                              || gc.zmax != zlim.max
                              || gpu.cached_grid_planes != static_cast<int>(gp);

        if (limits_changed)
        {
            // Generate 3D grid vertices at tick positions (matches tick labels)
            Axes3DRenderer::GridPlaneData grid_gen;
            vec3                          min_corner = {xlim.min, ylim.min, zlim.min};
            vec3                          max_corner = {xlim.max, ylim.max, zlim.max};

            auto x_ticks = axes3d->compute_x_ticks().positions;
            auto y_ticks = axes3d->compute_y_ticks().positions;
            auto z_ticks = axes3d->compute_z_ticks().positions;

            if (static_cast<int>(gp & Axes3D::GridPlane::XY))
                grid_gen.generate_xy_plane(min_corner, max_corner, zlim.min, x_ticks, y_ticks);
            if (static_cast<int>(gp & Axes3D::GridPlane::XZ))
                grid_gen.generate_xz_plane(min_corner, max_corner, ylim.min, x_ticks, z_ticks);
            if (static_cast<int>(gp & Axes3D::GridPlane::YZ))
                grid_gen.generate_yz_plane(min_corner, max_corner, xlim.min, y_ticks, z_ticks);

            if (grid_gen.vertices.empty())
                return;

            size_t float_count = grid_gen.vertices.size() * 3;
            if (grid_scratch_.size() < float_count)
                grid_scratch_.resize(float_count);
            for (size_t i = 0; i < grid_gen.vertices.size(); ++i)
            {
                grid_scratch_[i * 3]     = grid_gen.vertices[i].x;
                grid_scratch_[i * 3 + 1] = grid_gen.vertices[i].y;
                grid_scratch_[i * 3 + 2] = grid_gen.vertices[i].z;
            }

            size_t byte_size = float_count * sizeof(float);
            if (!gpu.grid_buffer || gpu.grid_capacity < byte_size)
            {
                if (gpu.grid_buffer)
                    backend_.destroy_buffer(gpu.grid_buffer);
                gpu.grid_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
                gpu.grid_capacity = byte_size * 2;
            }
            backend_.upload_buffer(gpu.grid_buffer, grid_scratch_.data(), byte_size);
            gpu.grid_vertex_count  = static_cast<uint32_t>(float_count / 3);
            gc.xmin                = xlim.min;
            gc.xmax                = xlim.max;
            gc.ymin                = ylim.min;
            gc.ymax                = ylim.max;
            gc.zmin                = zlim.min;
            gc.zmax                = zlim.max;
            gpu.cached_grid_planes = static_cast<int>(gp);
            gc.valid               = true;
        }

        if (!gpu.grid_buffer || gpu.grid_vertex_count == 0)
            return;

        // Draw 3D grid as overlay (no depth test so it's always visible)
        backend_.bind_pipeline(grid_overlay3d_pipeline_);

        SeriesPushConstants pc{};
        const auto&         theme_colors = ui::ThemeManager::instance().colors();
        float               blend        = 0.3f;
        pc.color[0]                      = theme_colors.grid_line.r * (1.0f - blend) + blend;
        pc.color[1]                      = theme_colors.grid_line.g * (1.0f - blend) + blend;
        pc.color[2]                      = theme_colors.grid_line.b * (1.0f - blend) + blend;
        pc.color[3]                      = 0.35f;
        pc.line_width                    = 1.0f;
        pc.data_offset_x                 = 0.0f;
        pc.data_offset_y                 = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(gpu.grid_buffer, 0);
        backend_.draw(gpu.grid_vertex_count);
    }
    else if (auto* axes2d = dynamic_cast<Axes*>(&axes))
    {
        // 2D grid rendering
        if (!axes2d->grid_enabled())
            return;

        auto  xlim = axes2d->x_limits();
        auto  ylim = axes2d->y_limits();
        auto& gpu  = axes_gpu_data_[&axes];

        // Check if limits changed — skip regeneration if cached
        auto& gc             = gpu.grid_cache;
        bool  limits_changed = !gc.valid || gc.xmin != xlim.min || gc.xmax != xlim.max
                              || gc.ymin != ylim.min || gc.ymax != ylim.max;

        if (limits_changed)
        {
            auto x_ticks = axes2d->compute_x_ticks();
            auto y_ticks = axes2d->compute_y_ticks();

            size_t num_x = x_ticks.positions.size();
            size_t num_y = y_ticks.positions.size();
            if (num_x == 0 && num_y == 0)
                return;

            size_t total_lines   = num_x + num_y;
            size_t grid2d_floats = total_lines * 4;
            if (grid_scratch_.size() < grid2d_floats)
                grid_scratch_.resize(grid2d_floats);
            size_t wi = 0;

            for (size_t i = 0; i < num_x; ++i)
            {
                float x             = x_ticks.positions[i];
                grid_scratch_[wi++] = x;
                grid_scratch_[wi++] = ylim.min;
                grid_scratch_[wi++] = x;
                grid_scratch_[wi++] = ylim.max;
            }
            for (size_t i = 0; i < num_y; ++i)
            {
                float y             = y_ticks.positions[i];
                grid_scratch_[wi++] = xlim.min;
                grid_scratch_[wi++] = y;
                grid_scratch_[wi++] = xlim.max;
                grid_scratch_[wi++] = y;
            }

            size_t byte_size = wi * sizeof(float);
            if (!gpu.grid_buffer || gpu.grid_capacity < byte_size)
            {
                if (gpu.grid_buffer)
                    backend_.destroy_buffer(gpu.grid_buffer);
                gpu.grid_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
                gpu.grid_capacity = byte_size * 2;
            }
            backend_.upload_buffer(gpu.grid_buffer, grid_scratch_.data(), byte_size);
            gpu.grid_vertex_count = static_cast<uint32_t>(total_lines * 2);
            gc.xmin               = xlim.min;
            gc.xmax               = xlim.max;
            gc.ymin               = ylim.min;
            gc.ymax               = ylim.max;
            gc.valid              = true;
        }

        if (!gpu.grid_buffer || gpu.grid_vertex_count == 0)
            return;

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc{};
        const auto&         as = axes2d->axis_style();
        if (as.grid_color.a > 0.0f)
        {
            pc.color[0] = as.grid_color.r;
            pc.color[1] = as.grid_color.g;
            pc.color[2] = as.grid_color.b;
            pc.color[3] = as.grid_color.a;
        }
        else
        {
            const auto& theme_colors = ui::ThemeManager::instance().colors();
            pc.color[0]              = theme_colors.grid_line.r;
            pc.color[1]              = theme_colors.grid_line.g;
            pc.color[2]              = theme_colors.grid_line.b;
            pc.color[3]              = theme_colors.grid_line.a;
        }
        pc.line_width    = as.grid_width;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.set_line_width(std::max(1.0f, as.grid_width));
        backend_.bind_buffer(gpu.grid_buffer, 0);
        backend_.draw(gpu.grid_vertex_count);
        backend_.set_line_width(1.0f);
    }
}

void Renderer::render_bounding_box(Axes3D& axes, const Rect& /*viewport*/)
{
    if (!axes.show_bounding_box())
        return;

    auto  xlim = axes.x_limits();
    auto  ylim = axes.y_limits();
    auto  zlim = axes.z_limits();
    auto& gpu  = axes_gpu_data_[&axes];

    // Check if limits changed — skip regeneration if cached
    auto& bc             = gpu.bbox_cache;
    bool  limits_changed = !bc.valid || bc.xmin != xlim.min || bc.xmax != xlim.max
                          || bc.ymin != ylim.min || bc.ymax != ylim.max || bc.zmin != zlim.min
                          || bc.zmax != zlim.max;

    if (limits_changed)
    {
        vec3 min_corner = {xlim.min, ylim.min, zlim.min};
        vec3 max_corner = {xlim.max, ylim.max, zlim.max};

        Axes3DRenderer::BoundingBoxData bbox;
        bbox.generate(min_corner, max_corner);

        if (bbox.edge_vertices.empty())
            return;

        size_t bbox_floats = bbox.edge_vertices.size() * 3;
        if (bbox_scratch_.size() < bbox_floats)
            bbox_scratch_.resize(bbox_floats);
        for (size_t i = 0; i < bbox.edge_vertices.size(); ++i)
        {
            bbox_scratch_[i * 3]     = bbox.edge_vertices[i].x;
            bbox_scratch_[i * 3 + 1] = bbox.edge_vertices[i].y;
            bbox_scratch_[i * 3 + 2] = bbox.edge_vertices[i].z;
        }

        size_t byte_size = bbox_floats * sizeof(float);
        if (!gpu.bbox_buffer || gpu.bbox_capacity < byte_size)
        {
            if (gpu.bbox_buffer)
                backend_.destroy_buffer(gpu.bbox_buffer);
            gpu.bbox_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size);
            gpu.bbox_capacity = byte_size;
        }
        backend_.upload_buffer(gpu.bbox_buffer, bbox_scratch_.data(), byte_size);
        gpu.bbox_vertex_count = static_cast<uint32_t>(bbox.edge_vertices.size());
        bc.xmin               = xlim.min;
        bc.xmax               = xlim.max;
        bc.ymin               = ylim.min;
        bc.ymax               = ylim.max;
        bc.zmin               = zlim.min;
        bc.zmax               = zlim.max;
        bc.valid              = true;
    }

    if (!gpu.bbox_buffer || gpu.bbox_vertex_count == 0)
        return;

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = ui::ThemeManager::instance().colors();
    pc.color[0]                      = theme_colors.grid_line.r * 0.7f;
    pc.color[1]                      = theme_colors.grid_line.g * 0.7f;
    pc.color[2]                      = theme_colors.grid_line.b * 0.7f;
    pc.color[3]                      = theme_colors.grid_line.a * 0.8f;
    pc.line_width                    = 1.5f;
    pc.data_offset_x                 = 0.0f;
    pc.data_offset_y                 = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.bbox_buffer, 0);
    backend_.draw(gpu.bbox_vertex_count);
}

void Renderer::render_tick_marks(Axes3D& axes, const Rect& /*viewport*/)
{
    auto  xlim = axes.x_limits();
    auto  ylim = axes.y_limits();
    auto  zlim = axes.z_limits();
    auto& gpu  = axes_gpu_data_[&axes];

    // Check if limits changed — skip regeneration if cached
    auto& tc             = gpu.tick_cache;
    bool  limits_changed = !tc.valid || tc.xmin != xlim.min || tc.xmax != xlim.max
                          || tc.ymin != ylim.min || tc.ymax != ylim.max || tc.zmin != zlim.min
                          || tc.zmax != zlim.max;

    if (limits_changed)
    {
        vec3 min_corner = {xlim.min, ylim.min, zlim.min};
        vec3 max_corner = {xlim.max, ylim.max, zlim.max};

        // Tick length: ~2% of axis range
        float x_tick_len = (ylim.max - ylim.min) * 0.02f;
        float y_tick_len = (xlim.max - xlim.min) * 0.02f;
        float z_tick_len = (xlim.max - xlim.min) * 0.02f;

        Axes3DRenderer::TickMarkData x_data;
        x_data.generate_x_ticks(axes, min_corner, max_corner);
        Axes3DRenderer::TickMarkData y_data;
        y_data.generate_y_ticks(axes, min_corner, max_corner);
        Axes3DRenderer::TickMarkData z_data;
        z_data.generate_z_ticks(axes, min_corner, max_corner);

        size_t total_ticks =
            x_data.positions.size() + y_data.positions.size() + z_data.positions.size();
        if (total_ticks == 0)
            return;

        size_t floats_needed = total_ticks * 6;
        if (tick_scratch_.size() < floats_needed)
            tick_scratch_.resize(floats_needed);
        size_t wi = 0;

        for (const auto& pos : x_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y - x_tick_len;
            tick_scratch_[wi++] = pos.z;
        }
        for (const auto& pos : y_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x - y_tick_len;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
        }
        for (const auto& pos : z_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x - z_tick_len;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
        }

        size_t byte_size = wi * sizeof(float);
        if (!gpu.tick_buffer || gpu.tick_capacity < byte_size)
        {
            if (gpu.tick_buffer)
                backend_.destroy_buffer(gpu.tick_buffer);
            gpu.tick_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
            gpu.tick_capacity = byte_size * 2;
        }
        backend_.upload_buffer(gpu.tick_buffer, tick_scratch_.data(), byte_size);
        gpu.tick_vertex_count = static_cast<uint32_t>(wi / 3);
        tc.xmin               = xlim.min;
        tc.xmax               = xlim.max;
        tc.ymin               = ylim.min;
        tc.ymax               = ylim.max;
        tc.zmin               = zlim.min;
        tc.zmax               = zlim.max;
        tc.valid              = true;
    }

    if (!gpu.tick_buffer || gpu.tick_vertex_count == 0)
        return;

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = ui::ThemeManager::instance().colors();
    pc.color[0]                      = theme_colors.grid_line.r * 0.6f;
    pc.color[1]                      = theme_colors.grid_line.g * 0.6f;
    pc.color[2]                      = theme_colors.grid_line.b * 0.6f;
    pc.color[3]                      = theme_colors.grid_line.a;
    pc.line_width                    = 1.5f;
    pc.data_offset_x                 = 0.0f;
    pc.data_offset_y                 = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.tick_buffer, 0);
    backend_.draw(gpu.tick_vertex_count);
}

void Renderer::render_arrows(Axes3D& axes, const Rect& /*viewport*/)
{
    auto  xlim = axes.x_limits();
    auto  ylim = axes.y_limits();
    auto  zlim = axes.z_limits();
    auto& gpu  = axes_gpu_data_[&axes];

    float x0 = xlim.min, y0 = ylim.min, z0 = zlim.min;
    float x1 = xlim.max, y1 = ylim.max, z1 = zlim.max;
    float arrow_len_x = (xlim.max - xlim.min) * 0.18f;
    float arrow_len_y = (ylim.max - ylim.min) * 0.18f;
    float arrow_len_z = (zlim.max - zlim.min) * 0.18f;

    // Transform arrow endpoints from data space to normalized space so that
    // the cylinder/cone geometry is generated in a uniformly-scaled coordinate
    // system. The data_to_normalized_matrix applies non-uniform scale per axis
    // which would distort circular cross-sections into ellipses.
    mat4 model    = axes.data_to_normalized_matrix();
    auto xform_pt = [&](vec3 p) -> vec3
    {
        return {model.m[0] * p.x + model.m[4] * p.y + model.m[8] * p.z + model.m[12],
                model.m[1] * p.x + model.m[5] * p.y + model.m[9] * p.z + model.m[13],
                model.m[2] * p.x + model.m[6] * p.y + model.m[10] * p.z + model.m[14]};
    };

    // Arrow endpoints in normalized space
    vec3 n_x_start = xform_pt({x1, y0, z0});
    vec3 n_x_end   = xform_pt({x1 + arrow_len_x, y0, z0});
    vec3 n_y_start = xform_pt({x0, y1, z0});
    vec3 n_y_end   = xform_pt({x0, y1 + arrow_len_y, z0});
    vec3 n_z_start = xform_pt({x0, y0, z1});
    vec3 n_z_end   = xform_pt({x0, y0, z1 + arrow_len_z});

    // In normalized space, box_half_size is the reference for arrow thickness.
    float hs = axes.box_half_size();

    // Geometry parameters for solid lit 3D arrows
    constexpr int   SEGMENTS    = 16;
    constexpr float SHAFT_FRAC  = 0.018f;   // shaft radius as fraction of box half-size
    constexpr float CONE_FRAC   = 0.048f;   // cone radius as fraction of box half-size
    constexpr float CONE_LENGTH = 0.25f;    // cone length as fraction of arrow length
    constexpr float PI          = 3.14159265358979f;

    float shaft_r = hs * SHAFT_FRAC;
    float cone_r  = hs * CONE_FRAC;

    // Vertex layout: {px, py, pz, nx, ny, nz} = 6 floats per vertex
    arrow_tri_scratch_.clear();

    // Helper: push one vertex (position + normal) into scratch buffer
    auto push_vert = [&](vec3 pos, vec3 n)
    {
        arrow_tri_scratch_.push_back(pos.x);
        arrow_tri_scratch_.push_back(pos.y);
        arrow_tri_scratch_.push_back(pos.z);
        arrow_tri_scratch_.push_back(n.x);
        arrow_tri_scratch_.push_back(n.y);
        arrow_tri_scratch_.push_back(n.z);
    };

    // Helper: build an orthonormal basis (u, v) perpendicular to direction d
    auto make_basis = [](vec3 d, vec3& u, vec3& v)
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1e-8f)
            return;
        d.x /= len;
        d.y /= len;
        d.z /= len;
        vec3 ref = (std::abs(d.y) < 0.9f) ? vec3{0, 1, 0} : vec3{1, 0, 0};
        u = {d.y * ref.z - d.z * ref.y, d.z * ref.x - d.x * ref.z, d.x * ref.y - d.y * ref.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        if (ul < 1e-8f)
            return;
        u.x /= ul;
        u.y /= ul;
        u.z /= ul;
        v = {d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x};
    };

    // Emit a full lit 3D arrow: cylinder shaft + cone arrowhead with normals.
    auto emit_arrow_3d = [&](vec3 start, vec3 end)
    {
        vec3  dir       = {end.x - start.x, end.y - start.y, end.z - start.z};
        float total_len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (total_len < 1e-6f)
            return;
        vec3 d = {dir.x / total_len, dir.y / total_len, dir.z / total_len};

        vec3 u{}, v{};
        make_basis(d, u, v);

        float cone_len  = total_len * CONE_LENGTH;
        float shaft_len = total_len - cone_len;

        vec3 shaft_end = {
            start.x + d.x * shaft_len,
            start.y + d.y * shaft_len,
            start.z + d.z * shaft_len,
        };

        // Precompute circle offsets
        float cos_table[SEGMENTS];
        float sin_table[SEGMENTS];
        for (int i = 0; i < SEGMENTS; ++i)
        {
            float angle  = 2.0f * PI * static_cast<float>(i) / static_cast<float>(SEGMENTS);
            cos_table[i] = std::cos(angle);
            sin_table[i] = std::sin(angle);
        }

        // Point on circle at center c with radius r
        auto circle_pt = [&](vec3 c, float r, int seg) -> vec3
        {
            float cs = cos_table[seg];
            float sn = sin_table[seg];
            return {c.x + (u.x * cs + v.x * sn) * r,
                    c.y + (u.y * cs + v.y * sn) * r,
                    c.z + (u.z * cs + v.z * sn) * r};
        };

        // Outward-pointing radial normal at segment index
        auto radial_normal = [&](int seg) -> vec3
        {
            float cs = cos_table[seg];
            float sn = sin_table[seg];
            return {u.x * cs + v.x * sn, u.y * cs + v.y * sn, u.z * cs + v.z * sn};
        };

        // Negative axis direction (for back-facing caps)
        vec3 neg_d = {-d.x, -d.y, -d.z};

        // ── Cylinder shaft body ──
        // Normals point radially outward from the cylinder axis.
        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 b0   = circle_pt(start, shaft_r, i);
            vec3 b1   = circle_pt(start, shaft_r, next);
            vec3 t0   = circle_pt(shaft_end, shaft_r, i);
            vec3 t1   = circle_pt(shaft_end, shaft_r, next);
            vec3 n0   = radial_normal(i);
            vec3 n1   = radial_normal(next);
            // Two triangles per quad
            push_vert(b0, n0);
            push_vert(t0, n0);
            push_vert(b1, n1);
            push_vert(b1, n1);
            push_vert(t0, n0);
            push_vert(t1, n1);
        }

        // ── Shaft start cap (disc) — normal points backward ──
        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 p0   = circle_pt(start, shaft_r, i);
            vec3 p1   = circle_pt(start, shaft_r, next);
            push_vert(start, neg_d);
            push_vert(p1, neg_d);
            push_vert(p0, neg_d);
        }

        // ── Cone side ──
        // Cone normal: for a cone with tip at 'end' and base at 'shaft_end',
        // the surface normal tilts outward from the axis. The tilt angle depends
        // on the cone_r / cone_len ratio.
        float cone_slope  = cone_len / std::sqrt(cone_r * cone_r + cone_len * cone_len);
        float cone_radial = cone_r / std::sqrt(cone_r * cone_r + cone_len * cone_len);
        // cone_normal(seg) = radial_normal(seg) * cone_slope + d * cone_radial
        // (tilted outward from axis by the cone half-angle)
        auto cone_normal = [&](int seg) -> vec3
        {
            vec3 rn = radial_normal(seg);
            return {rn.x * cone_slope + d.x * cone_radial,
                    rn.y * cone_slope + d.y * cone_radial,
                    rn.z * cone_slope + d.z * cone_radial};
        };

        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 c0   = circle_pt(shaft_end, cone_r, i);
            vec3 c1   = circle_pt(shaft_end, cone_r, next);
            vec3 cn0  = cone_normal(i);
            vec3 cn1  = cone_normal(next);
            // Average normal at tip for smooth shading
            vec3 cn_avg = {(cn0.x + cn1.x) * 0.5f, (cn0.y + cn1.y) * 0.5f, (cn0.z + cn1.z) * 0.5f};
            push_vert(end, cn_avg);
            push_vert(c0, cn0);
            push_vert(c1, cn1);
        }

        // ── Cone base cap (disc) — normal points backward ──
        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 c0   = circle_pt(shaft_end, cone_r, i);
            vec3 c1   = circle_pt(shaft_end, cone_r, next);
            push_vert(shaft_end, neg_d);
            push_vert(c1, neg_d);
            push_vert(c0, neg_d);
        }
    };

    emit_arrow_3d(n_x_start, n_x_end);
    emit_arrow_3d(n_y_start, n_y_end);
    emit_arrow_3d(n_z_start, n_z_end);

    // Triangles per arrow: shaft body (SEGMENTS*2) + shaft cap (SEGMENTS)
    //                     + cone body (SEGMENTS) + cone cap (SEGMENTS)
    //                     = SEGMENTS * 5
    constexpr uint32_t TRIS_PER_ARROW  = SEGMENTS * 5;
    constexpr uint32_t VERTS_PER_ARROW = TRIS_PER_ARROW * 3;
    // 6 floats per vertex (pos + normal)
    constexpr uint32_t FLOATS_PER_ARROW = VERTS_PER_ARROW * 6;

    // Upload and draw all arrow geometry (Arrow3D pipeline — lit, depth tested).
    // Geometry is in normalized space, so we temporarily set the UBO model matrix
    // to identity (the vertex shader must not re-apply the non-uniform data scale).
    uint32_t total_floats   = static_cast<uint32_t>(arrow_tri_scratch_.size());
    uint32_t tri_vert_count = total_floats / 6;
    if (tri_vert_count > 0)
    {
        size_t tri_bytes = arrow_tri_scratch_.size() * sizeof(float);
        if (!gpu.arrow_tri_buffer || gpu.arrow_tri_capacity < tri_bytes)
        {
            if (gpu.arrow_tri_buffer)
                backend_.destroy_buffer(gpu.arrow_tri_buffer);
            gpu.arrow_tri_buffer   = backend_.create_buffer(BufferUsage::Vertex, tri_bytes * 2);
            gpu.arrow_tri_capacity = tri_bytes * 2;
        }
        backend_.upload_buffer(gpu.arrow_tri_buffer, arrow_tri_scratch_.data(), tri_bytes);
        gpu.arrow_tri_vertex_count = tri_vert_count;

        // Swap model matrix to identity for arrow rendering (geometry already
        // in normalized space). Preserve the rest of the UBO (projection, view,
        // camera_pos, light_dir).
        FrameUBO arrow_ubo{};
        // Read back current UBO contents by rebuilding from axes state
        const auto& cam = axes.camera();
        {
            const auto& vp     = axes.viewport();
            float       aspect = vp.w / std::max(vp.h, 1.0f);
            // Copy projection
            if (cam.projection_mode == Camera::ProjectionMode::Perspective)
            {
                float fov_rad            = cam.fov * PI / 180.0f;
                float f                  = 1.0f / std::tan(fov_rad * 0.5f);
                arrow_ubo.projection[0]  = f / aspect;
                arrow_ubo.projection[5]  = -f;
                arrow_ubo.projection[10] = cam.far_clip / (cam.near_clip - cam.far_clip);
                arrow_ubo.projection[11] = -1.0f;
                arrow_ubo.projection[14] =
                    (cam.far_clip * cam.near_clip) / (cam.near_clip - cam.far_clip);
            }
            else
            {
                float half_w = cam.ortho_size * aspect;
                float half_h = cam.ortho_size;
                build_ortho_projection_3d(-half_w,
                                          half_w,
                                          -half_h,
                                          half_h,
                                          cam.near_clip,
                                          cam.far_clip,
                                          arrow_ubo.projection);
            }
        }
        // View matrix
        const mat4& view_mat = cam.view_matrix();
        std::memcpy(arrow_ubo.view, view_mat.m, 16 * sizeof(float));
        // Identity model matrix (geometry is already in normalized space)
        mat4 identity = mat4_identity();
        std::memcpy(arrow_ubo.model, identity.m, 16 * sizeof(float));
        arrow_ubo.viewport_width  = axes.viewport().w;
        arrow_ubo.viewport_height = axes.viewport().h;
        arrow_ubo.near_plane      = cam.near_clip;
        arrow_ubo.far_plane       = cam.far_clip;
        arrow_ubo.camera_pos[0]   = cam.position.x;
        arrow_ubo.camera_pos[1]   = cam.position.y;
        arrow_ubo.camera_pos[2]   = cam.position.z;
        if (axes.lighting_enabled())
        {
            vec3 ld                = axes.light_dir();
            arrow_ubo.light_dir[0] = ld.x;
            arrow_ubo.light_dir[1] = ld.y;
            arrow_ubo.light_dir[2] = ld.z;
        }

        backend_.upload_buffer(frame_ubo_buffer_, &arrow_ubo, sizeof(FrameUBO));
        backend_.bind_buffer(frame_ubo_buffer_, 0);

        backend_.bind_pipeline(arrow3d_pipeline_);

        float arrow_colors[][4] = {
            {0.902f, 0.275f, 0.275f, 1.0f},   // X: red
            {0.275f, 0.784f, 0.275f, 1.0f},   // Y: green
            {0.314f, 0.510f, 1.000f, 1.0f},   // Z: blue
        };

        backend_.bind_buffer(gpu.arrow_tri_buffer, 0);
        uint32_t num_arrows = total_floats / FLOATS_PER_ARROW;
        for (uint32_t i = 0; i < num_arrows && i < 3; ++i)
        {
            SeriesPushConstants pc{};
            pc.color[0] = arrow_colors[i][0];
            pc.color[1] = arrow_colors[i][1];
            pc.color[2] = arrow_colors[i][2];
            pc.color[3] = arrow_colors[i][3];
            pc.opacity  = 1.0f;
            backend_.push_constants(pc);
            backend_.draw(VERTS_PER_ARROW, i * VERTS_PER_ARROW);
        }

        // Restore the original data-space model UBO so subsequent rendering
        // (grid, series) uses the correct non-uniform scale.
        FrameUBO restore_ubo = arrow_ubo;
        std::memcpy(restore_ubo.model, model.m, 16 * sizeof(float));
        backend_.upload_buffer(frame_ubo_buffer_, &restore_ubo, sizeof(FrameUBO));
        backend_.bind_buffer(frame_ubo_buffer_, 0);
    }
}

void Renderer::render_axis_border(AxesBase& axes,
                                  const Rect& /*viewport*/,
                                  uint32_t /*fig_width*/,
                                  uint32_t /*fig_height*/)
{
    // Draw border in data space using the already-bound data-space UBO.
    // Inset vertices by a tiny fraction of the axis range so they don't
    // land exactly on the NDC ±1.0 clip boundary (which causes clipping
    // of the top/right edges on some GPUs).
    auto* axes2d = dynamic_cast<Axes*>(&axes);
    if (!axes2d)
        return;   // Border only for 2D axes
    auto xlim = axes2d->x_limits();
    auto ylim = axes2d->y_limits();

    float x_range = xlim.max - xlim.min;
    float y_range = ylim.max - ylim.min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    // Use epsilon to prevent NDC boundary clipping
    // Slightly larger for symmetric ranges to ensure all borders visible
    float       eps_x   = 0.002f * x_range;   // 0.2% of x range
    float       eps_y   = 0.002f * y_range;   // 0.2% of y range
    const float MIN_EPS = 1e-8f;
    if (eps_x < MIN_EPS)
        eps_x = MIN_EPS;
    if (eps_y < MIN_EPS)
        eps_y = MIN_EPS;

    float x0 = xlim.min + eps_x;
    float y0 = ylim.min + eps_y;
    float x1 = xlim.max - eps_x;
    float y1 = ylim.max - eps_y;

    float border_verts[] = {
        // Bottom edge
        x0,
        y0,
        x1,
        y0,
        // Top edge
        x0,
        y1,
        x1,
        y1,
        // Left edge
        x0,
        y0,
        x0,
        y1,
        // Right edge
        x1,
        y0,
        x1,
        y1,
    };

    size_t byte_size = sizeof(border_verts);

    // Use per-axes border buffer so multi-subplot draws don't overwrite
    // each other within the same command buffer.
    auto& gpu = axes_gpu_data_[&axes];
    if (!gpu.border_buffer || gpu.border_capacity < byte_size)
    {
        if (gpu.border_buffer)
        {
            backend_.destroy_buffer(gpu.border_buffer);
        }
        gpu.border_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        gpu.border_capacity = byte_size;
    }
    backend_.upload_buffer(gpu.border_buffer, border_verts, byte_size);

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = ui::ThemeManager::instance().colors();
    pc.color[0]                      = theme_colors.axis_line.r;
    pc.color[1]                      = theme_colors.axis_line.g;
    pc.color[2]                      = theme_colors.axis_line.b;
    pc.color[3]                      = theme_colors.axis_line.a;
    pc.line_width                    = 1.0f;
    pc.data_offset_x                 = 0.0f;
    pc.data_offset_y                 = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.border_buffer, 0);
    backend_.draw(8);   // 4 lines × 2 vertices
}

void Renderer::render_series(Series& series, const Rect& /*viewport*/,
                             const VisibleRange* visible)
{
    auto it = series_gpu_data_.find(&series);
    if (it == series_gpu_data_.end())
        return;

    auto& gpu = it->second;
    if (!gpu.ssbo)
        return;

    SeriesPushConstants pc{};
    const auto&         c = series.color();
    pc.color[0]           = c.r;
    pc.color[1]           = c.g;
    pc.color[2]           = c.b;
    pc.color[3]           = c.a * series.opacity();

    const auto& style = series.plot_style();
    pc.line_style     = static_cast<uint32_t>(style.line_style);
    pc.marker_type    = static_cast<uint32_t>(style.marker_style);
    pc.marker_size    = style.marker_size;
    pc.opacity        = style.opacity;

    // Use cached SeriesType to avoid 6x dynamic_cast per series per frame.
    // Only perform the single targeted static_cast for the known type.
    switch (gpu.type)
    {
        case SeriesType::Line2D:
        {
            auto* line = static_cast<LineSeries*>(&series);
            if (style.line_style != LineStyle::Solid && style.line_style != LineStyle::None)
            {
                DashPattern dp = get_dash_pattern(style.line_style, line->width());
                for (int i = 0; i < dp.count && i < 8; ++i)
                    pc.dash_pattern[i] = dp.segments[i];
                pc.dash_total = dp.total;
                pc.dash_count = dp.count;
            }

            // Compute visible segment range via binary search on sorted x_data.
            // For unsorted data, fall back to drawing all segments.
            uint32_t first_seg = 0;
            uint32_t seg_count = line->point_count() > 1
                                     ? static_cast<uint32_t>(line->point_count()) - 1
                                     : 0;
            uint32_t first_pt  = 0;
            uint32_t pt_count  = static_cast<uint32_t>(line->point_count());

            if (visible && line->point_count() > 256)
            {
                const auto& xd   = line->x_data();
                size_t      n    = xd.size();
                // Quick check: is x_data sorted? (sample a few points)
                bool sorted = (n < 2) || (xd[0] <= xd[n / 4] && xd[n / 4] <= xd[n / 2]
                                           && xd[n / 2] <= xd[3 * n / 4]
                                           && xd[3 * n / 4] <= xd[n - 1]);
                if (sorted)
                {
                    const float* begin = xd.data();
                    const float* end   = begin + n;
                    // Find first point >= x_min (with 1-point margin for segment connectivity)
                    auto lo = std::lower_bound(begin, end, visible->x_min);
                    size_t lo_idx = static_cast<size_t>(lo - begin);
                    if (lo_idx > 0)
                        lo_idx--;   // include one segment before visible range

                    // Find first point > x_max
                    auto hi = std::upper_bound(begin, end, visible->x_max);
                    size_t hi_idx = static_cast<size_t>(hi - begin);
                    if (hi_idx < n)
                        hi_idx++;   // include one segment after visible range

                    if (lo_idx < hi_idx && hi_idx <= n)
                    {
                        first_seg = static_cast<uint32_t>(lo_idx);
                        uint32_t last_seg_end = static_cast<uint32_t>(hi_idx);
                        if (last_seg_end > 0)
                            last_seg_end--;   // segments = points - 1
                        seg_count = (last_seg_end > first_seg) ? (last_seg_end - first_seg) : 0;

                        first_pt = static_cast<uint32_t>(lo_idx);
                        pt_count = static_cast<uint32_t>(hi_idx - lo_idx);
                    }
                }
            }

            if (style.has_line() && seg_count > 0)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = line->width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw(seg_count * 6, first_seg * 6);
            }
            if (style.has_marker() && pt_count > 0)
            {
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size = style.marker_size;
                pc.data_offset_x = 0.0f;
                pc.data_offset_y = 0.0f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.draw_instanced(6, pt_count, first_pt);
            }
            break;
        }
        case SeriesType::Scatter2D:
        {
            auto* scatter = static_cast<ScatterSeries*>(&series);
            backend_.bind_pipeline(scatter_pipeline_);
            pc.point_size  = scatter->size();
            pc.marker_type = static_cast<uint32_t>(style.marker_style);
            if (pc.marker_type == 0)
            {
                const auto& theme_colors = ui::ThemeManager::instance().colors();
                float       bg_luma      = 0.2126f * theme_colors.bg_primary.r
                                    + 0.7152f * theme_colors.bg_primary.g
                                    + 0.0722f * theme_colors.bg_primary.b;
                pc.marker_type = static_cast<uint32_t>(
                    bg_luma > 0.80f ? MarkerStyle::FilledCircle : MarkerStyle::Circle);
            }
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(scatter->point_count()));
            break;
        }
        case SeriesType::Line3D:
        {
            auto* line3d = static_cast<LineSeries3D*>(&series);
            if (line3d->point_count() > 1)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                backend_.bind_pipeline(is_transparent ? line3d_transparent_pipeline_
                                                      : line3d_pipeline_);
                pc.line_width = line3d->width();
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(line3d->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Scatter3D:
        {
            auto* scatter3d      = static_cast<ScatterSeries3D*>(&series);
            bool  is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
            backend_.bind_pipeline(is_transparent ? scatter3d_transparent_pipeline_
                                                  : scatter3d_pipeline_);
            pc.point_size  = scatter3d->size();
            pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
            backend_.push_constants(pc);
            backend_.bind_buffer(gpu.ssbo, 0);
            backend_.draw_instanced(6, static_cast<uint32_t>(scatter3d->point_count()));
            break;
        }
        case SeriesType::Surface3D:
        {
            auto* surface = static_cast<SurfaceSeries*>(&series);
            if (gpu.index_buffer)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                if (surface->wireframe())
                {
                    if (!surface->is_wireframe_mesh_generated())
                        return;
                    backend_.bind_pipeline(is_transparent
                                               ? surface_wireframe3d_transparent_pipeline_
                                               : surface_wireframe3d_pipeline_);
                    pc._pad2[0] = surface->ambient();
                    pc._pad2[1] = surface->specular();
                    if (surface->shininess() > 0.0f)
                    {
                        pc.marker_size = surface->shininess();
                        pc.marker_type = 0;
                    }
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.bind_index_buffer(gpu.index_buffer);
                    backend_.draw_indexed(
                        static_cast<uint32_t>(surface->wireframe_mesh().indices.size()));
                }
                else
                {
                    if (!surface->is_mesh_generated())
                        return;
                    const auto& surf_mesh = surface->mesh();
                    backend_.bind_pipeline(is_transparent ? surface3d_transparent_pipeline_
                                                          : surface3d_pipeline_);
                    pc._pad2[0] = surface->ambient();
                    pc._pad2[1] = surface->specular();
                    if (surface->shininess() > 0.0f)
                    {
                        pc.marker_size = surface->shininess();
                        pc.marker_type = 0;
                    }
                    // Encode colormap in push constants for fragment shader:
                    // dash_count = colormap type (1=Viridis..7=Grayscale, 0=None)
                    // dash_pattern[0..1] = model-space Z range
                    auto cm = surface->colormap_type();
                    if (cm != ColormapType::None)
                    {
                        pc.dash_count      = static_cast<int>(cm);
                        pc.dash_pattern[0] = -3.0f;   // box_half_size (model-space Z min)
                        pc.dash_pattern[1] = 3.0f;    // box_half_size (model-space Z max)
                    }
                    backend_.push_constants(pc);
                    backend_.bind_buffer(gpu.ssbo, 0);
                    backend_.bind_index_buffer(gpu.index_buffer);
                    backend_.draw_indexed(static_cast<uint32_t>(surf_mesh.indices.size()));
                }
            }
            break;
        }
        case SeriesType::Mesh3D:
        {
            auto* mesh = static_cast<MeshSeries*>(&series);
            if (gpu.index_buffer)
            {
                bool is_transparent = (pc.color[3] * pc.opacity) < 0.99f;
                backend_.bind_pipeline(is_transparent ? mesh3d_transparent_pipeline_
                                                      : mesh3d_pipeline_);
                pc._pad2[0] = mesh->ambient();
                pc._pad2[1] = mesh->specular();
                if (mesh->shininess() > 0.0f)
                {
                    pc.marker_size = mesh->shininess();
                    pc.marker_type = 0;
                }
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                backend_.bind_index_buffer(gpu.index_buffer);
                backend_.draw_indexed(static_cast<uint32_t>(mesh->indices().size()));
            }
            break;
        }
        case SeriesType::BoxPlot2D:
        {
            auto* bp = static_cast<BoxPlotSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.45f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (bp->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(bp->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            // Render outliers as scatter points (using persistent buffer from upload)
            if (gpu.outlier_buffer && gpu.outlier_count > 0)
            {
                backend_.bind_pipeline(scatter_pipeline_);
                pc.point_size  = 5.0f;
                pc.marker_type = static_cast<uint32_t>(MarkerStyle::Circle);
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.outlier_buffer, 0);
                backend_.draw_instanced(6, static_cast<uint32_t>(gpu.outlier_count));
            }
            break;
        }
        case SeriesType::Violin2D:
        {
            auto* vn = static_cast<ViolinSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.40f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (vn->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(vn->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Histogram2D:
        {
            auto* hist = static_cast<HistogramSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.65f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (hist->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.0f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(hist->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Bar2D:
        {
            auto* bs = static_cast<BarSeries*>(&series);
            // Draw fill with per-vertex gradient alpha
            if (gpu.fill_buffer && gpu.fill_vertex_count > 0)
            {
                backend_.bind_pipeline(stat_fill_pipeline_);
                SeriesPushConstants fill_pc = pc;
                fill_pc.color[3] *= 0.75f;
                backend_.push_constants(fill_pc);
                backend_.bind_buffer(gpu.fill_buffer, 0);
                backend_.draw(static_cast<uint32_t>(gpu.fill_vertex_count));
            }
            // Draw outline
            if (bs->point_count() > 1)
            {
                backend_.bind_pipeline(line_pipeline_);
                pc.line_width = 1.5f;
                backend_.push_constants(pc);
                backend_.bind_buffer(gpu.ssbo, 0);
                uint32_t segments = static_cast<uint32_t>(bs->point_count()) - 1;
                backend_.draw(segments * 6);
            }
            break;
        }
        case SeriesType::Unknown:
            break;
    }
}

void Renderer::build_ortho_projection(float left, float right, float bottom, float top, float* m)
{
    // Column-major 4x4 orthographic projection matrix
    // Maps [left,right] x [bottom,top] to [-1,1] x [-1,1]
    std::memset(m, 0, 16 * sizeof(float));

    float rl = right - left;
    float tb = top - bottom;

    if (rl == 0.0f)
        rl = 1.0f;
    if (tb == 0.0f)
        tb = 1.0f;

    m[0]  = 2.0f / rl;
    m[5]  = -2.0f / tb;   // Negate for Vulkan Y-down clip space
    m[10] = -1.0f;
    m[12] = -(right + left) / rl;
    m[13] = (top + bottom) / tb;   // Flip sign for Vulkan
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

    m[0]  = 2.0f / rl;
    m[5]  = -2.0f / tb;   // Negate for Vulkan Y-down
    m[10] = -1.0f / fn;   // Maps [near,far] → [0,1] for Vulkan depth
    m[12] = -(right + left) / rl;
    m[13] = (top + bottom) / tb;
    m[14] = -near_clip / fn;   // Depth offset
    m[15] = 1.0f;
}

}   // namespace spectra
