#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/status_bar.hpp"

    #include "imgui.h"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
void StatusBar::set_layout_manager(spectra::LayoutManager* lm)
{
    layout_manager_ = lm;
}

spectra::LayoutManager* StatusBar::layout_manager() const
{
    return layout_manager_;
}

void StatusBar::add_segment(StatusSegment segment)
{
    segments_.push_back(std::move(segment));
}

void StatusBar::clear()
{
    segments_.clear();
}

void StatusBar::draw()
{
    if (!layout_manager_)
        return;

    const Rect bounds = layout_manager_->status_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::STATUS_BAR_PADDING_H, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##AppShellStatusBar", nullptr, flags))
    {
        const auto& colors = ui::theme();
        ImDrawList* bar_dl = ImGui::GetWindowDrawList();
        ImVec2      wpos   = ImGui::GetWindowPos();
        ImVec2      wsz    = ImGui::GetWindowSize();
        ImVec2      wmax(wpos.x + wsz.x, wpos.y + wsz.y);

        bar_dl->AddRectFilled(wpos,
                              wmax,
                              IM_COL32(static_cast<uint8_t>(colors.bg_primary.r * 255),
                                       static_cast<uint8_t>(colors.bg_primary.g * 255),
                                       static_cast<uint8_t>(colors.bg_primary.b * 255),
                                       255));

        bar_dl->AddLine(wpos,
                        ImVec2(wmax.x, wpos.y),
                        IM_COL32(static_cast<uint8_t>(colors.border_subtle.r * 255),
                                 static_cast<uint8_t>(colors.border_subtle.g * 255),
                                 static_cast<uint8_t>(colors.border_subtle.b * 255),
                                 70),
                        1.0f);

        const float bar_h  = bounds.h;
        const float text_h = ImGui::GetTextLineHeight();
        const float y      = (bar_h - text_h) * 0.5f;
        ImGui::SetCursorPosY(y);

        const float content_w = ImGui::GetContentRegionAvail().x;
        ImGui::Columns(3, "##status_bar_cols", false);
        ImGui::SetColumnWidth(0, content_w * 0.33f);
        ImGui::SetColumnWidth(1, content_w * 0.34f);
        ImGui::SetColumnWidth(2, content_w * 0.33f);

        for (const StatusSegment& segment : segments_)
        {
            if (segment.align != StatusAlign::Left || !segment.draw_fn)
                continue;
            segment.draw_fn();
            ImGui::SameLine(0.0f, ui::tokens::STATUS_BAR_GROUP_GAP);
        }

        ImGui::NextColumn();

        for (const StatusSegment& segment : segments_)
        {
            if (segment.align != StatusAlign::Center || !segment.draw_fn)
                continue;
            const float col_w  = ImGui::GetColumnWidth();
            const float prev_x = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(prev_x + col_w * 0.5f);
            segment.draw_fn();
            ImGui::NewLine();
        }

        ImGui::NextColumn();

        std::vector<const StatusSegment*> right_segments;
        for (const StatusSegment& segment : segments_)
        {
            if (segment.align == StatusAlign::Right && segment.draw_fn)
                right_segments.push_back(&segment);
        }

        if (!right_segments.empty())
        {
            float used_w = 0.0f;
            for (auto it = right_segments.rbegin(); it != right_segments.rend(); ++it)
            {
                used_w += ui::tokens::STATUS_BAR_GROUP_GAP;
                ImGui::SetCursorPosX(ImGui::GetColumnOffset(2) + ImGui::GetColumnWidth(2) - used_w);
                (*it)->draw_fn();
                used_w += ImGui::GetItemRectSize().x;
            }
        }

        ImGui::Columns(1);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
