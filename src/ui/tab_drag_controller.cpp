#ifdef SPECTRA_USE_IMGUI

#include "tab_drag_controller.hpp"

#include <cmath>

#include "dock_system.hpp"
#include "window_manager.hpp"
#include "../render/vulkan/window_context.hpp"

struct GLFWwindow;

// GLFW functions needed for screen-space window queries
extern "C"
{
    void glfwGetWindowPos(GLFWwindow* window, int* xpos, int* ypos);
    void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
}

namespace spectra
{

// ─── Input events ────────────────────────────────────────────────────────────

void TabDragController::on_mouse_down(uint32_t source_pane_id, FigureId figure_id,
                                       float mouse_x, float mouse_y)
{
    if (state_ != State::Idle)
        return;

    state_ = State::DragStartCandidate;
    source_pane_id_ = source_pane_id;
    figure_id_ = figure_id;
    start_mouse_x_ = mouse_x;
    start_mouse_y_ = mouse_y;
    current_mouse_x_ = mouse_x;
    current_mouse_y_ = mouse_y;
    cross_pane_ = false;
    dock_dragging_ = false;
}

void TabDragController::update(float mouse_x, float mouse_y, bool mouse_down,
                                float screen_mouse_x, float screen_mouse_y)
{
    current_mouse_x_ = mouse_x;
    current_mouse_y_ = mouse_y;
    current_screen_x_ = screen_mouse_x;
    current_screen_y_ = screen_mouse_y;

    switch (state_)
    {
    case State::Idle:
        // Nothing to do.
        break;

    case State::DragStartCandidate:
    {
        if (!mouse_down)
        {
            // Mouse released before threshold — treat as click, not drag.
            transition_to_idle();
            return;
        }

        float dx = mouse_x - start_mouse_x_;
        float dy = mouse_y - start_mouse_y_;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist > drag_threshold_)
        {
            transition_to_dragging();
        }
        break;
    }

    case State::DraggingDetached:
    {
        if (!mouse_down)
        {
            // Mouse released — destroy preview and execute drop.
            if (preview_created_ && window_manager_)
            {
                window_manager_->request_destroy_preview();
                preview_created_ = false;
            }
            preview_grace_frames_ = 0;

            // Mouse released — determine drop target.
            if (is_outside_all_windows(screen_mouse_x, screen_mouse_y))
            {
                execute_drop_outside(screen_mouse_x, screen_mouse_y);
            }
            else
            {
                // Check if dropped on a different window (cross-window move)
                uint32_t target_wid = find_window_at(screen_mouse_x, screen_mouse_y);
                if (target_wid != 0 && target_wid != source_window_id_
                    && on_drop_on_window_)
                {
                    if (dock_dragging_ && dock_system_)
                        dock_system_->cancel_drag();
                    on_drop_on_window_(figure_id_, target_wid,
                                       screen_mouse_x, screen_mouse_y);
                    transition_to_idle();
                }
                else
                {
                    execute_drop_inside(mouse_x, mouse_y);
                }
            }
            return;
        }

        // Check vertical displacement for dock-drag mode.
        float dy = mouse_y - start_mouse_y_;
        if (!dock_dragging_ && std::abs(dy) > dock_drag_threshold_)
        {
            dock_dragging_ = true;
            if (dock_system_)
            {
                dock_system_->begin_drag(figure_id_, mouse_x, mouse_y);
            }
        }

        // Preview window creation uses a delay: once the tearoff threshold
        // is exceeded, we wait a few frames (grace period) with the mouse
        // still held before actually creating the OS window.  This prevents
        // a brief flash for fast drag-and-release gestures.
        constexpr float TEAROFF_THRESHOLD = 25.0f;
        if (!preview_created_ && std::abs(dy) > TEAROFF_THRESHOLD && window_manager_)
        {
            if (preview_grace_frames_ == 0)
            {
                // First frame past threshold — start the grace countdown.
                preview_grace_frames_ = 4;
            }
            else
            {
                --preview_grace_frames_;
                if (preview_grace_frames_ == 0)
                {
                    // Grace period expired and mouse is still held — create preview.
                    constexpr uint32_t PREVIEW_W = 280;
                    constexpr uint32_t PREVIEW_H = 200;
                    window_manager_->request_preview_window(
                        PREVIEW_W, PREVIEW_H,
                        static_cast<int>(screen_mouse_x),
                        static_cast<int>(screen_mouse_y),
                        ghost_title_);
                    preview_created_ = true;
                }
            }
        }

        // Move preview window to follow mouse
        if (preview_created_ && window_manager_)
        {
            window_manager_->move_preview_window(
                static_cast<int>(screen_mouse_x),
                static_cast<int>(screen_mouse_y));
        }

        // Forward to dock system if in dock-drag mode.
        if (dock_dragging_ && dock_system_)
        {
            dock_system_->update_drag(mouse_x, mouse_y);
        }

        break;
    }
    }
}

void TabDragController::cancel()
{
    if (state_ == State::Idle)
        return;

    // Request deferred preview window destruction on cancel
    if (preview_created_ && window_manager_)
    {
        window_manager_->request_destroy_preview();
        preview_created_ = false;
    }

    execute_cancel();
}

// ─── State transitions ──────────────────────────────────────────────────────

void TabDragController::transition_to_idle()
{
    state_ = State::Idle;
    figure_id_ = INVALID_FIGURE_ID;
    source_pane_id_ = 0;
    cross_pane_ = false;
    dock_dragging_ = false;
    preview_created_ = false;
    preview_grace_frames_ = 0;
    ghost_title_.clear();
}

void TabDragController::transition_to_dragging()
{
    state_ = State::DraggingDetached;
}

void TabDragController::execute_drop_inside(float mouse_x, float mouse_y)
{
    if (dock_dragging_ && dock_system_)
    {
        // Let the dock system handle the split/dock operation.
        dock_system_->end_drag(mouse_x, mouse_y);
    }

    if (on_drop_inside_)
    {
        on_drop_inside_(figure_id_, mouse_x, mouse_y);
    }

    transition_to_idle();
}

void TabDragController::execute_drop_outside(float screen_x, float screen_y)
{
    if (dock_dragging_ && dock_system_)
    {
        dock_system_->cancel_drag();
    }

    if (on_drop_outside_)
    {
        on_drop_outside_(figure_id_, screen_x, screen_y);
    }

    transition_to_idle();
}

void TabDragController::execute_cancel()
{
    if (dock_dragging_ && dock_system_)
    {
        dock_system_->cancel_drag();
    }

    FigureId fig = figure_id_;

    transition_to_idle();

    if (on_cancel_)
    {
        on_cancel_(fig);
    }
}

// ─── Window hit-testing ─────────────────────────────────────────────────────

bool TabDragController::is_outside_all_windows(float screen_x, float screen_y) const
{
    if (!window_manager_)
    {
        // Fallback: no window manager means single-window mode.
        // Cannot determine "outside" without window list — assume inside.
        return false;
    }

    for (auto* wctx : window_manager_->windows())
    {
        if (!wctx || !wctx->glfw_window || wctx->is_preview)
            continue;

        int wx = 0, wy = 0, ww = 0, wh = 0;
        glfwGetWindowPos(static_cast<GLFWwindow*>(wctx->glfw_window), &wx, &wy);
        glfwGetWindowSize(static_cast<GLFWwindow*>(wctx->glfw_window), &ww, &wh);

        if (screen_x >= static_cast<float>(wx)
            && screen_x < static_cast<float>(wx + ww)
            && screen_y >= static_cast<float>(wy)
            && screen_y < static_cast<float>(wy + wh))
        {
            return false;  // Inside this window
        }
    }

    return true;
}

uint32_t TabDragController::find_window_at(float screen_x, float screen_y) const
{
    if (!window_manager_)
        return 0;

    for (auto* wctx : window_manager_->windows())
    {
        if (!wctx || !wctx->glfw_window || wctx->is_preview)
            continue;

        int wx = 0, wy = 0, ww = 0, wh = 0;
        glfwGetWindowPos(static_cast<GLFWwindow*>(wctx->glfw_window), &wx, &wy);
        glfwGetWindowSize(static_cast<GLFWwindow*>(wctx->glfw_window), &ww, &wh);

        if (screen_x >= static_cast<float>(wx)
            && screen_x < static_cast<float>(wx + ww)
            && screen_y >= static_cast<float>(wy)
            && screen_y < static_cast<float>(wy + wh))
        {
            return wctx->id;
        }
    }

    return 0;
}

}  // namespace spectra

#endif  // SPECTRA_USE_IMGUI
