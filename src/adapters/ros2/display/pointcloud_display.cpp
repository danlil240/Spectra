#include "display/pointcloud_display.hpp"

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
spectra::Transform resolve_frame_transform(const TfBuffer*     tf_buffer,
                                           const std::string&  fixed_frame,
                                           const std::string&  frame_id,
                                           uint64_t            stamp_ns,
                                           bool                use_message_stamp,
                                           bool&               ok_out)
{
    ok_out = true;
    spectra::Transform transform{};
    if (!tf_buffer || fixed_frame.empty() || frame_id.empty() || frame_id == fixed_frame)
        return transform;

    const TransformResult result =
        tf_buffer->lookup_transform(fixed_frame, frame_id, use_message_stamp ? stamp_ns : 0);
    if (!result.ok)
    {
        ok_out = false;
        return transform;
    }

    transform.translation = {result.tx, result.ty, result.tz};
    transform.rotation = {
        static_cast<float>(result.qx),
        static_cast<float>(result.qy),
        static_cast<float>(result.qz),
        static_cast<float>(result.qw),
    };
    return transform;
}
}   // namespace

PointCloudDisplay::PointCloudDisplay()
{
    set_topic("/points");
}

void PointCloudDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_ = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_ = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_ = DisplayStatus::Warn;
    status_text_ = "Waiting for point cloud topic";
}

void PointCloudDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_ = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void PointCloudDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

void PointCloudDisplay::on_update(float)
{
    ensure_subscription();

    const auto frame = latest_frame();
    if (status_ == DisplayStatus::Disabled)
        return;

    if (!frame.has_value())
    {
        status_ = subscribed_topic_.empty() ? DisplayStatus::Warn : DisplayStatus::Warn;
        status_text_ = subscribed_topic_.empty()
            ? "Waiting for point cloud topic"
            : "Subscribed, no point cloud received";
        return;
    }

    status_ = DisplayStatus::Ok;
    status_text_ = std::to_string(frame->point_count) + " pts";
    if (frame->original_point_count > frame->point_count)
        status_text_ += " (decimated)";
}

void PointCloudDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    const auto frame = latest_frame();
    if (!frame.has_value())
        return;

    bool tf_ok = true;
    const spectra::Transform frame_transform = resolve_frame_transform(
        tf_buffer_,
        fixed_frame_,
        frame->frame_id,
        frame->stamp_ns,
        use_message_stamp_,
        tf_ok);
    if (!tf_ok)
        return;

    SceneEntity entity;
    entity.type = "pointcloud";
    entity.label = topic_.empty() ? display_name() : topic_;
    entity.display_name = display_name();
    entity.topic = frame->topic;
    entity.frame_id = frame->frame_id;
    entity.transform = frame_transform;
    entity.transform.translation = frame_transform.transform_point(frame->centroid);
    entity.scale = frame->max_bounds - frame->min_bounds;
    entity.stamp_ns = frame->stamp_ns;
    entity.properties.push_back({"points", std::to_string(frame->point_count)});
    entity.properties.push_back({"input_points", std::to_string(frame->original_point_count)});
    entity.properties.push_back({"color_mode", color_mode_name(color_mode_)});
    entity.properties.push_back({"point_size", std::to_string(point_size_)});
    entity.properties.push_back({"has_rgb", frame->has_rgb ? "true" : "false"});
    entity.properties.push_back({"has_intensity", frame->has_intensity ? "true" : "false"});
    entity.properties.push_back({"fixed_frame", fixed_frame_.empty() ? "(unset)" : fixed_frame_});
    scene.add_entity(std::move(entity));
}

void PointCloudDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::InputText("Topic", topic_input_.data(), topic_input_.size()))
        set_topic(std::string(topic_input_.data()));

    const char* modes[] = {"Flat", "Intensity", "Height", "RGB"};
    int current_mode = static_cast<int>(color_mode_);
    if (ImGui::Combo("Color Mode", &current_mode, modes, 4))
        color_mode_ = static_cast<ColorMode>(current_mode);

    ImGui::SliderFloat("Point Size", &point_size_, 1.0f, 10.0f, "%.1f");
    ImGui::SliderInt("Max Points", &max_points_, 1'000, 500'000);
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);

    const auto frame = latest_frame();
    if (frame.has_value())
    {
        ImGui::Text("Latest points: %zu / %zu",
                    frame->point_count,
                    frame->original_point_count);
        ImGui::Text("Frame: %s", frame->frame_id.empty() ? "(none)" : frame->frame_id.c_str());
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void PointCloudDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    std::snprintf(topic_input_.data(), topic_input_.size(), "%s", topic_.c_str());
    resubscribe_requested_ = true;
}

std::string PointCloudDisplay::serialize_config_blob() const
{
    char buffer[256];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "topic=%s;color_mode=%d;point_size=%.2f;max_points=%d;use_message_stamp=%d",
                  topic_.c_str(),
                  static_cast<int>(color_mode_),
                  point_size_,
                  max_points_,
                  use_message_stamp_ ? 1 : 0);
    return buffer;
}

void PointCloudDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char topic[256] = {};
    int color_mode = static_cast<int>(color_mode_);
    float point_size = point_size_;
    int max_points = max_points_;
    int use_message_stamp = use_message_stamp_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];color_mode=%d;point_size=%f;max_points=%d;use_message_stamp=%d",
                    topic,
                    &color_mode,
                    &point_size,
                    &max_points,
                    &use_message_stamp) >= 1)
    {
        set_topic(topic);
        color_mode_ = static_cast<ColorMode>(std::clamp(color_mode, 0, 3));
        if (point_size > 0.0f)
            point_size_ = point_size;
        if (max_points > 0)
            max_points_ = max_points;
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void PointCloudDisplay::ingest_pointcloud_frame(const PointCloudFrame& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_ = frame;
}

std::optional<PointCloudFrame> PointCloudDisplay::latest_frame() const
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return latest_frame_;
}

void PointCloudDisplay::ensure_subscription()
{
#ifdef SPECTRA_USE_ROS2
    if (!resubscribe_requested_)
        return;

    resubscribe_requested_ = false;
    subscription_.reset();
    subscribed_topic_.clear();

    if (!node_ || !topic_discovery_ || topic_.empty())
        return;

    const TopicInfo info = topic_discovery_->topic(topic_);
    if (info.name.empty() || info.types.empty())
    {
        status_ = DisplayStatus::Warn;
        status_text_ = "Topic not discovered yet";
        return;
    }
    if (info.types.front() != "sensor_msgs/msg/PointCloud2")
    {
        status_ = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<sensor_msgs::msg::PointCloud2>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(4)).best_effort(),
        [this](const sensor_msgs::msg::PointCloud2::SharedPtr msg)
        {
            const auto frame = adapt_pointcloud_message(*msg,
                                                        topic_,
                                                        static_cast<size_t>(std::max(1, max_points_)));
            if (frame.has_value())
                ingest_pointcloud_frame(*frame);
        });

    subscribed_topic_ = topic_;
    status_ = DisplayStatus::Ok;
    status_text_ = "Subscribed to " + topic_;
#endif
}

const char* PointCloudDisplay::color_mode_name(ColorMode mode)
{
    switch (mode)
    {
        case ColorMode::Flat: return "flat";
        case ColorMode::Intensity: return "intensity";
        case ColorMode::Height: return "height";
        case ColorMode::RGB: return "rgb";
    }
    return "flat";
}

}   // namespace spectra::adapters::ros2
