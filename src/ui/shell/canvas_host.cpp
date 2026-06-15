#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/canvas_host.hpp"

    #include "imgui.h"
    #include "ui/layout/layout_manager.hpp"

namespace spectra::ui::shell
{
CanvasHost::CanvasHost(spectra::LayoutManager* layout_manager) : layout_manager_(layout_manager) {}

void CanvasHost::set_layout_manager(spectra::LayoutManager* lm)
{
    layout_manager_ = lm;
}

spectra::LayoutManager* CanvasHost::layout_manager() const
{
    return layout_manager_;
}

void CanvasHost::draw()
{
    if (!layout_manager_)
        return;

    const ImVec2 content_min = ImGui::GetWindowContentRegionMin();
    const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
    const ImVec2 window_pos  = ImGui::GetWindowPos();
    Rect         rect{};
    rect.x = window_pos.x + content_min.x;
    rect.y = window_pos.y + content_min.y;
    rect.w = content_max.x - content_min.x;
    rect.h = content_max.y - content_min.y;
    if (rect.w <= 0.0f || rect.h <= 0.0f)
        rect = layout_manager_->canvas_rect();

    layout_manager_->set_canvas_override(rect);

    ImGui::BeginChild("##canvas_host",
                      ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImGui::EndChild();

    draw_empty_state();
    draw_overlays();
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
