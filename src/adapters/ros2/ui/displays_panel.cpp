#include "ui/displays_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

void DisplaysPanel::draw(bool* p_open,
                         const DisplayRegistry& registry,
                         const DisplayContext& context,
                         std::vector<std::unique_ptr<DisplayPlugin>>& displays)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    if (!ImGui::Begin(title_.c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Add Display"))
        ImGui::OpenPopup("##add_display_popup");
    ImGui::SameLine();
    const bool can_remove = selected_index_ >= 0
                         && selected_index_ < static_cast<int>(displays.size());
    if (ImGui::Button("Remove Selected") && can_remove)
    {
        displays[static_cast<size_t>(selected_index_)]->on_destroy();
        displays.erase(displays.begin() + selected_index_);
        if (selected_index_ >= static_cast<int>(displays.size()))
            selected_index_ = static_cast<int>(displays.size()) - 1;
    }

    if (ImGui::BeginPopup("##add_display_popup"))
    {
        for (const auto& type : registry.list_types())
        {
            if (ImGui::Selectable(type.display_name.c_str()))
            {
                if (auto display = registry.create(type.type_id))
                {
                    if (display->enabled())
                        display->on_enable(context);
                    displays.push_back(std::move(display));
                    selected_index_ = static_cast<int>(displays.size()) - 1;
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    for (int index = 0; index < static_cast<int>(displays.size()); ++index)
    {
        auto& display = displays[static_cast<size_t>(index)];
        ImGui::PushID(index);

        bool enabled = display->enabled();
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            if (enabled)
                display->on_enable(context);
            else
                display->on_disable();
            display->set_enabled(enabled);
        }

        ImGui::SameLine();
        std::string label = display->display_name();
        if (!display->topic().empty())
            label += " [" + display->topic() + "]";
        if (ImGui::Selectable(label.c_str(), selected_index_ == index))
            selected_index_ = index;
        if (display->status() != DisplayStatus::Disabled)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", display->status_text().c_str());
        }

        ImGui::PopID();
    }

    const bool has_selection = selected_index_ >= 0
                            && selected_index_ < static_cast<int>(displays.size());
    if (has_selection)
    {
        ImGui::Separator();
        displays[static_cast<size_t>(selected_index_)]->draw_inspector_ui();
    }

    ImGui::End();
#else
    (void)p_open;
    (void)registry;
    (void)context;
    (void)displays;
#endif
}

}   // namespace spectra::adapters::ros2
