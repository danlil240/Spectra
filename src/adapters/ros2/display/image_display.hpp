#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "display_plugin.hpp"
#include "messages/image_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#endif

namespace spectra::adapters::ros2
{

class ImageDisplay : public DisplayPlugin
{
public:
    enum class Mode
    {
        Panel2D,
        Billboard3D,
        PanelAndBillboard,
    };

    ImageDisplay();

    std::string type_id() const override { return "image"; }
    std::string display_name() const override { return "Image"; }
    std::string icon() const override { return "IM"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;
    void draw_auxiliary_ui() override;

    void set_topic(const std::string& topic) override;
    std::vector<std::string> compatible_message_types() const override
    {
        return {"sensor_msgs/msg/Image"};
    }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;

    void ingest_image_frame(const ImageFrame& frame);
    std::optional<ImageFrame> latest_frame() const;

private:
    void ensure_subscription();
    static const char* mode_name(Mode mode);

    const TfBuffer* tf_buffer_{nullptr};
    TopicDiscovery* topic_discovery_{nullptr};
    std::string fixed_frame_;
    bool resubscribe_requested_{false};
    std::string subscribed_topic_;
    bool use_message_stamp_{true};
    Mode mode_{Mode::Panel2D};
    bool panel_visible_{true};
    uint32_t preview_max_dim_{48};
    std::array<char, 256> topic_input_{};
    mutable std::mutex frame_mutex_;
    std::optional<ImageFrame> latest_frame_;

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node_;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
#endif
};

}   // namespace spectra::adapters::ros2
