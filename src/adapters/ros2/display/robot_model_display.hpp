#pragma once

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "display_plugin.hpp"
#include "messages/joint_state_adapter.hpp"
#include "urdf/urdf_parser.hpp"

#ifdef SPECTRA_USE_ROS2
#include <sensor_msgs/msg/joint_state.hpp>
#endif

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
    void set_joint_positions_for_test(const std::unordered_map<std::string, double>& positions);
    size_t collision_count() const;
    const RobotDescription& robot_description() const { return robot_; }

    /// Get the FK-computed world transform for a link.
    spectra::Transform link_transform(const std::string& link_name) const;

private:
    void apply_robot_description_xml(const std::string& xml);
    void build_kinematic_chain();
    void compute_fk();
    spectra::Transform joint_transform(const UrdfJoint& joint, double position) const;

    std::string parameter_name_{"robot_description"};
    std::string joint_state_topic_{"/joint_states"};
    std::array<char, 256> parameter_input_{};
    std::array<char, 256> joint_topic_input_{};
    bool show_collision_shapes_{true};
    bool show_frame_axes_{false};
    bool show_joint_axes_{false};
    RobotDescription robot_;
    std::string robot_description_xml_;
    std::string pending_robot_description_xml_;
    std::vector<std::string> warnings_;
    bool last_parse_failed_{false};

    // FK state
    std::unordered_map<std::string, double> joint_positions_;
    mutable std::mutex joint_mutex_;
    std::unordered_map<std::string, spectra::Transform> link_transforms_;
    std::string root_link_;
    bool fk_dirty_{true};

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node_;
    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_sub_;
#endif
};

}   // namespace spectra::adapters::ros2
