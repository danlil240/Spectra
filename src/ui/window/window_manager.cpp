// WindowManager orchestration helpers kept alongside the split lifecycle,
// figure-op, and callback translation units.

#include "window_manager.hpp"

#include <spectra/logger.hpp>

#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
#endif

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
#else
    (void)timeout_seconds;
#endif
}

WindowContext* WindowManager::focused_window() const
{
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close && wctx->is_focused)
            return wctx.get();
    }

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

void WindowManager::rebuild_active_list()
{
    active_ptrs_.clear();

    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
            active_ptrs_.push_back(wctx.get());
    }
}

#ifdef SPECTRA_USE_GLFW

void WindowManager::set_window_position(WindowContext& wctx, int x, int y)
{
    if (wctx.glfw_window)
        glfwSetWindowPos(static_cast<GLFWwindow*>(wctx.glfw_window), x, y);
}

bool WindowManager::is_mouse_button_held(int glfw_button) const
{
    if (mouse_release_tracking_ && glfw_button == GLFW_MOUSE_BUTTON_LEFT)
        return !mouse_release_seen_;

    for (auto& wctx : windows_)
    {
        if (wctx->glfw_window && !wctx->should_close)
        {
            if (glfwGetMouseButton(static_cast<GLFWwindow*>(wctx->glfw_window), glfw_button)
                == GLFW_PRESS)
                return true;
        }
    }
    return false;
}

bool WindowManager::get_global_cursor_pos(double& screen_x, double& screen_y) const
{
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
    double cx       = 0.0;
    double cy       = 0.0;
    glfwGetCursorPos(glfw_win, &cx, &cy);
    int wx = 0;
    int wy = 0;
    glfwGetWindowPos(glfw_win, &wx, &wy);
    screen_x = wx + cx;
    screen_y = wy + cy;
    return true;
}

#else

void WindowManager::set_window_position(WindowContext&, int, int) {}

bool WindowManager::is_mouse_button_held(int) const
{
    return false;
}

bool WindowManager::get_global_cursor_pos(double& screen_x, double& screen_y) const
{
    (void)screen_x;
    (void)screen_y;
    return false;
}

#endif

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

}   // namespace spectra
