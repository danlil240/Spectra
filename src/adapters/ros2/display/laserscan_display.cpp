#include "display/laserscan_display.hpp"

#include <algorithm>
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

LaserScanDisplay::LaserScanDisplay()
{
    set_topic("/scan");
}

void LaserScanDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_ = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_ = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_ = DisplayStatus::Warn;
    status_text_ = "Waiting for laser scan topic";
}

void LaserScanDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_ = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void LaserScanDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(scans_mutex_);
    scans_.clear();
}

void LaserScanDisplay::on_update(float)
{
    ensure_subscription();

    std::lock_guard<std::mutex> lock(scans_mutex_);
    while (static_cast<int>(scans_.size()) > trail_size_)
        scans_.pop_front();

    if (status_ == DisplayStatus::Disabled)
        return;
    if (scans_.empty())
    {
        status_ = subscribed_topic_.empty()
            ? DisplayStatus::Warn
            : DisplayStatus::Warn;
        status_text_ = subscribed_topic_.empty()
            ? "Waiting for laser scan topic"
            : "Subscribed, no scans received";
        return;
    }

    status_ = DisplayStatus::Ok;
    status_text_ = std::to_string(scans_.back().point_count) + " points";
    if (scans_.size() > 1)
        status_text_ += " x" + std::to_string(scans_.size());
}

void LaserScanDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    std::deque<LaserScanFrame> scans;
    {
        std::lock_guard<std::mutex> lock(scans_mutex_);
        scans = scans_;
    }

    for (size_t index = 0; index < scans.size(); ++index)
    {
        const auto& frame = scans[index];
        bool tf_ok = true;
        const spectra::Transform frame_transform = resolve_frame_transform(
            tf_buffer_,
            fixed_frame_,
            frame.frame_id,
            frame.stamp_ns,
            use_message_stamp_,
            tf_ok);
        if (!tf_ok)
            continue;

        SceneEntity entity;
        entity.type = "laserscan";
        entity.label = topic_.empty()
            ? display_name()
            : topic_ + " [" + std::to_string(index + 1) + "/" + std::to_string(scans.size()) + "]";
        entity.display_name = display_name();
        entity.topic = frame.topic;
        entity.frame_id = frame.frame_id;
        entity.transform = frame_transform;
        entity.transform.translation = frame_transform.transform_point(frame.centroid);
        entity.scale = frame.max_bounds - frame.min_bounds;
        entity.stamp_ns = frame.stamp_ns;
        entity.properties.push_back({"points", std::to_string(frame.point_count)});
        entity.properties.push_back({"render_style", render_style_name(render_style_)});
        entity.properties.push_back({"color_mode", color_mode_name(color_mode_)});
        entity.properties.push_back({"min_range", std::to_string(frame.min_range)});
        entity.properties.push_back({"max_range", std::to_string(frame.max_range)});
        entity.properties.push_back({"average_range", std::to_string(frame.average_range)});
        entity.properties.push_back({"has_intensity", frame.has_intensity ? "true" : "false"});
        scene.add_entity(std::move(entity));
    }
}

void LaserScanDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::InputText("Topic", topic_input_.data(), topic_input_.size()))
        set_topic(std::string(topic_input_.data()));

    const char* render_styles[] = {"Points", "Lines"};
    int render_style = static_cast<int>(render_style_);
    if (ImGui::Combo("Render Style", &render_style, render_styles, 2))
        render_style_ = static_cast<RenderStyle>(render_style);

    const char* color_modes[] = {"Flat", "Range", "Intensity"};
    int color_mode = static_cast<int>(color_mode_);
    if (ImGui::Combo("Color Mode", &color_mode, color_modes, 3))
        color_mode_ = static_cast<ColorMode>(color_mode);

    ImGui::SliderInt("Trail Size", &trail_size_, 1, 32);
    ImGui::SliderFloat("Min Range", &min_range_filter_, 0.0f, 50.0f, "%.2f");
    ImGui::SliderFloat("Max Range", &max_range_filter_, 0.1f, 200.0f, "%.2f");
    if (max_range_filter_ < min_range_filter_)
        max_range_filter_ = min_range_filter_;
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);
    ImGui::Text("Stored scans: %zu", scan_count());
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void LaserScanDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    std::snprintf(topic_input_.data(), topic_input_.size(), "%s", topic_.c_str());
    resubscribe_requested_ = true;
}

std::string LaserScanDisplay::serialize_config_blob() const
{
    char buffer[320];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "topic=%s;render_style=%d;color_mode=%d;trail_size=%d;min_range=%.3f;max_range=%.3f;use_message_stamp=%d",
                  topic_.c_str(),
                  static_cast<int>(render_style_),
                  static_cast<int>(color_mode_),
                  trail_size_,
                  min_range_filter_,
                  max_range_filter_,
                  use_message_stamp_ ? 1 : 0);
    return buffer;
}

void LaserScanDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char topic[256] = {};
    int render_style = static_cast<int>(render_style_);
    int color_mode = static_cast<int>(color_mode_);
    int trail_size = trail_size_;
    float min_range = min_range_filter_;
    float max_range = max_range_filter_;
    int use_message_stamp = use_message_stamp_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];render_style=%d;color_mode=%d;trail_size=%d;min_range=%f;max_range=%f;use_message_stamp=%d",
                    topic,
                    &render_style,
                    &color_mode,
                    &trail_size,
                    &min_range,
                    &max_range,
                    &use_message_stamp) >= 1)
    {
        set_topic(topic);
        render_style_ = static_cast<RenderStyle>(std::clamp(render_style, 0, 1));
        color_mode_ = static_cast<ColorMode>(std::clamp(color_mode, 0, 2));
        if (trail_size > 0)
            trail_size_ = trail_size;
        if (min_range >= 0.0f)
            min_range_filter_ = min_range;
        if (max_range > 0.0f)
            max_range_filter_ = max_range;
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void LaserScanDisplay::ingest_scan_frame(const LaserScanFrame& frame)
{
    std::lock_guard<std::mutex> lock(scans_mutex_);
    scans_.push_back(frame);
    while (static_cast<int>(scans_.size()) > trail_size_)
        scans_.pop_front();
}

size_t LaserScanDisplay::scan_count() const
{
    std::lock_guard<std::mutex> lock(scans_mutex_);
    return scans_.size();
}

void LaserScanDisplay::ensure_subscription()
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
    if (info.types.front() != "sensor_msgs/msg/LaserScan")
    {
        status_ = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<sensor_msgs::msg::LaserScan>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(32)).best_effort(),
        [this](const sensor_msgs::msg::LaserScan::SharedPtr msg)
        {
            const auto frame = adapt_laserscan_message(*msg,
                                                       topic_,
                                                       min_range_filter_,
                                                       max_range_filter_);
            if (frame.has_value())
                ingest_scan_frame(*frame);
        });

    subscribed_topic_ = topic_;
    status_ = DisplayStatus::Ok;
    status_text_ = "Subscribed to " + topic_;
#endif
}

const char* LaserScanDisplay::render_style_name(RenderStyle style)
{
    return style == RenderStyle::Lines ? "lines" : "points";
}

const char* LaserScanDisplay::color_mode_name(ColorMode mode)
{
    switch (mode)
    {
        case ColorMode::Flat: return "flat";
        case ColorMode::Range: return "range";
        case ColorMode::Intensity: return "intensity";
    }
    return "flat";
}

}   // namespace spectra::adapters::ros2
