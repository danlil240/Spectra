#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/shell_style.hpp"

    #include "imgui.h"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
bool begin_panel(const char* title, bool* p_open, ImGuiWindowFlags extra_flags)
{
    const auto& colors = ui::theme();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(colors.bg_secondary.r,
                                 colors.bg_secondary.g,
                                 colors.bg_secondary.b,
                                 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(colors.border_subtle.r,
                                 colors.border_subtle.g,
                                 colors.border_subtle.b,
                                 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | extra_flags;
    return ImGui::Begin(title, p_open, flags);
}

void end_panel()
{
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
