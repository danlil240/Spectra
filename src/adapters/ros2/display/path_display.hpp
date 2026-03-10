#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "display_plugin.hpp"
#include "messages/path_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
    #include <nav_msgs/msg/path.hpp>
    #include <rclcpp/rclcpp.hpp>
#endif

namespace spectra::adapters::ros2
{

class PathDisplay : public DisplayPlugin
{
   public:
    PathDisplay();

    std::string type_id() const override { return "path"; }
    std::string display_name() const override { return "Path"; }
    std::string icon() const override { return "PA"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    void                     set_topic(const std::string& topic) override;
    std::vector<std::string> compatible_message_types() const override
    {
        return {"nav_msgs/msg/Path"};
    }

    std::string serialize_config_blob() const override;
    void        deserialize_config_blob(const std::string& blob) override;

    void                     ingest_path_frame(const PathFrame& frame);
    std::optional<PathFrame> latest_frame() const;

   private:
    void ensure_subscription();

    const TfBuffer*          tf_buffer_{nullptr};
    TopicDiscovery*          topic_discovery_{nullptr};
    std::string              fixed_frame_;
    bool                     resubscribe_requested_{false};
    std::string              subscribed_topic_;
    bool                     use_message_stamp_{true};
    float                    line_width_{2.0f};
    float                    alpha_{1.0f};
    bool                     show_pose_arrows_{false};
    std::array<char, 256>    topic_input_{};
    mutable std::mutex       frame_mutex_;
    std::optional<PathFrame> latest_frame_;

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr                              node_;
    rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr subscription_;
#endif
};

}   // namespace spectra::adapters::ros2
