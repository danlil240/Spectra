#include "detachable_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#ifdef IMGUI_HAS_DOCK
#include <imgui_internal.h>
#endif
#endif

#ifdef SPECTRA_USE_GLFW
#include "render/vulkan/window_context.hpp"
#include "ui/window/window_manager.hpp"
#endif

namespace spectra::ui
{

DetachablePanel::~DetachablePanel()
{
    // Direct destruction is safe here — we are not inside the
    // session_runtime window iteration loop during teardown.
#ifdef SPECTRA_USE_GLFW
    if (panel_window_id_ != 0 && window_mgr_)
    {
        window_mgr_->destroy_panel_window(panel_window_id_);
        panel_window_id_ = 0;
    }
#endif
}

// ---------------------------------------------------------------------------
// draw  (called from main window's ImGui frame each tick)
// ---------------------------------------------------------------------------

void DetachablePanel::draw(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI

    // ── If the panel lives in its own OS window, skip main-window rendering.
#ifdef SPECTRA_USE_GLFW
    if (panel_window_id_ != 0 && window_mgr_)
    {
        auto* wctx = window_mgr_->find_window(panel_window_id_);
        if (!wctx || wctx->should_close)
        {
            // OS window was closed (user clicked X on the panel window).
            // Queue deferred destruction via process_pending(); reset local
            // state so the panel draws docked on this frame.
            wants_destroy_window_ = true;
            panel_window_id_      = 0;
            detached_             = false;
            // Fall through to docked drawing below.
        }
        else
        {
            // Panel content is rendered by session_runtime in its OS window.
            return;
        }
    }
#endif

    // ── Detach requested but window not yet created ─────────────────────
    // process_pending() will create it after app.step() returns.
    // Skip rendering in the main window for one frame so the dock slot
    // is released and the panel does not flicker in both places.
    if (wants_create_window_)
        return;

    // ── After attach: force the window back into the target dockspace.
#ifdef IMGUI_HAS_DOCK
    if (wants_destroy_window_ && dock_id_ != 0)
    {
        ImGui::SetNextWindowDockID(
            static_cast<ImGuiID>(dock_id_), ImGuiCond_Always);
    }
#endif

    ImGuiWindowFlags flags = static_cast<ImGuiWindowFlags>(extra_window_flags());

    if (!ImGui::Begin(title_.c_str(), p_open, flags))
    {
        ImGui::End();
        return;
    }

    draw_context_menu();
    draw_content();

    ImGui::End();

#else
    (void)p_open;
#endif   // SPECTRA_USE_IMGUI
}

// ---------------------------------------------------------------------------
// process_pending  — called AFTER app.step(), outside window iteration
// ---------------------------------------------------------------------------

void DetachablePanel::process_pending()
{
#ifdef SPECTRA_USE_GLFW
    // ── Destroy panel OS window ──────────────────────────────────────────
    if (wants_destroy_window_)
    {
        wants_destroy_window_ = false;

        if (panel_window_id_ != 0 && window_mgr_)
        {
            window_mgr_->destroy_panel_window(panel_window_id_);
            panel_window_id_ = 0;
        }
        detached_ = false;
    }

    // ── Create panel OS window ───────────────────────────────────────────
    if (wants_create_window_)
    {
        wants_create_window_ = false;

        if (window_mgr_)
        {
            auto* wctx = window_mgr_->create_panel_window(
                static_cast<uint32_t>(detached_width_),
                static_cast<uint32_t>(detached_height_),
                title_,
                [this]() { draw_content(); },
                create_screen_x_, create_screen_y_);
            if (wctx)
            {
                panel_window_id_ = wctx->id;
                detached_        = true;
                return;
            }
        }
        // Fallback: window creation failed — stay docked.
        detached_ = false;
    }
#endif
}

// ---------------------------------------------------------------------------
// detach / attach
// ---------------------------------------------------------------------------

void DetachablePanel::detach()
{
    if (panel_window_id_ != 0 || wants_create_window_)
        return;

    create_screen_x_ = 100;
    create_screen_y_ = 100;

#ifdef SPECTRA_USE_IMGUI
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp)
    {
        create_screen_x_ =
            static_cast<int>(vp->Pos.x + vp->Size.x * 0.5f - detached_width_ * 0.5f);
        create_screen_y_ =
            static_cast<int>(vp->Pos.y + vp->Size.y * 0.5f - detached_height_ * 0.5f);
    }
#endif

    wants_create_window_ = true;
}

void DetachablePanel::attach()
{
    if (panel_window_id_ == 0 || wants_destroy_window_)
        return;
    wants_destroy_window_ = true;
}

// ---------------------------------------------------------------------------
// draw_context_menu
// ---------------------------------------------------------------------------

void DetachablePanel::draw_context_menu()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::BeginPopupContextWindow("##DetachMenu"))
    {
        if (panel_window_id_ != 0)
        {
            if (ImGui::MenuItem("Attach to Dockspace"))
                attach();
        }
        else
        {
            if (ImGui::MenuItem("Detach to Window"))
                detach();
        }
        ImGui::EndPopup();
    }
#endif
}

}   // namespace spectra::ui
