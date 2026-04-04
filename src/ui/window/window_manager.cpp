#include "window_manager.hpp"

#include <spectra/event_bus.hpp>
#include <spectra/logger.hpp>

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/theme/theme.hpp"
#include "io/export_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "glfw_utils.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_impl_glfw.h>

    #include "ui/app/register_commands.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include <spectra/figure_registry.hpp>
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/figures/tab_bar.hpp"
#endif

#include <algorithm>

namespace spectra
{

void WindowManager::poll_events()
{
#ifdef SPECTRA_USE_GLFW
    glfwPollEvents();
#endif
}

void WindowManager::wait_events_timeout(double timeout_seconds)
{
#ifdef SPECTRA_USE_GLFW
    glfwWaitEventsTimeout(timeout_seconds);
#endif
}

WindowContext* WindowManager::focused_window() const
{
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close && wctx->is_focused)
            return wctx.get();
    }

    // Fallback: return first open window
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
            return wctx.get();
    }

    return nullptr;
}

bool WindowManager::any_window_open() const
{
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
            return true;
    }
    return false;
}

WindowContext* WindowManager::find_window(uint32_t window_id) const
{
    for (auto& wctx : windows_)
    {
        if (wctx->id == window_id)
            return wctx.get();
    }
    return nullptr;
}

void WindowManager::clear_figure_caches(Figure* fig)
{
#ifdef SPECTRA_USE_IMGUI
    if (!fig)
        return;

    for (auto* wctx : active_ptrs_)
    {
        if (!wctx || !wctx->ui_ctx)
            continue;
        if (wctx->ui_ctx->data_interaction)
            wctx->ui_ctx->data_interaction->clear_figure_cache(fig);
        wctx->ui_ctx->input_handler.clear_figure_cache(fig);
        if (wctx->ui_ctx->imgui_ui)
            wctx->ui_ctx->imgui_ui->clear_figure_cache(fig);
    }
#else
    (void)fig;
#endif
}

// --- Private helpers ---

void WindowManager::rebuild_active_list()
{
    active_ptrs_.clear();

    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
        {
            active_ptrs_.push_back(wctx.get());
        }
    }
}

#ifdef SPECTRA_USE_GLFW

void WindowManager::set_window_position(WindowContext& wctx, int x, int y)
{
    if (wctx.glfw_window)
    {
        glfwSetWindowPos(static_cast<GLFWwindow*>(wctx.glfw_window), x, y);
    }
}

#else

void WindowManager::set_window_position(WindowContext& /*wctx*/, int /*x*/, int /*y*/) {}

#endif

// ── Detached-panel window ───────────────────────────────────────────────────

// ── Tearoff preview window ──────────────────────────────────────────────────

bool WindowManager::is_mouse_button_held(int glfw_button) const
{
    // When callback-based tracking is active (during a tab drag), use
    // the tracked state.  Polling glfwGetMouseButton gives false RELEASE
    // on X11 after creating a new GLFW window because poll_events()
    // processes X11 events from the window creation in the same frame.
    // The callback only fires for real ButtonRelease X11 events.
    if (mouse_release_tracking_ && glfw_button == GLFW_MOUSE_BUTTON_LEFT)
        return !mouse_release_seen_;

#ifdef SPECTRA_USE_GLFW
    for (auto& wctx : windows_)
    {
        if (wctx->glfw_window && !wctx->should_close)
        {
            if (glfwGetMouseButton(static_cast<GLFWwindow*>(wctx->glfw_window), glfw_button)
                == GLFW_PRESS)
                return true;
        }
    }
#endif
    return false;
}

void WindowManager::begin_mouse_release_tracking()
{
    mouse_release_tracking_ = true;
    mouse_release_seen_     = false;
}

void WindowManager::end_mouse_release_tracking()
{
    mouse_release_tracking_ = false;
    mouse_release_seen_     = false;
}

bool WindowManager::get_global_cursor_pos(double& screen_x, double& screen_y) const
{
#ifdef SPECTRA_USE_GLFW
    // Try focused window first, then fall back to the first open window.
    WindowContext* wctx = nullptr;
    for (auto& w : windows_)
    {
        if (w->glfw_window && !w->should_close && w->is_focused)
        {
            wctx = w.get();
            break;
        }
    }
    if (!wctx)
    {
        for (auto& w : windows_)
        {
            if (w->glfw_window && !w->should_close)
            {
                wctx = w.get();
                break;
            }
        }
    }
    if (!wctx)
        return false;

    auto*  glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    double cx, cy;
    glfwGetCursorPos(glfw_win, &cx, &cy);
    int wx, wy;
    glfwGetWindowPos(glfw_win, &wx, &wy);
    screen_x = wx + cx;
    screen_y = wy + cy;
    return true;
#else
    (void)screen_x;
    (void)screen_y;
    return false;
#endif
}

}   // namespace spectra
