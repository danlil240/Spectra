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
    char buffer[256];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "cell_size=%.3f;cell_count=%d;plane=%s;color=%.3f,%.3f,%.3f;alpha=%.3f;offset=%."
                  "3f,%.3f,%.3f",
                  cell_size_,
                  cell_count_,
                  plane_.c_str(),
                  color_[0],
                  color_[1],
                  color_[2],
                  alpha_,
                  offset_[0],
                  offset_[1],
                  offset_[2]);
    return buffer;
}

void GridDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    float size      = cell_size_;
    int   count     = cell_count_;
    char  plane[16] = {};
    float r = color_[0], g = color_[1], b = color_[2];
    float alpha = alpha_;
    float ox = offset_[0], oy = offset_[1], oz = offset_[2];
    if (std::sscanf(
            blob.c_str(),
            "cell_size=%f;cell_count=%d;plane=%15[^;];color=%f,%f,%f;alpha=%f;offset=%f,%f,%f",
            &size,
            &count,
            plane,
            &r,
            &g,
            &b,
            &alpha,
            &ox,
            &oy,
            &oz)
        >= 2)
    {
        cell_size_  = size > 0.0f ? size : cell_size_;
        cell_count_ = count > 0 ? count : cell_count_;
        if (plane[0] != '\0')
            plane_ = plane;
        color_[0]  = r;
        color_[1]  = g;
        color_[2]  = b;
        alpha_     = alpha;
        offset_[0] = ox;
        offset_[1] = oy;
        offset_[2] = oz;
    }
}

void GridDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;
    SceneEntity entity;
    entity.type                  = type_id();
    entity.label                 = display_name() + " (" + plane_ + ")";
    entity.display_name          = display_name();
    entity.transform.translation = {offset_[0], offset_[1], offset_[2]};
    entity.scale                 = {cell_size_, 1.0, static_cast<double>(cell_count_)};
    char color_buffer[96];
    std::snprintf(color_buffer,
                  sizeof(color_buffer),
                  "%.3f, %.3f, %.3f, %.3f",
                  color_[0],
                  color_[1],
                  color_[2],
                  alpha_);
    entity.properties.push_back({"plane", plane_});
    entity.properties.push_back({"cell_size", std::to_string(cell_size_)});
    entity.properties.push_back({"cell_count", std::to_string(cell_count_)});
    entity.properties.push_back({"color", color_buffer});
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
    int         current  = plane_ == "xz" ? 1 : (plane_ == "yz" ? 2 : 0);
    if (ImGui::Combo("Plane", &current, planes, 3))
        plane_ = planes[current];
    ImGui::ColorEdit3("Color", color_);
    ImGui::SliderFloat("Alpha", &alpha_, 0.0f, 1.0f, "%.2f");
    ImGui::DragFloat3("Offset", offset_, 0.1f, -100.0f, 100.0f, "%.2f");
#endif
}

}   // namespace spectra::adapters::ros2
