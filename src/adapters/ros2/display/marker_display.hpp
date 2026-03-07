#pragma once

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "display_plugin.hpp"
#include "messages/marker_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
#include <visualization_msgs/msg/marker.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#endif

namespace spectra::adapters::ros2
{

class MarkerDisplay : public DisplayPlugin
{
public:
    MarkerDisplay();

    std::string type_id() const override { return "marker"; }
    std::string display_name() const override { return "Marker"; }
    std::string icon() const override { return "MK"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    void set_topic(const std::string& topic) override;
    std::vector<std::string> compatible_message_types() const override
    {
        return {
            "visualization_msgs/msg/Marker",
            "visualization_msgs/msg/MarkerArray",
        };
    }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;

    void ingest_marker_data(const MarkerData& marker);
    size_t marker_count() const;

private:
    struct StoredMarker
    {
        MarkerData marker;
        uint64_t expires_steady_ns{0};
    };

    void ensure_subscription();
    void sweep_expired_markers();
    void handle_marker_batch(const std::vector<MarkerData>& markers);
    void handle_marker(const MarkerData& marker);
    std::string marker_key(const MarkerData& marker) const;

    const TfBuffer* tf_buffer_{nullptr};
    TopicDiscovery* topic_discovery_{nullptr};
    std::string fixed_frame_;
    bool resubscribe_requested_{false};
    std::string subscribed_topic_;
    std::string subscribed_type_;
    bool show_expired_count_{false};
    bool use_message_stamp_{true};
    std::array<char, 256> topic_input_{};
    mutable std::mutex markers_mutex_;
    std::unordered_map<std::string, StoredMarker> markers_;
    uint64_t expired_count_{0};

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node_;
    rclcpp::Subscription<visualization_msgs::msg::Marker>::SharedPtr marker_sub_;
    rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr marker_array_sub_;
#endif
};

}   // namespace spectra::adapters::ros2
