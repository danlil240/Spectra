#ifdef SPECTRA_USE_IMGUI

    #include "tab_drag_controller.hpp"

    #include <cmath>
    #include <cstdio>
    #include <spectra/logger.hpp>

    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>

    #include "../render/vulkan/window_context.hpp"
    #include "dock_system.hpp"
    #include "window_manager.hpp"

namespace spectra
{

// ─── Input events ────────────────────────────────────────────────────────────

void TabDragController::on_mouse_down(uint32_t source_pane_id,
                                      FigureId figure_id,
                                      float    mouse_x,
                                      float    mouse_y)
{
    if (state_ != State::Idle)
        return;

    state_           = State::DragStartCandidate;
    source_pane_id_  = source_pane_id;
    figure_id_       = figure_id;
    start_mouse_x_   = mouse_x;
    start_mouse_y_   = mouse_y;
    current_mouse_x_ = mouse_x;
    current_mouse_y_ = mouse_y;
    cross_pane_      = false;
    dock_dragging_   = false;

    // Begin callback-based mouse release tracking so we don't lose
    // the button state when creating preview windows on X11.
    if (window_manager_)
        window_manager_->begin_mouse_release_tracking();
}

void TabDragController::update(float mouse_x,
                               float mouse_y,
                               bool  mouse_down,
                               float screen_mouse_x,
                               float screen_mouse_y)
{
    current_mouse_x_  = mouse_x;
    current_mouse_y_  = mouse_y;
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
                transition_to_idle();
                return;
            }

            float dx   = mouse_x - start_mouse_x_;
            float dy   = mouse_y - start_mouse_y_;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > drag_threshold_)
            {
                transition_to_dragging();
            }
            break;
        }

        case State::DraggingDetached:
        {
            // ── Track which non-source window the cursor is over (every frame) ──
            // This is done BEFORE the release check so the value is fresh at drop time.
            if (window_manager_)
            {
                uint32_t hovered = 0;
                for (auto* wctx : window_manager_->windows())
                {
                    if (!wctx || !wctx->glfw_window || wctx->is_preview)
                        continue;
                    auto*  win = static_cast<GLFWwindow*>(wctx->glfw_window);
                    double cx, cy;
                    glfwGetCursorPos(win, &cx, &cy);
                    int ww, wh;
                    glfwGetWindowSize(win, &ww, &wh);
                    // Content area check with generous margin for title bar
                    if (cx >= -10 && cx < ww + 10 && cy >= -50 && cy < wh + 10)
                    {
                        hovered = wctx->id;
                        // Prefer the non-source window if cursor is over it
                        if (hovered != source_window_id_)
                            break;
                    }
                }
                if (hovered != 0 && hovered != source_window_id_)
                    last_hovered_window_id_ = hovered;

                // Publish the drag target so target windows can draw dock highlights
                window_manager_->set_drag_target_window(last_hovered_window_id_);

                // Compute directional drop zone on the target window's DockSystem
                if (last_hovered_window_id_ != 0)
                {
                    // Get cursor position in target window's local coordinates
                    auto* target_wctx = window_manager_->find_window(last_hovered_window_id_);
                    if (target_wctx && target_wctx->glfw_window)
                    {
                        auto*  tw = static_cast<GLFWwindow*>(target_wctx->glfw_window);
                        double lcx, lcy;
                        glfwGetCursorPos(tw, &lcx, &lcy);
                        window_manager_->compute_cross_window_drop_zone(last_hovered_window_id_,
                                                                        static_cast<float>(lcx),
                                                                        static_cast<float>(lcy));
                    }
                }
            }

            if (!mouse_down)
            {
                // Mouse released — destroy preview and execute drop.
                if (preview_created_ && window_manager_)
                {
                    window_manager_->request_destroy_preview();
                    preview_created_ = false;
                }

                // Also check at release time for immediate detection
                uint32_t release_target = find_window_at(screen_mouse_x, screen_mouse_y);

                // Use the best available target: prefer release-time detection,
                // fall back to continuously tracked hover
                uint32_t target_wid = 0;
                if (release_target != 0 && release_target != source_window_id_)
                    target_wid = release_target;
                else if (last_hovered_window_id_ != 0
                         && last_hovered_window_id_ != source_window_id_)
                    target_wid = last_hovered_window_id_;

                fprintf(stderr,
                        "[tab_drag] DROP: fig=%lu src=%u target=%u "
                        "(release_at=%u hover=%u) screen=(%.0f,%.0f) "
                        "has_cb=%d\n",
                        static_cast<unsigned long>(figure_id_),
                        source_window_id_,
                        target_wid,
                        release_target,
                        last_hovered_window_id_,
                        screen_mouse_x,
                        screen_mouse_y,
                        on_drop_on_window_ ? 1 : 0);

                // Log all windows
                if (window_manager_)
                {
                    for (auto* wctx : window_manager_->windows())
                    {
                        if (!wctx || !wctx->glfw_window || wctx->is_preview)
                            continue;
                        auto*  win = static_cast<GLFWwindow*>(wctx->glfw_window);
                        double cx, cy;
                        glfwGetCursorPos(win, &cx, &cy);
                        int ww, wh;
                        glfwGetWindowSize(win, &ww, &wh);
                        fprintf(stderr,
                                "[tab_drag]   win %u: cursor_rel=(%.0f,%.0f) "
                                "size=(%d,%d) inside=%d\n",
                                wctx->id,
                                cx,
                                cy,
                                ww,
                                wh,
                                (cx >= 0 && cx < ww && cy >= 0 && cy < wh) ? 1 : 0);
                    }
                }

                if (target_wid != 0 && on_drop_on_window_)
                {
                    fprintf(stderr, "[tab_drag]   → MOVE to window %u\n", target_wid);
                    if (dock_dragging_ && dock_system_)
                        dock_system_->cancel_drag();
                    on_drop_on_window_(figure_id_, target_wid, screen_mouse_x, screen_mouse_y);
                    transition_to_idle();
                }
                else if (is_outside_all_windows(screen_mouse_x, screen_mouse_y))
                {
                    fprintf(stderr, "[tab_drag]   → DETACH (outside all windows)\n");
                    execute_drop_outside(screen_mouse_x, screen_mouse_y);
                }
                else
                {
                    fprintf(stderr, "[tab_drag]   → DROP INSIDE (return to source)\n");
                    execute_drop_inside(mouse_x, mouse_y);
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

            // Create preview window once drag exceeds tearoff threshold
            constexpr float TEAROFF_THRESHOLD = 25.0f;
            if (!preview_created_ && std::abs(dy) > TEAROFF_THRESHOLD && window_manager_)
            {
                constexpr uint32_t PREVIEW_W = 280;
                constexpr uint32_t PREVIEW_H = 200;
                window_manager_->request_preview_window(PREVIEW_W,
                                                        PREVIEW_H,
                                                        static_cast<int>(screen_mouse_x),
                                                        static_cast<int>(screen_mouse_y),
                                                        ghost_title_);
                preview_created_ = true;
            }

            // Move preview window to follow mouse
            if (preview_created_ && window_manager_)
            {
                window_manager_->move_preview_window(static_cast<int>(screen_mouse_x),
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

bool TabDragController::check_mouse_held() const
{
    if (window_manager_)
        return window_manager_->is_mouse_button_held(GLFW_MOUSE_BUTTON_LEFT);
    return false;
}

bool TabDragController::get_screen_cursor(double& sx, double& sy) const
{
    if (window_manager_)
        return window_manager_->get_global_cursor_pos(sx, sy);
    return false;
}

// ─── State transitions ──────────────────────────────────────────────────────

void TabDragController::transition_to_idle()
{
    state_                    = State::Idle;
    figure_id_                = INVALID_FIGURE_ID;
    source_pane_id_           = 0;
    cross_pane_               = false;
    dock_dragging_            = false;
    preview_created_          = false;
    preview_grace_frames_     = 0;
    preview_created_recently_ = 0;
    last_hovered_window_id_   = 0;
    ghost_title_.clear();

    if (window_manager_)
    {
        window_manager_->set_drag_target_window(0);
        window_manager_->end_mouse_release_tracking();
    }
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

bool TabDragController::is_outside_all_windows(float /*screen_x*/, float /*screen_y*/) const
{
    if (!window_manager_)
        return false;

    // Query each window directly via glfwGetCursorPos — this returns the
    // cursor position relative to that window's content area, which is
    // far more reliable than screen-coordinate math on X11 (where
    // glfwGetWindowPos may not account for decorations consistently).
    // Include a margin above the content area for the title bar so that
    // drops near the top of a window are not treated as "outside".
    constexpr double TITLE_BAR_MARGIN = 50.0;
    constexpr double SIDE_MARGIN      = 10.0;

    for (auto* wctx : window_manager_->windows())
    {
        if (!wctx || !wctx->glfw_window || wctx->is_preview)
            continue;

        auto*  win = static_cast<GLFWwindow*>(wctx->glfw_window);
        double cx, cy;
        glfwGetCursorPos(win, &cx, &cy);
        int ww, wh;
        glfwGetWindowSize(win, &ww, &wh);

        if (cx >= -SIDE_MARGIN && cx < ww + SIDE_MARGIN && cy >= -TITLE_BAR_MARGIN
            && cy < wh + SIDE_MARGIN)
        {
            return false;   // Inside (or near) this window
        }
    }

    return true;
}

uint32_t TabDragController::find_window_at(float /*screen_x*/, float /*screen_y*/) const
{
    if (!window_manager_)
        return 0;

    // Query each window directly via glfwGetCursorPos to check if the
    // cursor is inside its content area.  This avoids screen-coordinate
    // math issues on X11 with window decorations.
    for (auto* wctx : window_manager_->windows())
    {
        if (!wctx || !wctx->glfw_window || wctx->is_preview)
            continue;

        auto*  win = static_cast<GLFWwindow*>(wctx->glfw_window);
        double cx, cy;
        glfwGetCursorPos(win, &cx, &cy);
        int ww, wh;
        glfwGetWindowSize(win, &ww, &wh);

        if (cx >= 0 && cx < ww && cy >= 0 && cy < wh)
        {
            return wctx->id;
        }
    }

    return 0;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
