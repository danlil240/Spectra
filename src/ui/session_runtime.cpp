#include "session_runtime.hpp"

#include <spectra/animator.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "command_queue.hpp"
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
#include <string>

namespace spectra
{

SessionRuntime::SessionRuntime(Backend& backend, Renderer& renderer, FigureRegistry& registry)
    : backend_(backend), renderer_(renderer), registry_(registry),
      win_rt_(backend, renderer, registry)
{
}

SessionRuntime::~SessionRuntime() = default;

void SessionRuntime::queue_detach(PendingDetach pd)
{
    pending_detaches_.push_back(std::move(pd));
}

FrameState SessionRuntime::tick(
    FrameScheduler& scheduler,
    Animator& animator,
    CommandQueue& cmd_queue,
    bool headless,
    WindowUIContext* headless_ui_ctx,
#ifdef SPECTRA_USE_GLFW
    WindowManager* window_mgr,
#endif
    FrameState& frame_state)
{
    newly_created_window_ids_.clear();

    try
    {
        scheduler.begin_frame();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_CRITICAL("main_loop", "Frame scheduler failed: " + std::string(e.what()));
        running_ = false;
        return frame_state;
    }

    // Drain command queue (apply app-thread mutations)
    size_t commands_processed = cmd_queue.drain();
    if (commands_processed > 0)
    {
        SPECTRA_LOG_TRACE("main_loop",
                         "Processed " + std::to_string(commands_processed) + " commands");
    }

    // Evaluate keyframe animations
    animator.evaluate(scheduler.elapsed_seconds());

    // ── Unified window update + render loop ───────────────────────
#ifdef SPECTRA_USE_GLFW
    if (window_mgr)
    {
        auto* vk = static_cast<VulkanBackend*>(&backend_);

        for (auto* wctx : window_mgr->windows())
        {
            if (wctx->should_close)
                continue;

            // Skip windows created this frame
            if (std::find(newly_created_window_ids_.begin(),
                          newly_created_window_ids_.end(),
                          wctx->id) != newly_created_window_ids_.end())
                continue;

            // Handle minimized window (0x0 framebuffer): skip until restored
            if (wctx->glfw_window)
            {
                int fb_w = 0, fb_h = 0;
                glfwGetFramebufferSize(static_cast<GLFWwindow*>(wctx->glfw_window), &fb_w, &fb_h);
                if (fb_w <= 0 || fb_h <= 0)
                    continue;
            }

            if (!wctx->ui_ctx)
            {
                // Legacy window (no ImGui, figure-only) — skip for now,
                // caller handles via render_secondary_window.
                continue;
            }

            // Set active window for Vulkan operations
            vk->set_active_window(wctx);

            // Switch to this window's ImGui context
            if (wctx->imgui_context)
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));

            // Sync WindowContext resize state → UIContext resize fields
            if (wctx->needs_resize)
            {
                wctx->ui_ctx->needs_resize = true;
                wctx->ui_ctx->new_width = wctx->pending_width;
                wctx->ui_ctx->new_height = wctx->pending_height;
                wctx->ui_ctx->resize_requested_time = wctx->resize_time;
                wctx->needs_resize = false;
            }

            // Build per-window FrameState
            FrameState win_fs;
            win_fs.active_figure_id = wctx->active_figure_id;
            win_fs.active_figure = registry_.get(win_fs.active_figure_id);
            if (!win_fs.active_figure)
                continue;
            win_fs.has_animation = static_cast<bool>(win_fs.active_figure->anim_on_frame_);

            win_rt_.update(*wctx->ui_ctx, win_fs, scheduler, window_mgr);
            win_rt_.render(*wctx->ui_ctx, win_fs);

            // Sync active figure back to WindowContext so the next frame
            // reads the correct figure (tab switch via FigureManager updates
            // win_fs.active_figure_id but nothing wrote it back to wctx).
            wctx->active_figure_id = win_fs.active_figure_id;

            // Sync back to the app-level frame_state for the initial window
            if (wctx == window_mgr->windows()[0])
            {
                frame_state = win_fs;
            }
        }
    }
#endif

    // Headless path (no GLFW, no WindowManager)
    if (headless && headless_ui_ctx)
    {
        win_rt_.update(*headless_ui_ctx, frame_state, scheduler
#ifdef SPECTRA_USE_GLFW
                       , nullptr
#endif
        );
        win_rt_.render(*headless_ui_ctx, frame_state);
    }

    // ── Process deferred detach requests ─────────────────────────
#ifdef SPECTRA_USE_GLFW
    if (window_mgr && !pending_detaches_.empty())
    {
        auto* src_wctx = window_mgr->focused_window();
        if (!src_wctx && !window_mgr->windows().empty())
            src_wctx = window_mgr->windows()[0];

        for (auto& pd : pending_detaches_)
        {
            if (!src_wctx || !src_wctx->ui_ctx || !src_wctx->ui_ctx->fig_mgr)
                continue;
            auto* src_fm = src_wctx->ui_ctx->fig_mgr;
            if (src_fm->count() <= 1)
                continue;

            FigureState detached_state = src_fm->remove_figure(pd.figure_id);

            auto& pf = src_wctx->assigned_figures;
            pf.erase(std::remove(pf.begin(), pf.end(), pd.figure_id), pf.end());
            if (src_wctx->active_figure_id == pd.figure_id)
                src_wctx->active_figure_id = pf.empty() ? INVALID_FIGURE_ID : pf.front();

            auto* new_wctx = window_mgr->detach_figure(
                pd.figure_id, pd.width, pd.height,
                pd.title, pd.screen_x, pd.screen_y);

            if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
            {
                auto* new_fm = new_wctx->ui_ctx->fig_mgr;
                new_fm->state(pd.figure_id) = std::move(detached_state);
                std::string correct_title = new_fm->get_title(pd.figure_id);
                if (new_fm->tab_bar())
                    new_fm->tab_bar()->set_tab_title(0, correct_title);
            }

            frame_state.active_figure_id = src_fm->active_index();
            frame_state.active_figure = registry_.get(frame_state.active_figure_id);

            if (new_wctx)
                newly_created_window_ids_.push_back(new_wctx->id);
        }
        pending_detaches_.clear();
    }
#endif

    scheduler.end_frame();

    // ── Poll events + check exit ─────────────────────────────────
#ifdef SPECTRA_USE_GLFW
    if (window_mgr)
    {
        window_mgr->poll_events();
        window_mgr->process_pending_closes();

        if (!window_mgr->any_window_open())
        {
            SPECTRA_LOG_INFO("main_loop", "All windows closed, exiting loop");
            running_ = false;
        }
    }
#endif

    // Headless without animation: single frame
    if (headless && frame_state.active_figure && !frame_state.has_animation)
    {
        SPECTRA_LOG_INFO("main_loop", "Headless single frame mode, exiting loop");
        running_ = false;
    }

    return frame_state;
}

}  // namespace spectra
