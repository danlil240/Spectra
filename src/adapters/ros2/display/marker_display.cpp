#include "display/marker_display.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"
#include "topic_discovery.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

namespace
{
uint64_t steady_now_ns()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string color_string(const spectra::Color& color)
{
    char buffer[96];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%.3f, %.3f, %.3f, %.3f",
                  color.r,
                  color.g,
                  color.b,
                  color.a);
    return buffer;
}
}   // namespace

MarkerDisplay::MarkerDisplay()
{
    set_topic("/visualization_marker");
}

void MarkerDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_ = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_ = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_ = DisplayStatus::Warn;
    status_text_ = "Waiting for marker topic";
}

void MarkerDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    marker_sub_.reset();
    marker_array_sub_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    subscribed_type_.clear();
    status_ = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void MarkerDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(markers_mutex_);
    markers_.clear();
}

void MarkerDisplay::on_update(float)
{
    ensure_subscription();
    sweep_expired_markers();

    const size_t count = marker_count();
    if (status_ != DisplayStatus::Disabled)
    {
        if (count == 0)
        {
            if (subscribed_topic_.empty())
            {
                status_ = DisplayStatus::Warn;
                status_text_ = "Waiting for marker topic";
            }
            else
            {
                status_ = DisplayStatus::Warn;
                status_text_ = "Subscribed, no markers received";
            }
        }
        else
        {
            status_ = DisplayStatus::Ok;
            status_text_ = std::to_string(count) + " active markers";
        }
    }
}

void MarkerDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    std::vector<StoredMarker> markers;
    {
        std::lock_guard<std::mutex> lock(markers_mutex_);
        markers.reserve(markers_.size());
        for (const auto& [_, entry] : markers_)
            markers.push_back(entry);
    }

    for (const auto& entry : markers)
    {
        const MarkerData& marker = entry.marker;
        spectra::Transform transform = marker.pose;

        if (tf_buffer_ && !fixed_frame_.empty() && !marker.frame_id.empty()
            && marker.frame_id != fixed_frame_)
        {
            const uint64_t lookup_time_ns = use_message_stamp_ ? marker.stamp_ns : 0;
            const TransformResult tf_result =
                tf_buffer_->lookup_transform(fixed_frame_, marker.frame_id, lookup_time_ns);
            if (!tf_result.ok)
                continue;

            spectra::Transform frame_transform;
            frame_transform.translation = {tf_result.tx, tf_result.ty, tf_result.tz};
            frame_transform.rotation = {
                static_cast<float>(tf_result.qx),
                static_cast<float>(tf_result.qy),
                static_cast<float>(tf_result.qz),
                static_cast<float>(tf_result.qw),
            };
            transform = frame_transform.compose(marker.pose);
        }

        SceneEntity entity;
        entity.type         = "marker";
        entity.label        = marker.ns.empty()
            ? "Marker " + std::to_string(marker.id)
            : marker.ns + "/" + std::to_string(marker.id);
        entity.display_name = display_name();
        entity.topic        = marker.topic;
        entity.frame_id     = marker.frame_id;
        entity.transform    = transform;
        entity.scale        = marker.scale;
        entity.stamp_ns     = marker.stamp_ns;
        entity.properties.push_back({"primitive", marker_primitive_name(marker.primitive)});
        entity.properties.push_back({"namespace", marker.ns.empty() ? "(none)" : marker.ns});
        entity.properties.push_back({"id", std::to_string(marker.id)});
        entity.properties.push_back({"points", std::to_string(marker.points.size())});
        entity.properties.push_back({"frame_locked", marker.frame_locked ? "true" : "false"});
        entity.properties.push_back({"color", color_string(marker.color)});
        if (!marker.text.empty())
            entity.properties.push_back({"text", marker.text});
        scene.add_entity(std::move(entity));
    }
}

void MarkerDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::InputText("Topic", topic_input_.data(), topic_input_.size()))
        set_topic(std::string(topic_input_.data()));
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);
    ImGui::Checkbox("Show Expired Count", &show_expired_count_);
    ImGui::Text("Active markers: %zu", marker_count());
    if (show_expired_count_)
        ImGui::Text("Expired markers: %llu", static_cast<unsigned long long>(expired_count_));
    if (!subscribed_type_.empty())
        ImGui::TextWrapped("Subscription type: %s", subscribed_type_.c_str());
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void MarkerDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    std::snprintf(topic_input_.data(), topic_input_.size(), "%s", topic_.c_str());
    resubscribe_requested_ = true;
}

std::string MarkerDisplay::serialize_config_blob() const
{
    char buffer[384];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "topic=%s;use_message_stamp=%d;show_expired_count=%d",
                  topic_.c_str(),
                  use_message_stamp_ ? 1 : 0,
                  show_expired_count_ ? 1 : 0);
    return buffer;
}

void MarkerDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char topic[256] = {};
    int use_message_stamp = use_message_stamp_ ? 1 : 0;
    int show_expired_count = show_expired_count_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];use_message_stamp=%d;show_expired_count=%d",
                    topic,
                    &use_message_stamp,
                    &show_expired_count) >= 1)
    {
        set_topic(topic);
        use_message_stamp_ = use_message_stamp != 0;
        show_expired_count_ = show_expired_count != 0;
    }
}

void MarkerDisplay::ingest_marker_data(const MarkerData& marker)
{
    handle_marker(marker);
}

size_t MarkerDisplay::marker_count() const
{
    std::lock_guard<std::mutex> lock(markers_mutex_);
    return markers_.size();
}

void MarkerDisplay::ensure_subscription()
{
#ifdef SPECTRA_USE_ROS2
    if (!resubscribe_requested_)
        return;

    resubscribe_requested_ = false;
    marker_sub_.reset();
    marker_array_sub_.reset();
    subscribed_topic_.clear();
    subscribed_type_.clear();

    if (!node_ || !topic_discovery_ || topic_.empty())
        return;

    const TopicInfo info = topic_discovery_->topic(topic_);
    if (info.name.empty() || info.types.empty())
    {
        status_ = DisplayStatus::Warn;
        status_text_ = "Topic not discovered yet";
        return;
    }

    const std::string type = info.types.front();
    if (type == "visualization_msgs/msg/Marker")
    {
        marker_sub_ = node_->create_subscription<visualization_msgs::msg::Marker>(
            topic_,
            rclcpp::QoS(rclcpp::KeepLast(100)),
            [this](const visualization_msgs::msg::Marker::SharedPtr msg)
            {
                auto marker = adapt_marker_message(*msg, topic_);
                if (marker.has_value())
                    handle_marker(*marker);
            });
    }
    else if (type == "visualization_msgs/msg/MarkerArray")
    {
        marker_array_sub_ = node_->create_subscription<visualization_msgs::msg::MarkerArray>(
            topic_,
            rclcpp::QoS(rclcpp::KeepLast(100)),
            [this](const visualization_msgs::msg::MarkerArray::SharedPtr msg)
            {
                handle_marker_batch(adapt_marker_array_message(*msg, topic_));
            });
    }
    else
    {
        status_ = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + type;
        return;
    }

    subscribed_topic_ = topic_;
    subscribed_type_ = type;
    status_ = DisplayStatus::Ok;
    status_text_ = "Subscribed to " + topic_;
#endif
}

void MarkerDisplay::sweep_expired_markers()
{
    const uint64_t now_ns = steady_now_ns();
    std::lock_guard<std::mutex> lock(markers_mutex_);
    for (auto it = markers_.begin(); it != markers_.end();)
    {
        if (it->second.expires_steady_ns != 0 && it->second.expires_steady_ns <= now_ns)
        {
            ++expired_count_;
            it = markers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void MarkerDisplay::handle_marker_batch(const std::vector<MarkerData>& markers)
{
    for (const auto& marker : markers)
        handle_marker(marker);
}

void MarkerDisplay::handle_marker(const MarkerData& marker)
{
#ifdef SPECTRA_USE_ROS2
    if (marker.action == visualization_msgs::msg::Marker::DELETEALL)
    {
        std::lock_guard<std::mutex> lock(markers_mutex_);
        markers_.clear();
        return;
    }
    if (marker.action == visualization_msgs::msg::Marker::DELETE)
    {
        std::lock_guard<std::mutex> lock(markers_mutex_);
        markers_.erase(marker_key(marker));
        return;
    }
#endif

    if (marker.primitive == MarkerPrimitive::Unknown)
        return;

    StoredMarker stored;
    stored.marker = marker;
    stored.expires_steady_ns = marker.lifetime_ns > 0 ? steady_now_ns() + marker.lifetime_ns : 0;

    std::lock_guard<std::mutex> lock(markers_mutex_);
    markers_[marker_key(marker)] = std::move(stored);
}

std::string MarkerDisplay::marker_key(const MarkerData& marker) const
{
    return marker.ns + "#" + std::to_string(marker.id);
}

}   // namespace spectra::adapters::ros2
