#pragma once

#include <array>
#include <deque>
#include <mutex>
#include <string>

#include "display_plugin.hpp"
#include "messages/laserscan_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#endif

namespace spectra::adapters::ros2
{

class LaserScanDisplay : public DisplayPlugin
{
public:
    enum class RenderStyle
    {
        Points,
        Lines,
    };

    enum class ColorMode
    {
        Flat,
        Range,
        Intensity,
    };

    LaserScanDisplay();

    std::string type_id() const override { return "laserscan"; }
    std::string display_name() const override { return "LaserScan"; }
    std::string icon() const override { return "LS"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    void set_topic(const std::string& topic) override;
    std::vector<std::string> compatible_message_types() const override
    {
        return {"sensor_msgs/msg/LaserScan"};
    }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;

    void ingest_scan_frame(const LaserScanFrame& frame);
    size_t scan_count() const;

private:
    void ensure_subscription();
    static const char* render_style_name(RenderStyle style);
    static const char* color_mode_name(ColorMode mode);

    const TfBuffer* tf_buffer_{nullptr};
    TopicDiscovery* topic_discovery_{nullptr};
    std::string fixed_frame_;
    bool resubscribe_requested_{false};
    std::string subscribed_topic_;
    bool use_message_stamp_{true};
    RenderStyle render_style_{RenderStyle::Points};
    ColorMode color_mode_{ColorMode::Flat};
    int trail_size_{1};
    float min_range_filter_{0.0f};
    float max_range_filter_{100.0f};
    std::array<char, 256> topic_input_{};
    mutable std::mutex scans_mutex_;
    std::deque<LaserScanFrame> scans_;

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node_;
    rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr subscription_;
#endif
};

}   // namespace spectra::adapters::ros2
