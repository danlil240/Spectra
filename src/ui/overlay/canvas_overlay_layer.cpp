#include "canvas_overlay_layer.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <imgui.h>
    #include <imgui_internal.h>

namespace spectra::ui
{

namespace
{

constexpr const char* k_layer_window_name = "##canvas_overlay_layer";

// A window the layer must stay below: anything ImGui brings to the front
// (floating panels, menus, popups, modals, tooltips).
bool is_front_window(const ImGuiWindow* w, const ImGuiWindow* layer)
{
    if (w == layer || w->IsFallbackWindow)
        return false;
    if ((w->Flags & ImGuiWindowFlags_ChildWindow) && !(w->Flags & ImGuiWindowFlags_Popup))
        return false;   // Child windows are drawn with their root window
    if (!w->Active && !w->WasActive)
        return false;
    return (w->Flags & ImGuiWindowFlags_NoBringToFrontOnFocus) == 0;
}

void place_layer(ImGuiWindow* layer)
{
    ImGuiContext& g = *GImGui;
    for (ImGuiWindow* w : g.Windows)
    {
        if (is_front_window(w, layer))
        {
            ImGui::BringWindowToDisplayBehind(layer, w);
            return;
        }
    }
    ImGui::BringWindowToDisplayFront(layer);
}

}   // namespace

ImDrawList* canvas_overlay_draw_list()
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
    ImGui::Begin(k_layer_window_name, nullptr, flags);
    ImGuiWindow* layer = ImGui::GetCurrentWindow();
    ImGui::End();
    ImGui::PopStyleVar(2);

    place_layer(layer);
    return layer->DrawList;
}

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI
