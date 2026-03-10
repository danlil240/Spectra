#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "display_plugin.hpp"
#include "messages/pointcloud_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
    #include <rclcpp/rclcpp.hpp>
    #include <sensor_msgs/msg/point_cloud2.hpp>
#endif

namespace spectra::adapters::ros2
{

class PointCloudDisplay : public DisplayPlugin
{
   public:
    enum class ColorMode
    {
        Flat,
        Intensity,
        Height,
        RGB,
    };

    PointCloudDisplay();

    std::string type_id() const override { return "pointcloud"; }
    std::string display_name() const override { return "PointCloud2"; }
    std::string icon() const override { return "PC"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    void                     set_topic(const std::string& topic) override;
    std::vector<std::string> compatible_message_types() const override
    {
        return {"sensor_msgs/msg/PointCloud2"};
    }

    std::string serialize_config_blob() const override;
    void        deserialize_config_blob(const std::string& blob) override;

    void                           ingest_pointcloud_frame(const PointCloudFrame& frame);
    std::optional<PointCloudFrame> latest_frame() const;

   private:
    void               ensure_subscription();
    static const char* color_mode_name(ColorMode mode);

    const TfBuffer*                tf_buffer_{nullptr};
    TopicDiscovery*                topic_discovery_{nullptr};
    std::string                    fixed_frame_;
    bool                           resubscribe_requested_{false};
    std::string                    subscribed_topic_;
    bool                           use_message_stamp_{true};
    ColorMode                      color_mode_{ColorMode::Flat};
    float                          point_size_{3.0f};
    int                            max_points_{100'000};
    std::array<char, 256>          topic_input_{};
    mutable std::mutex             frame_mutex_;
    std::optional<PointCloudFrame> latest_frame_;

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr                                        node_;
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr subscription_;
#endif
};

}   // namespace spectra::adapters::ros2
