#include "display/tf_display.hpp"

#include <algorithm>
#include <cstdio>

#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

void TfDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_   = context.tf_buffer;
    fixed_frame_ = context.fixed_frame;
    status_      = tf_buffer_ != nullptr ? DisplayStatus::Ok : DisplayStatus::Error;
    status_text_ = tf_buffer_ != nullptr ? "Ready" : "TF buffer unavailable";
}

void TfDisplay::on_disable()
{
    frames_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void TfDisplay::on_update(float)
{
    frames_.clear();
    if (!tf_buffer_)
    {
        status_      = DisplayStatus::Error;
        status_text_ = "TF buffer unavailable";
        return;
    }
    if (fixed_frame_.empty())
    {
        status_      = DisplayStatus::Warn;
        status_text_ = "Waiting for fixed frame";
        return;
    }

    const TfTreeSnapshot snapshot = tf_buffer_->snapshot();
    frames_.reserve(std::min(static_cast<int>(snapshot.frames.size()), max_frames_));

    for (const auto& frame : snapshot.frames)
    {
        if (static_cast<int>(frames_.size()) >= max_frames_)
            break;

        TransformResult result;
        if (frame.frame_id == fixed_frame_)
        {
            result.ok = true;
            result.qw = 1.0;
        }
        else
        {
            const uint64_t lookup_time_ns = frame.last_transform.recv_ns != 0
                                                ? frame.last_transform.recv_ns
                                                : snapshot.snapshot_ns;
            result = tf_buffer_->lookup_transform(fixed_frame_, frame.frame_id, lookup_time_ns);
        }
        if (!result.ok)
            continue;

        FrameVisual visual;
        visual.frame_id              = frame.frame_id;
        visual.parent_frame_id       = frame.parent_frame_id;
        visual.transform.translation = {result.tx, result.ty, result.tz};
        visual.transform.rotation    = {
            static_cast<float>(result.qx),
            static_cast<float>(result.qy),
            static_cast<float>(result.qz),
            static_cast<float>(result.qw),
        };
        visual.hz        = frame.hz;
        visual.age_ms    = frame.age_ms;
        visual.is_static = frame.is_static;
        visual.stale     = frame.stale;
        frames_.push_back(std::move(visual));
    }

    status_ = frames_.empty() ? DisplayStatus::Warn : DisplayStatus::Ok;
    status_text_ =
        frames_.empty() ? "No TF frames resolved" : std::to_string(frames_.size()) + " frames";
}

void TfDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    for (const auto& frame : frames_)
    {
        SceneEntity entity;
        entity.type         = "tf_frame";
        entity.label        = frame.frame_id;
        entity.display_name = display_name();
        entity.frame_id     = frame.frame_id;
        entity.transform    = frame.transform;
        entity.scale        = {axis_scale_, axis_scale_, axis_scale_};
        entity.properties.push_back(
            {"parent", frame.parent_frame_id.empty() ? "(root)" : frame.parent_frame_id});
        entity.properties.push_back({"hz", frame.is_static ? "static" : std::to_string(frame.hz)});
        entity.properties.push_back({"age_ms", std::to_string(frame.age_ms)});
        entity.properties.push_back({"stale", frame.stale ? "true" : "false"});
        entity.properties.push_back({"labels", show_labels_ ? "true" : "false"});
        scene.add_entity(std::move(entity));
    }
}

void TfDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    ImGui::Text("Resolved frames: %zu", frames_.size());
    ImGui::Checkbox("Show Labels", &show_labels_);
    ImGui::SliderFloat("Axis Scale", &axis_scale_, 0.05f, 2.0f, "%.2f");
    ImGui::SliderInt("Max Frames", &max_frames_, 1, 2048);
    ImGui::TextWrapped("Fixed frame: %s", fixed_frame_.empty() ? "(unset)" : fixed_frame_.c_str());
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

std::string TfDisplay::serialize_config_blob() const
{
    char buffer[128];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "labels=%d;axis_scale=%.3f;max_frames=%d",
                  show_labels_ ? 1 : 0,
                  axis_scale_,
                  max_frames_);
    return buffer;
}

void TfDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    int   labels     = show_labels_ ? 1 : 0;
    float axis_scale = axis_scale_;
    int   max_frames = max_frames_;
    if (std::sscanf(blob.c_str(),
                    "labels=%d;axis_scale=%f;max_frames=%d",
                    &labels,
                    &axis_scale,
                    &max_frames)
        >= 1)
    {
        show_labels_ = labels != 0;
        if (axis_scale > 0.0f)
            axis_scale_ = axis_scale;
        if (max_frames > 0)
            max_frames_ = max_frames;
    }
}

}   // namespace spectra::adapters::ros2
