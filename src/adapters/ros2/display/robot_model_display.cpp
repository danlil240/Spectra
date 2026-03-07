#include "display/robot_model_display.hpp"

#include <cstdio>

#include "scene/scene_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

namespace
{
std::string geometry_name(UrdfGeometryType type)
{
    switch (type)
    {
        case UrdfGeometryType::Box: return "box";
        case UrdfGeometryType::Cylinder: return "cylinder";
        case UrdfGeometryType::Sphere: return "sphere";
        case UrdfGeometryType::Unsupported: return "unsupported";
    }
    return "unsupported";
}
}   // namespace

RobotModelDisplay::RobotModelDisplay()
{
    std::snprintf(parameter_input_.data(), parameter_input_.size(), "%s", parameter_name_.c_str());
    status_ = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void RobotModelDisplay::on_enable(const DisplayContext& context)
{
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    status_ = DisplayStatus::Warn;
    status_text_ = "Waiting for robot description";
}

void RobotModelDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    node_.reset();
#endif
    status_ = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void RobotModelDisplay::on_destroy()
{
    on_disable();
    robot_ = {};
    robot_description_xml_.clear();
    pending_robot_description_xml_.clear();
    warnings_.clear();
    last_parse_failed_ = false;
}

void RobotModelDisplay::on_update(float)
{
    if (!pending_robot_description_xml_.empty()
        && pending_robot_description_xml_ != robot_description_xml_)
    {
        apply_robot_description_xml(pending_robot_description_xml_);
    }

#ifdef SPECTRA_USE_ROS2
    if (node_ && !parameter_name_.empty())
    {
        std::string xml;
        if (node_->get_parameter(parameter_name_, xml)
            && !xml.empty()
            && xml != robot_description_xml_)
        {
            apply_robot_description_xml(xml);
        }
    }
#endif

    if (status_ == DisplayStatus::Disabled)
        return;

    if (robot_.links.empty())
    {
        if (!last_parse_failed_)
        {
            status_ = DisplayStatus::Warn;
            status_text_ = "Waiting for robot description";
        }
        return;
    }

    status_ = warnings_.empty() ? DisplayStatus::Ok : DisplayStatus::Warn;
    status_text_ = std::to_string(robot_.links.size()) + " links, "
                 + std::to_string(robot_.joints.size()) + " joints, "
                 + std::to_string(collision_count()) + " collision shapes";
}

void RobotModelDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_ || !show_collision_shapes_)
        return;

    for (const auto& link : robot_.links)
    {
        for (const auto& collision : link.collisions)
        {
            SceneEntity entity;
            entity.type = "robot_" + geometry_name(collision.geometry.type);
            entity.label = collision.name.empty() ? link.name : link.name + "/" + collision.name;
            entity.display_name = display_name();
            entity.transform = collision.origin;
            entity.frame_id = link.name;
            entity.properties.push_back({"link", link.name});
            entity.properties.push_back({"geometry", geometry_name(collision.geometry.type)});
            entity.properties.push_back({"parameter", parameter_name_});

            switch (collision.geometry.type)
            {
                case UrdfGeometryType::Box:
                    entity.scale = collision.geometry.box_size;
                    entity.properties.push_back({
                        "size",
                        std::to_string(collision.geometry.box_size.x) + ", "
                        + std::to_string(collision.geometry.box_size.y) + ", "
                        + std::to_string(collision.geometry.box_size.z),
                    });
                    break;
                case UrdfGeometryType::Cylinder:
                    entity.scale = {
                        collision.geometry.radius * 2.0,
                        collision.geometry.radius * 2.0,
                        collision.geometry.length,
                    };
                    entity.properties.push_back({"radius", std::to_string(collision.geometry.radius)});
                    entity.properties.push_back({"length", std::to_string(collision.geometry.length)});
                    break;
                case UrdfGeometryType::Sphere:
                    entity.scale = {
                        collision.geometry.radius * 2.0,
                        collision.geometry.radius * 2.0,
                        collision.geometry.radius * 2.0,
                    };
                    entity.properties.push_back({"radius", std::to_string(collision.geometry.radius)});
                    break;
                case UrdfGeometryType::Unsupported:
                    continue;
            }

            scene.add_entity(std::move(entity));
        }
    }
}

void RobotModelDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::InputText("Robot Param", parameter_input_.data(), parameter_input_.size()))
        parameter_name_ = parameter_input_.data();
    ImGui::Checkbox("Show Collision Shapes", &show_collision_shapes_);
    ImGui::Text("Links: %zu", robot_.links.size());
    ImGui::Text("Joints: %zu", robot_.joints.size());
    ImGui::Text("Collision Shapes: %zu", collision_count());
    if (!warnings_.empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("Warnings:");
        for (const auto& warning : warnings_)
            ImGui::TextWrapped("- %s", warning.c_str());
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

std::string RobotModelDisplay::serialize_config_blob() const
{
    char buffer[384];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "parameter=%s;show_collision_shapes=%d",
                  parameter_name_.c_str(),
                  show_collision_shapes_ ? 1 : 0);
    return buffer;
}

void RobotModelDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char parameter_name[256] = {};
    int show_collision_shapes = show_collision_shapes_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "parameter=%255[^;];show_collision_shapes=%d",
                    parameter_name,
                    &show_collision_shapes) >= 1)
    {
        parameter_name_ = parameter_name;
        std::snprintf(parameter_input_.data(),
                      parameter_input_.size(),
                      "%s",
                      parameter_name_.c_str());
        show_collision_shapes_ = show_collision_shapes != 0;
    }
}

void RobotModelDisplay::set_robot_description_xml_for_test(const std::string& xml)
{
    pending_robot_description_xml_ = xml;
}

size_t RobotModelDisplay::collision_count() const
{
    size_t total = 0;
    for (const auto& link : robot_.links)
        total += link.collisions.size();
    return total;
}

void RobotModelDisplay::apply_robot_description_xml(const std::string& xml)
{
    const UrdfParseResult result = UrdfParser::parse_string(xml);
    if (!result.ok)
    {
        robot_ = {};
        warnings_.clear();
        last_parse_failed_ = true;
        status_ = DisplayStatus::Error;
        status_text_ = result.error;
        return;
    }

    robot_ = result.robot;
    warnings_ = result.warnings;
    robot_description_xml_ = xml;
    pending_robot_description_xml_.clear();
    last_parse_failed_ = false;
}

}   // namespace spectra::adapters::ros2
