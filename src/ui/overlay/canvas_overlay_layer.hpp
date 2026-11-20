#pragma once

#ifdef SPECTRA_USE_IMGUI

struct ImDrawList;

namespace spectra::ui
{

// Draw list for canvas-level overlays: split-pane splitters and the active
// pane outline, dock drop zones, rubber bands, measure lines and similar.
//
// Draws into a transparent, input-less, full-display window that is kept just
// above the canvas-level windows (the ones created with
// ImGuiWindowFlags_NoBringToFrontOnFocus, such as pane headers) and below
// every other window: floating panels, menus, popups, modals and tooltips.
// ImGui::GetForegroundDrawList() would draw over all of those.
//
// Must be called between ImGui::NewFrame() and ImGui::Render(). Calling it
// several times in a frame returns the same draw list.
ImDrawList* canvas_overlay_draw_list();

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI
