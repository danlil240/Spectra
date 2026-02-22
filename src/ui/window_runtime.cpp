#include "window_runtime.hpp"

#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../core/layout.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "figure_registry.hpp"
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

#include <algorithm>
#include <chrono>
#include <span>
#include <string>

namespace spectra
{

WindowRuntime::WindowRuntime(Backend& backend, Renderer& renderer, FigureRegistry& registry)
    : backend_(backend), renderer_(renderer), registry_(registry)
{
}

// ─── update ───────────────────────────────────────────────────────────────────
// Per-window update: advance animations, build ImGui UI, compute layout.
void WindowRuntime::update(WindowUIContext& ui_ctx,
                           FrameState& fs,
                           FrameScheduler& scheduler,
                           FrameProfiler* profiler
#ifdef SPECTRA_USE_GLFW
                           ,
                           WindowManager* /*window_mgr*/
#endif
)
{
    auto* active_figure = fs.active_figure;
    auto& active_figure_id = fs.active_figure_id;
    auto& has_animation = fs.has_animation;

#ifdef SPECTRA_USE_IMGUI
    auto& imgui_ui = ui_ctx.imgui_ui;
    auto& data_interaction = ui_ctx.data_interaction;
    auto& dock_system = ui_ctx.dock_system;
    auto& timeline_editor = ui_ctx.timeline_editor;
    auto& mode_transition = ui_ctx.mode_transition;
    auto& home_limits = ui_ctx.home_limits;
    auto& fig_mgr = *ui_ctx.fig_mgr;
    auto& anim_controller = ui_ctx.anim_controller;

    // Advance timeline editor (drives interpolator evaluation)
    // When Playing, we control the playhead ourselves to avoid double-speed
    if (timeline_editor.playback_state() != PlaybackState::Playing)
    {
        timeline_editor.advance(scheduler.dt());
    }

    // Update mode transition animation — only animate camera, never axis limits
    if (mode_transition.is_active())
    {
        mode_transition.update(scheduler.dt());

        Axes3D* ax3d = nullptr;
        for (auto& ax_base : active_figure->all_axes())
        {
            if (ax_base)
            {
                ax3d = dynamic_cast<Axes3D*>(ax_base.get());
                if (ax3d)
                    break;
            }
        }
        if (ax3d)
        {
            Camera interp_cam = mode_transition.interpolated_camera();
            // Set position directly (not via orbit) because the
            // top-down camera is on the Z axis, not an orbit position.
            ax3d->camera().position = interp_cam.position;
            ax3d->camera().target = interp_cam.target;
            ax3d->camera().up = interp_cam.up;
            ax3d->camera().fov = interp_cam.fov;
            ax3d->camera().ortho_size = interp_cam.ortho_size;
            ax3d->camera().projection_mode = interp_cam.projection_mode;
            ax3d->camera().near_clip = interp_cam.near_clip;
            ax3d->camera().far_clip = interp_cam.far_clip;
            ax3d->camera().distance = interp_cam.distance;
        }
    }
#endif

#ifdef SPECTRA_USE_GLFW
    // Update interaction animations (animated zoom, inertial pan, auto-fit)
    auto& input_handler = ui_ctx.input_handler;
    input_handler.update(scheduler.dt());
#endif

    // Helper: wire deferred-deletion callbacks on a figure's axes
    // BEFORE the user's on_frame callback can call clear_series().
    // Only set on axes that don't already have it (avoids per-frame
    // std::function allocation for the common case of unchanged axes).
    auto wire_series_callbacks = [this](Figure* fig)
    {
        for (auto& axes_ptr : fig->axes())
        {
            if (axes_ptr && !axes_ptr->has_series_removed_callback())
                axes_ptr->set_series_removed_callback([this](const Series* s)
                                                      { renderer_.notify_series_removed(s); });
        }
        for (auto& axes_ptr : fig->all_axes())
        {
            if (axes_ptr && !axes_ptr->has_series_removed_callback())
                axes_ptr->set_series_removed_callback([this](const Series* s)
                                                      { renderer_.notify_series_removed(s); });
        }
    };

    wire_series_callbacks(active_figure);

    // Helper: drive animation for a single figure using its own anim_time_.
    // is_active controls whether this figure syncs with the timeline editor.
    auto drive_figure_anim = [&](Figure* fig, bool is_active)
    {
        if (!fig->anim_on_frame_)
            return;

        Frame frame = scheduler.current_frame();

#ifdef SPECTRA_USE_IMGUI
        if (is_active)
        {
            auto tl_state = timeline_editor.playback_state();
            if (tl_state == PlaybackState::Playing)
            {
                float tl_playhead = timeline_editor.playhead();
                float diff = tl_playhead - fig->anim_time_;
                // If the figure's anim_time_ has advanced past the timeline
                // playhead (e.g. it was running as non-active while a different
                // tab was selected), sync the playhead forward to the figure
                // instead of resetting the figure backward.
                if (diff < -0.001f)
                {
                    // Figure is ahead of playhead — sync playhead to figure
                    timeline_editor.set_playhead(fig->anim_time_);
                }
                else if (diff > 0.001f)
                {
                    // User scrubbed the playhead forward — sync figure to playhead
                    fig->anim_time_ = tl_playhead;
                }
                fig->anim_time_ += frame.dt;
                frame.elapsed_sec = fig->anim_time_;
                fig->anim_on_frame_(frame);
                if (fig->anim_time_ > timeline_editor.duration())
                {
                    timeline_editor.set_duration(fig->anim_time_ + 30.0f);
                }
                timeline_editor.set_playhead(fig->anim_time_);
            }
            else if (tl_state == PlaybackState::Paused)
            {
                fig->anim_time_ = timeline_editor.playhead();
                frame.elapsed_sec = fig->anim_time_;
                frame.dt = 0.0f;
                fig->anim_on_frame_(frame);
            }
            else
            {
                fig->anim_time_ = 0.0f;
                frame.elapsed_sec = 0.0f;
                frame.dt = 0.0f;
                fig->anim_on_frame_(frame);
            }
        }
        else
        {
            // Non-active animated figure: advance its own time independently
            fig->anim_time_ += frame.dt;
            frame.elapsed_sec = fig->anim_time_;
            fig->anim_on_frame_(frame);
        }
#else
        fig->anim_time_ += frame.dt;
        frame.elapsed_sec = fig->anim_time_;
        fig->anim_on_frame_(frame);
#endif
    };

    // Drive animation for the active figure
    if (has_animation)
    {
        drive_figure_anim(active_figure, /*is_active=*/true);
    }

#ifdef SPECTRA_USE_IMGUI
    // Drive animation for non-active figures visible in split view panes.
    if (dock_system.is_split())
    {
        auto pane_infos = dock_system.get_pane_infos();
        for (const auto& pinfo : pane_infos)
        {
            if (pinfo.figure_index == fs.active_figure_id)
                continue;  // already driven above
            Figure* pfig = registry_.get(pinfo.figure_index);
            if (!pfig || !pfig->anim_on_frame_)
                continue;
            wire_series_callbacks(pfig);
            drive_figure_anim(pfig, /*is_active=*/false);
        }
    }
#endif

    // Start ImGui frame (updates layout manager with current window size).
    fs.imgui_frame_started = false;
#ifdef SPECTRA_USE_IMGUI
    if (imgui_ui)
    {
        imgui_ui->new_frame();
        fs.imgui_frame_started = true;
    }
#endif

#ifdef SPECTRA_USE_GLFW
    // Time-based resize debounce: recreate swapchain only when size has
    // stabilized (no new callback for RESIZE_DEBOUNCE ms). During drag,
    // we keep rendering with the old swapchain (slightly stretched but
    // no black flash). swapchain_dirty_ is set by present OUT_OF_DATE/SUBOPTIMAL.
    static constexpr auto RESIZE_DEBOUNCE = std::chrono::milliseconds(50);
    auto& needs_resize = ui_ctx.needs_resize;
    auto& new_width = ui_ctx.new_width;
    auto& new_height = ui_ctx.new_height;
    auto& resize_requested_time = ui_ctx.resize_requested_time;
    if (needs_resize)
    {
        auto now_resize = std::chrono::steady_clock::now();
        auto since_last = now_resize - resize_requested_time;
        if (since_last >= RESIZE_DEBOUNCE)
        {
            SPECTRA_LOG_INFO("resize",
                             "Recreating swapchain: " + std::to_string(new_width) + "x"
                                 + std::to_string(new_height));
            needs_resize = false;
            auto* vk = static_cast<VulkanBackend*>(&backend_);
            vk->clear_swapchain_dirty();
            backend_.recreate_swapchain(new_width, new_height);

            active_figure->config_.width = backend_.swapchain_width();
            active_figure->config_.height = backend_.swapchain_height();
    #ifdef SPECTRA_USE_IMGUI
            if (imgui_ui)
            {
                imgui_ui->on_swapchain_recreated(*vk);
            }
    #endif
        }
    }

    // Update input handler with current active axes viewport
    if (!active_figure->axes().empty() && active_figure->axes()[0])
    {
        auto& vp = active_figure->axes()[0]->viewport();
        input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
    }
#endif

#ifdef SPECTRA_USE_IMGUI
    // Build ImGui UI (new_frame was already called above before layout computation)
    if (imgui_ui && fs.imgui_frame_started)
    {
        if (profiler)
            profiler->begin_stage("imgui_build");
        imgui_ui->build_ui(*active_figure);

        // Old TabBar is replaced by unified pane tab headers
        // (drawn by draw_pane_tab_headers in ImGuiIntegration)
        // Always hide the layout manager's tab bar zone so canvas
        // extends into that space — pane headers draw in the canvas area.
        imgui_ui->get_layout_manager().set_tab_bar_visible(false);

        // Handle interaction state from UI — Home restores original view
        if (imgui_ui->should_reset_view())
        {
            for (auto& ax : active_figure->axes_mut())
            {
                if (ax)
                {
                    auto it = home_limits.find(ax.get());
                    if (it != home_limits.end())
                    {
                        // Animate back to the user's original limits
                        anim_controller.animate_axis_limits(
                            *ax, it->second.x, it->second.y, 0.25f, ease::ease_out);
                    }
                    else
                    {
                        // Fallback: auto-fit if we don't have saved limits
                        auto old_xlim = ax->x_limits();
                        auto old_ylim = ax->y_limits();
                        ax->auto_fit();
                        AxisLimits target_x = ax->x_limits();
                        AxisLimits target_y = ax->y_limits();
                        ax->xlim(old_xlim.min, old_xlim.max);
                        ax->ylim(old_ylim.min, old_ylim.max);
                        anim_controller.animate_axis_limits(
                            *ax, target_x, target_y, 0.25f, ease::ease_out);
                    }
                }
            }
            // 3D axes (subplot3d populates all_axes only)
            for (auto& ax_base : active_figure->all_axes_mut())
            {
                if (ax_base)
                {
                    if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                        ax3d->auto_fit();
                }
            }
            imgui_ui->clear_reset_view();
        }

        // Update input handler tool mode
        input_handler.set_tool_mode(imgui_ui->get_interaction_mode());

        // Feed cursor data to status bar
        auto readout = input_handler.cursor_readout();
        imgui_ui->set_cursor_data(readout.data_x, readout.data_y);

        // Update data interaction layer (nearest-point query, tooltip state)
        if (data_interaction)
        {
            data_interaction->update(readout, *active_figure);
        }

        // Feed zoom level (approximate: based on data bounds vs view)
        // Cache data_range to avoid O(n) minmax_element scan every frame.
        if (!active_figure->axes().empty() && active_figure->axes()[0])
        {
            auto& ax = active_figure->axes()[0];
            auto xlim = ax->x_limits();
            float view_range = xlim.max - xlim.min;

            // Invalidate cache when series count changes or any series is dirty
            size_t series_count = ax->series().size();
            bool needs_recompute =
                !ui_ctx.zoom_cache_valid || series_count != ui_ctx.cached_zoom_series_count;
            if (!needs_recompute)
            {
                for (auto& s : ax->series())
                {
                    if (s && s->is_dirty())
                    {
                        needs_recompute = true;
                        break;
                    }
                }
            }

            if (needs_recompute)
            {
                float data_min = xlim.max, data_max = xlim.min;
                for (auto& s : ax->series())
                {
                    if (!s)
                        continue;
                    std::span<const float> xd;
                    if (auto* ls = dynamic_cast<LineSeries*>(s.get()))
                        xd = ls->x_data();
                    else if (auto* sc = dynamic_cast<ScatterSeries*>(s.get()))
                        xd = sc->x_data();
                    if (!xd.empty())
                    {
                        auto [it_min, it_max] = std::minmax_element(xd.begin(), xd.end());
                        data_min = std::min(data_min, *it_min);
                        data_max = std::max(data_max, *it_max);
                    }
                }
                ui_ctx.cached_data_min = data_min;
                ui_ctx.cached_data_max = data_max;
                ui_ctx.cached_zoom_series_count = series_count;
                ui_ctx.zoom_cache_valid = true;
            }

            float data_range = ui_ctx.cached_data_max - ui_ctx.cached_data_min;
            if (view_range > 0.0f && data_range > 0.0f)
            {
                imgui_ui->set_zoom_level(data_range / view_range);
            }
        }

        // Always hide old tab bar — unified pane tab headers handle all tabs
        imgui_ui->get_layout_manager().set_tab_bar_visible(false);

        if (profiler)
            profiler->end_stage("imgui_build");
    }
#endif

#ifdef SPECTRA_USE_IMGUI
    // Process queued figure operations (create, close, switch)
    fig_mgr.process_pending();

    // Always sync active figure with FigureManager.  build_ui() may trigger
    // operations that call switch_to() directly (e.g. duplicate_figure),
    // bypassing the pending queue.  Detect any mismatch and update.
    {
        FigureId mgr_active = fig_mgr.active_index();
        if (mgr_active != active_figure_id)
        {
            active_figure_id = mgr_active;
            Figure* fig = registry_.get(active_figure_id);
            if (fig)
            {
                fs.active_figure = fig;
                active_figure = fig;
                scheduler.set_target_fps(active_figure->anim_fps_);
                has_animation = static_cast<bool>(active_figure->anim_on_frame_);
    #ifdef SPECTRA_USE_GLFW
                input_handler.set_figure(active_figure);
                if (!active_figure->axes().empty() && active_figure->axes()[0])
                {
                    input_handler.set_active_axes(active_figure->axes()[0].get());
                    const auto& vp = active_figure->axes()[0]->viewport();
                    input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
    #endif
            }
        }
    }

    // Sync root pane's figure_indices_ with actual figures when not split.
    // The unified pane tab headers always read from the root pane.
    if (!dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root && root->is_leaf())
        {
            // Ensure root has exactly the right figures
            const auto& current = root->figure_indices();
            const auto& mgr_ids = fig_mgr.figure_ids();
            bool needs_sync_dock = (current.size() != mgr_ids.size());
            if (!needs_sync_dock)
            {
                for (auto id : mgr_ids)
                {
                    if (!root->has_figure(id))
                    {
                        needs_sync_dock = true;
                        break;
                    }
                }
            }
            if (needs_sync_dock)
            {
                // Rebuild figure_indices_ to match actual figures
                while (root->figure_count() > 0)
                {
                    root->remove_figure(root->figure_indices().back());
                }
                for (auto id : mgr_ids)
                {
                    root->add_figure(id);
                }
            }
            // Sync active tab
            size_t active = fig_mgr.active_index();

            // If the active figure is being torn off, switch to the next
            // available figure so the source window shows different content.
            if (imgui_ui)
            {
                FigureId tearoff = imgui_ui->tearoff_figure();
                if (tearoff != INVALID_FIGURE_ID && active == tearoff)
                {
                    for (auto id : mgr_ids)
                    {
                        if (id != tearoff)
                        {
                            active = id;
                            break;
                        }
                    }
                }
            }

            dock_system.set_active_figure_index(active);
            for (size_t li = 0; li < root->figure_indices().size(); ++li)
            {
                if (root->figure_indices()[li] == active)
                {
                    root->set_active_local_index(li);
                    break;
                }
            }
        }
    }
#endif

    // Compute subplot layout AFTER build_ui() so that nav rail / inspector
    // toggles from the current frame are immediately reflected.
    {
        if (profiler)
            profiler->begin_stage("scene_update");
#ifdef SPECTRA_USE_IMGUI
        if (imgui_ui)
        {
            const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();

            // Update dock system layout with current canvas bounds
            dock_system.update_layout(canvas);

            if (dock_system.is_split())
            {
                // Per-pane layout: each pane renders its own figure
                auto pane_infos = dock_system.get_pane_infos();
                for (const auto& pinfo : pane_infos)
                {
                    {
                        auto* fig = registry_.get(pinfo.figure_index);
                        if (!fig)
                            continue;
                        // Use figure's style margins, clamped to fit pane bounds
                        const auto& fs_style = fig->style();
                        Margins pane_margins;
                        pane_margins.left = std::min(fs_style.margin_left, pinfo.bounds.w * 0.3f);
                        pane_margins.right = std::min(fs_style.margin_right, pinfo.bounds.w * 0.2f);
                        pane_margins.bottom =
                            std::min(fs_style.margin_bottom, pinfo.bounds.h * 0.3f);
                        pane_margins.top = std::min(fs_style.margin_top, pinfo.bounds.h * 0.2f);
                        const auto rects = compute_subplot_layout(pinfo.bounds.w,
                                                                  pinfo.bounds.h,
                                                                  fig->grid_rows_,
                                                                  fig->grid_cols_,
                                                                  pane_margins,
                                                                  pinfo.bounds.x,
                                                                  pinfo.bounds.y);
                        for (size_t i = 0; i < fig->axes_mut().size() && i < rects.size(); ++i)
                        {
                            if (fig->axes_mut()[i])
                            {
                                fig->axes_mut()[i]->set_viewport(rects[i]);
                            }
                        }
                        for (size_t i = 0; i < fig->all_axes_mut().size() && i < rects.size(); ++i)
                        {
                            if (fig->all_axes_mut()[i])
                            {
                                fig->all_axes_mut()[i]->set_viewport(rects[i]);
                            }
                        }
                    }
                }
            }
            else
            {
                SplitPane* root = dock_system.split_view().root();
                Rect cb = (root && root->is_leaf()) ? root->content_bounds() : canvas;
                // Use figure's style margins for layout
                const auto& af_style = active_figure->style();
                Margins fig_margins;
                fig_margins.left = af_style.margin_left;
                fig_margins.right = af_style.margin_right;
                fig_margins.top = af_style.margin_top;
                fig_margins.bottom = af_style.margin_bottom;
                const auto rects = compute_subplot_layout(cb.w,
                                                          cb.h,
                                                          active_figure->grid_rows_,
                                                          active_figure->grid_cols_,
                                                          fig_margins,
                                                          cb.x,
                                                          cb.y);

                for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i)
                {
                    if (active_figure->axes_mut()[i])
                    {
                        active_figure->axes_mut()[i]->set_viewport(rects[i]);
                    }
                }
                for (size_t i = 0; i < active_figure->all_axes_mut().size() && i < rects.size();
                     ++i)
                {
                    if (active_figure->all_axes_mut()[i])
                    {
                        active_figure->all_axes_mut()[i]->set_viewport(rects[i]);
                    }
                }
            }
        }
        else
        {
            active_figure->compute_layout();
        }
#else
        active_figure->compute_layout();
#endif
        if (profiler)
            profiler->end_stage("scene_update");
    }
}

// ─── render ───────────────────────────────────────────────────────────────────
// Render one window: begin_frame, render pass, figure content, ImGui, end_frame.
// Returns true if the frame was successfully presented.
bool WindowRuntime::render(WindowUIContext& ui_ctx, FrameState& fs, FrameProfiler* profiler)
{
    auto* active_figure = fs.active_figure;
    auto& imgui_frame_started = fs.imgui_frame_started;

    // Render frame. If begin_frame fails (OUT_OF_DATE), recreate and
    // retry once so we present content immediately (no black-flash gap).
    if (profiler)
        profiler->begin_stage("begin_frame");
    bool frame_ok = backend_.begin_frame(profiler);
    if (profiler)
        profiler->end_stage("begin_frame");

    if (!frame_ok)
    {
        // Swapchain truly unusable — recreate and retry
#ifdef SPECTRA_USE_IMGUI
        if (imgui_frame_started)
        {
            ImGui::EndFrame();
            imgui_frame_started = false;
        }
#endif
        // Try to recover by recreating swapchain using actual framebuffer size
        auto* vk = static_cast<VulkanBackend*>(&backend_);
        auto* aw = vk->active_window();
        uint32_t target_w = 0, target_h = 0;
#ifdef SPECTRA_USE_GLFW
        if (aw && aw->glfw_window)
        {
            int fb_w = 0, fb_h = 0;
            glfwGetFramebufferSize(static_cast<GLFWwindow*>(aw->glfw_window), &fb_w, &fb_h);
            if (fb_w > 0 && fb_h > 0)
            {
                target_w = static_cast<uint32_t>(fb_w);
                target_h = static_cast<uint32_t>(fb_h);
            }
        }
#endif
        if (target_w == 0 || target_h == 0)
        {
            if (aw)
            {
                target_w = aw->pending_width;
                target_h = aw->pending_height;
            }
        }
        if (aw && target_w > 0 && target_h > 0)
        {
            SPECTRA_LOG_INFO("resize",
                             "OUT_OF_DATE, recreating: " + std::to_string(target_w) + "x"
                                 + std::to_string(target_h));
            if (profiler)
                profiler->increment_counter("swapchain_recreate");
            aw->swapchain_invalidated = false;
            backend_.recreate_swapchain(target_w, target_h);
            vk->clear_swapchain_dirty();
            active_figure->config_.width = backend_.swapchain_width();
            active_figure->config_.height = backend_.swapchain_height();
            ui_ctx.needs_resize = false;
#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx.imgui_ui)
            {
                ui_ctx.imgui_ui->on_swapchain_recreated(*vk);
            }
#endif
            // Retry begin_frame with the new swapchain
            frame_ok = backend_.begin_frame(profiler);
        }
    }

    if (frame_ok)
    {
        // begin_frame() just waited on the in-flight fence, so all GPU
        // work from DELETION_RING_SIZE frames ago is guaranteed complete.
        // Safe to free those deferred resources now.
        renderer_.flush_pending_deletions();

        renderer_.begin_render_pass();

        if (profiler)
            profiler->begin_stage("cmd_record");
#ifdef SPECTRA_USE_IMGUI
        auto& dock_system = ui_ctx.dock_system;
        if (dock_system.is_split())
        {
            auto pane_infos = dock_system.get_pane_infos();
            for (const auto& pinfo : pane_infos)
            {
                Figure* pfig = registry_.get(pinfo.figure_index);
                if (pfig)
                {
                    renderer_.render_figure_content(*pfig);
                }
            }
        }
        else
        {
            renderer_.render_figure_content(*active_figure);
        }
#else
        renderer_.render_figure_content(*active_figure);
#endif
        if (profiler)
            profiler->end_stage("cmd_record");

        // Flush Vulkan plot text BEFORE ImGui so that UI overlays (command
        // palette, inspector, menus) render on top of plot labels.
        // The ImGui canvas ##window uses NoBackground so it won't overwrite text.
        {
            float sw = static_cast<float>(backend_.swapchain_width());
            float sh = static_cast<float>(backend_.swapchain_height());
            renderer_.render_text(sw, sh);
        }

#ifdef SPECTRA_USE_IMGUI
        // Only render ImGui if we have a valid frame (not a retry frame
        // where we already ended the ImGui frame)
        if (ui_ctx.imgui_ui && imgui_frame_started)
        {
            if (profiler)
                profiler->begin_stage("imgui_render");
            ui_ctx.imgui_ui->render(*static_cast<VulkanBackend*>(&backend_));
            if (profiler)
                profiler->end_stage("imgui_render");
        }
#endif

        renderer_.end_render_pass();
        if (profiler)
            profiler->begin_stage("end_frame");
        backend_.end_frame(profiler);
        if (profiler)
            profiler->end_stage("end_frame");

        // Post-present recovery: if vkQueuePresentKHR returned OUT_OF_DATE,
        // the swapchain is permanently invalidated (Vulkan spec). Recreate
        // now so the next frame's begin_frame() starts with a valid swapchain
        // instead of entering the recovery path every frame (infinite loop).
        {
            auto* vk_post = static_cast<VulkanBackend*>(&backend_);
            auto* aw_post = vk_post->active_window();
            if (aw_post && aw_post->swapchain_invalidated)
            {
                aw_post->swapchain_invalidated = false;
                uint32_t rw = aw_post->swapchain.extent.width;
                uint32_t rh = aw_post->swapchain.extent.height;
#ifdef SPECTRA_USE_GLFW
                if (aw_post->glfw_window)
                {
                    int fb_w = 0, fb_h = 0;
                    glfwGetFramebufferSize(
                        static_cast<GLFWwindow*>(aw_post->glfw_window), &fb_w, &fb_h);
                    if (fb_w > 0 && fb_h > 0)
                    {
                        rw = static_cast<uint32_t>(fb_w);
                        rh = static_cast<uint32_t>(fb_h);
                    }
                }
#endif
                SPECTRA_LOG_DEBUG("resize",
                                  "Post-present OUT_OF_DATE, recreating: " + std::to_string(rw)
                                      + "x" + std::to_string(rh));
                backend_.recreate_swapchain(rw, rh);
                vk_post->clear_swapchain_dirty();
                active_figure->config_.width = backend_.swapchain_width();
                active_figure->config_.height = backend_.swapchain_height();
                ui_ctx.needs_resize = false;
#ifdef SPECTRA_USE_IMGUI
                if (ui_ctx.imgui_ui)
                {
                    ui_ctx.imgui_ui->on_swapchain_recreated(*vk_post);
                }
#endif
            }
        }
    }

    return frame_ok;
}

}  // namespace spectra
