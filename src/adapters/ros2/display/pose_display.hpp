#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "display_plugin.hpp"
#include "messages/path_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#endif

namespace spectra::adapters::ros2
{

class PoseDisplay : public DisplayPlugin
{
public:
    PoseDisplay();

    std::string type_id() const override { return "pose"; }
    std::string display_name() const override { return "Pose"; }
    std::string icon() const override { return "PS"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    void set_topic(const std::string& topic) override;
    std::vector<std::string> compatible_message_types() const override
    {
        return {"geometry_msgs/msg/PoseStamped"};
    }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;

    void ingest_pose_frame(const PoseFrame& frame);
    std::optional<PoseFrame> latest_frame() const;

private:
    void ensure_subscription();

    const TfBuffer* tf_buffer_{nullptr};
    TopicDiscovery* topic_discovery_{nullptr};
    std::string fixed_frame_;
    bool resubscribe_requested_{false};
    std::string subscribed_topic_;
    bool use_message_stamp_{true};
    float shaft_length_{0.8f};
    float shaft_width_{0.08f};
    float head_length_{0.25f};
    float head_width_{0.16f};
    std::array<char, 256> topic_input_{};
    mutable std::mutex frame_mutex_;
    std::optional<PoseFrame> latest_frame_;

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node_;
    rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr subscription_;
#endif
};

}   // namespace spectra::adapters::ros2
