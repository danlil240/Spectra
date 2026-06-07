#include "ui/inspector_panel.hpp"

#include "scene/scene_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

void InspectorPanel::draw(bool*              p_open,
                          SceneManager&      scene,
                          const std::string& fixed_frame,
                          size_t             display_count)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    if (!ImGui::Begin(title_.c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Scene entities: %zu", scene.entity_count());
    if (const SceneEntity* entity = scene.selected_entity())
    {
        if (ImGui::Button("Clear Selection"))
            scene.clear_selection();

        ImGui::Separator();
        ImGui::Text("Selected: %s",
                    entity->label.empty() ? entity->type.c_str() : entity->label.c_str());
        ImGui::Text("Type: %s", entity->type.c_str());
        if (!entity->display_name.empty())
            ImGui::Text("Display: %s", entity->display_name.c_str());
        if (!entity->topic.empty())
            ImGui::Text("Topic: %s", entity->topic.c_str());
        if (!entity->frame_id.empty())
            ImGui::Text("Frame: %s", entity->frame_id.c_str());
        ImGui::Text("Translation: (%.3f, %.3f, %.3f)",
                    entity->transform.translation.x,
                    entity->transform.translation.y,
                    entity->transform.translation.z);
        ImGui::Text("Scale: (%.3f, %.3f, %.3f)", entity->scale.x, entity->scale.y, entity->scale.z);
        if (entity->polyline.has_value())
            ImGui::Text("Polyline points: %zu", entity->polyline->points.size());
        if (entity->arrow.has_value())
            ImGui::Text("Arrow length: %.2f", entity->arrow->shaft_length);
        if (entity->billboard.has_value())
            ImGui::Text("Billboard: %.2f x %.2f",
                        entity->billboard->width,
                        entity->billboard->height);
        if (entity->stamp_ns != 0)
            ImGui::Text("Stamp: %llu ns", static_cast<unsigned long long>(entity->stamp_ns));
        if (!entity->properties.empty())
        {
            ImGui::Separator();
            for (const auto& property : entity->properties)
                ImGui::Text("%s: %s", property.key.c_str(), property.value.c_str());
        }
    }
    else
    {
        ImGui::Separator();
        ImGui::TextDisabled("Select an entity in the scene viewport to inspect it.");
        ImGui::Text("Fixed frame: %s", fixed_frame.empty() ? "(auto)" : fixed_frame.c_str());
        ImGui::Text("Entities: %zu", scene.entity_count());
        ImGui::Text("Displays enabled: %zu", display_count);
    }

    ImGui::End();
#else
    (void)p_open;
    (void)scene;
    (void)fixed_frame;
    (void)display_count;
#endif
}

}   // namespace spectra::adapters::ros2
