#ifdef SPECTRA_USE_IMGUI

    #include "panel_detach_controller.hpp"

    #include <spectra/logger.hpp>

    #include "render/vulkan/window_context.hpp"
    #include "ui/window/window_manager.hpp"

namespace spectra
{

// ─── Actions ────────────────────────────────────────────────────────────────

void PanelDetachController::detach()
{
    if (state_ != State::Docked)
        return;

    state_ = State::DetachPending;
}

void PanelDetachController::attach()
{
    if (state_ != State::Detached)
        return;

    state_ = State::AttachPending;
}

// ─── Frame update ───────────────────────────────────────────────────────────

void PanelDetachController::update()
{
    if (state_ != State::Detached)
        return;

    if (!is_window_alive())
    {
        // OS window was closed externally (user clicked X).
        panel_window_id_ = 0;
        state_           = State::AttachPending;
    }
}

// ─── Deferred operations ────────────────────────────────────────────────────

void PanelDetachController::process_pending()
{
    switch (state_)
    {
        case State::DetachPending:
            execute_create_window();
            break;

        case State::AttachPending:
            execute_destroy_window();
            break;

        case State::Docked:
        case State::Detached:
            break;
    }
}

// ─── State transitions ──────────────────────────────────────────────────────

void PanelDetachController::transition_to_docked()
{
    panel_window_id_ = 0;
    state_           = State::Docked;

    if (on_attached_)
        on_attached_();
}

void PanelDetachController::transition_to_detached(uint32_t window_id)
{
    panel_window_id_ = window_id;
    state_           = State::Detached;

    if (on_detached_)
        on_detached_(window_id);
}

void PanelDetachController::execute_create_window()
{
    #ifdef SPECTRA_USE_GLFW
    if (!window_mgr_ || !draw_callback_)
    {
        state_ = State::Docked;
        return;
    }

    auto* wctx = window_mgr_->create_panel_window(static_cast<uint32_t>(detached_width_),
                                                  static_cast<uint32_t>(detached_height_),
                                                  title_,
                                                  draw_callback_,
                                                  screen_x_,
                                                  screen_y_);

    if (wctx)
    {
        SPECTRA_LOG_INFO(
            "panel_detach",
            "Created panel window " + std::to_string(wctx->id) + ": \"" + title_ + "\"");
        transition_to_detached(wctx->id);
    }
    else
    {
        SPECTRA_LOG_ERROR("panel_detach", "Failed to create panel window: \"" + title_ + "\"");
        state_ = State::Docked;
    }
    #else
    state_ = State::Docked;
    #endif
}

void PanelDetachController::execute_destroy_window()
{
    #ifdef SPECTRA_USE_GLFW
    if (panel_window_id_ != 0 && window_mgr_)
    {
        SPECTRA_LOG_INFO(
            "panel_detach",
            "Destroying panel window " + std::to_string(panel_window_id_) + ": \"" + title_ + "\"");
        window_mgr_->destroy_panel_window(panel_window_id_);
    }
    #endif
    transition_to_docked();
}

// ─── Cursor position ────────────────────────────────────────────────────────

bool PanelDetachController::get_screen_cursor(double& sx, double& sy) const
{
    if (window_mgr_)
        return window_mgr_->get_global_cursor_pos(sx, sy);
    sx = sy = 0;
    return false;
}

// ─── Window liveness check ──────────────────────────────────────────────────

bool PanelDetachController::is_window_alive() const
{
    if (panel_window_id_ == 0 || !window_mgr_)
        return false;

    auto* wctx = window_mgr_->find_window(panel_window_id_);
    return wctx && !wctx->should_close;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
