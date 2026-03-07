#pragma once

#include <memory>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_ROS2
#include <rclcpp/rclcpp.hpp>
#endif

namespace spectra::adapters::ros2
{

class SceneManager;
class TfBuffer;
class TopicDiscovery;

enum class DisplayStatus
{
    Disabled,
    Ok,
    Warn,
    Error,
};

struct DisplayContext
{
    std::string fixed_frame;
    TfBuffer* tf_buffer{nullptr};
    TopicDiscovery* topic_discovery{nullptr};
#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr node;
#endif
};

class DisplayPlugin
{
public:
    virtual ~DisplayPlugin() = default;

    virtual std::string type_id() const = 0;
    virtual std::string display_name() const = 0;
    virtual std::string icon() const = 0;

    virtual void on_enable(const DisplayContext&) {}
    virtual void on_disable() {}
    virtual void on_destroy() {}
    virtual void on_update(float) {}
    virtual void submit_renderables(SceneManager&) {}
    virtual void draw_inspector_ui() = 0;
    virtual void draw_auxiliary_ui() {}

    virtual void set_topic(const std::string& topic) { topic_ = topic; }
    virtual std::string topic() const { return topic_; }
    virtual std::vector<std::string> compatible_message_types() const = 0;

    virtual std::string serialize_config_blob() const = 0;
    virtual void deserialize_config_blob(const std::string& blob) = 0;

    bool enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    DisplayStatus status() const { return status_; }
    const std::string& status_text() const { return status_text_; }

protected:
    bool enabled_{true};
    DisplayStatus status_{DisplayStatus::Disabled};
    std::string status_text_{"Disabled"};
    std::string topic_;
};

}   // namespace spectra::adapters::ros2
