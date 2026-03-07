#include "display/grid_display.hpp"

#include <cstdio>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

#include "scene/scene_manager.hpp"

namespace spectra::adapters::ros2
{

std::string GridDisplay::serialize_config_blob() const
{
    char buffer[128];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "cell_size=%.3f;cell_count=%d;plane=%s",
                  cell_size_,
                  cell_count_,
                  plane_.c_str());
    return buffer;
}

void GridDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    float size = cell_size_;
    int count = cell_count_;
    char plane[16] = {};
    if (std::sscanf(blob.c_str(),
                    "cell_size=%f;cell_count=%d;plane=%15s",
                    &size,
                    &count,
                    plane) >= 2)
    {
        cell_size_  = size > 0.0f ? size : cell_size_;
        cell_count_ = count > 0 ? count : cell_count_;
        if (plane[0] != '\0')
            plane_ = plane;
    }
}

void GridDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;
    SceneEntity entity;
    entity.type = type_id();
    entity.label = display_name() + " (" + plane_ + ")";
    entity.display_name = display_name();
    entity.scale = {cell_size_, 1.0, static_cast<double>(cell_count_)};
    entity.properties.push_back({"plane", plane_});
    entity.properties.push_back({"cell_size", std::to_string(cell_size_)});
    entity.properties.push_back({"cell_count", std::to_string(cell_count_)});
    scene.add_entity(std::move(entity));
}

void GridDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    ImGui::TextUnformatted("Grid");
    ImGui::SliderFloat("Cell Size", &cell_size_, 0.1f, 10.0f, "%.2f");
    ImGui::SliderInt("Cell Count", &cell_count_, 1, 200);
    const char* planes[] = {"xy", "xz", "yz"};
    int current = plane_ == "xy" ? 0 : (plane_ == "yz" ? 2 : 1);
    if (ImGui::Combo("Plane", &current, planes, 3))
        plane_ = planes[current];
#endif
}

}   // namespace spectra::adapters::ros2
