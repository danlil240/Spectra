#include "detachable_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#ifdef IMGUI_HAS_DOCK
#include <imgui_internal.h>
#endif
#endif

namespace spectra::ui
{

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void DetachablePanel::draw(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI

#ifdef IMGUI_HAS_DOCK
    // Handle pending detach: undock and force out of main viewport.
    if (pending_detach_)
    {
        pending_detach_ = false;
        detached_       = true;

        // Place the new OS window near the center of the main viewport.
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f,
                      vp->Pos.y + vp->Size.y * 0.5f);
        ImGui::SetNextWindowPos(
            ImVec2(center.x - detached_width_ * 0.5f,
                   center.y - detached_height_ * 0.5f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(detached_width_, detached_height_), ImGuiCond_Always);

        // Undock from any dockspace.
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    }

    // Handle pending attach: dock back into the target dockspace.
    if (pending_attach_)
    {
        pending_attach_ = false;
        detached_       = false;

        if (dock_id_ != 0)
        {
            ImGui::SetNextWindowDockID(
                static_cast<ImGuiID>(dock_id_), ImGuiCond_Always);
        }
    }
#endif   // IMGUI_HAS_DOCK

    ImGuiWindowFlags flags = static_cast<ImGuiWindowFlags>(extra_window_flags());

    if (!ImGui::Begin(title_.c_str(), p_open, flags))
    {
        ImGui::End();
        return;
    }

#ifdef IMGUI_HAS_DOCK
    // Track docked state from ImGui's perspective.
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win)
    {
        bool currently_docked = (win->DockId != 0);
        detached_ = !currently_docked;
    }
#endif

    draw_context_menu();
    draw_content();

    ImGui::End();

#else
    (void)p_open;
#endif   // SPECTRA_USE_IMGUI
}

// ---------------------------------------------------------------------------
// detach / attach
// ---------------------------------------------------------------------------

void DetachablePanel::detach()
{
    if (!detached_)
        pending_detach_ = true;
}

void DetachablePanel::attach()
{
    if (detached_)
        pending_attach_ = true;
}

// ---------------------------------------------------------------------------
// draw_context_menu
// ---------------------------------------------------------------------------

void DetachablePanel::draw_context_menu()
{
#ifdef SPECTRA_USE_IMGUI
    // Right-click context menu on the window content area.
    if (ImGui::BeginPopupContextItem("##DetachMenu"))
    {
        if (detached_)
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
