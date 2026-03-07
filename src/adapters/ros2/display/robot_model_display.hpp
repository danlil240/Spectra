#pragma once

#include <array>
#include <string>
#include <vector>

#include "display_plugin.hpp"
#include "urdf/urdf_parser.hpp"

namespace spectra::adapters::ros2
{

class RobotModelDisplay : public DisplayPlugin
{
public:
    RobotModelDisplay();

    std::string type_id() const override { return "robot_model"; }
    std::string display_name() const override { return "Robot Model"; }
    std::string icon() const override { return "RM"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    std::vector<std::string> compatible_message_types() const override
    {
        return {"sensor_msgs/msg/JointState"};
    }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;

    void set_robot_description_xml_for_test(const std::string& xml);
    size_t collision_count() const;
    const RobotDescription& robot_description() const { return robot_; }

private:
    void apply_robot_description_xml(const std::string& xml);

    std::string parameter_name_{"robot_description"};
    std::array<char, 256> parameter_input_{};
    bool show_collision_shapes_{true};
    RobotDescription robot_;
    std::string robot_description_xml_;
    std::string pending_robot_description_xml_;
    std::vector<std::string> warnings_;
    bool last_parse_failed_{false};

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node_;
#endif
};

}   // namespace spectra::adapters::ros2
