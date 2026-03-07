// tf_tree_panel.cpp — TF transform tree viewer implementation

#include "ui/tf_tree_panel.hpp"

#include <cstdio>

#include "messages/tf_adapter.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

TfTreePanel::TfTreePanel() = default;

TfTreePanel::~TfTreePanel()
{
    stop();
}

#ifdef SPECTRA_USE_ROS2
void TfTreePanel::set_node(rclcpp::Node::SharedPtr node)
{
    node_ = std::move(node);
}
#endif

void TfTreePanel::start()
{
    if (started_.load(std::memory_order_acquire))
        return;

#ifdef SPECTRA_USE_ROS2
    if (!node_)
        return;

    sub_tf_ = node_->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf",
        rclcpp::QoS(rclcpp::KeepLast(100)),
        [this](const tf2_msgs::msg::TFMessage::SharedPtr msg) { on_tf_message(*msg, false); });

    rclcpp::QoS static_qos(rclcpp::KeepLast(100));
    static_qos.transient_local();
    sub_tf_static_ = node_->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf_static",
        static_qos,
        [this](const tf2_msgs::msg::TFMessage::SharedPtr msg) { on_tf_message(*msg, true); });
#endif

    started_.store(true, std::memory_order_release);
}

void TfTreePanel::stop()
{
    if (!started_.load(std::memory_order_acquire))
        return;

    started_.store(false, std::memory_order_release);

#ifdef SPECTRA_USE_ROS2
    sub_tf_.reset();
    sub_tf_static_.reset();
#endif
}

void TfTreePanel::set_stale_threshold_ms(uint64_t ms)
{
    buffer_.set_stale_threshold_ms(ms);
}

uint64_t TfTreePanel::stale_threshold_ms() const
{
    return buffer_.stale_threshold_ms();
}

void TfTreePanel::set_hz_window_ms(uint64_t ms)
{
    buffer_.set_hz_window_ms(ms);
}

uint64_t TfTreePanel::hz_window_ms() const
{
    return buffer_.hz_window_ms();
}

void TfTreePanel::set_title(const std::string& title)
{
    title_ = title;
}

const std::string& TfTreePanel::title() const
{
    return title_;
}

TfTreeSnapshot TfTreePanel::snapshot() const
{
    return buffer_.snapshot();
}

size_t TfTreePanel::frame_count() const
{
    return buffer_.frame_count();
}

bool TfTreePanel::has_frame(const std::string& frame_id) const
{
    return buffer_.has_frame(frame_id);
}

TransformResult TfTreePanel::lookup_transform(const std::string& source_frame,
                                              const std::string& target_frame) const
{
    return buffer_.lookup_transform(source_frame, target_frame);
}

bool TfTreePanel::can_transform(const std::string& source_frame,
                                const std::string& target_frame) const
{
    return buffer_.can_transform(source_frame, target_frame);
}

void TfTreePanel::inject_transform(const TransformStamp& ts)
{
    buffer_.inject_transform(ts);
}

void TfTreePanel::clear()
{
    buffer_.clear();
}

void TfTreePanel::set_select_callback(FrameSelectCallback cb)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    select_cb_ = std::move(cb);
}

#ifdef SPECTRA_USE_ROS2
void TfTreePanel::on_tf_message(const tf2_msgs::msg::TFMessage& msg, bool is_static)
{
    for (const auto& transform : msg.transforms)
        inject_transform(adapt_tf_transform(transform, is_static));
}
#endif

#ifdef SPECTRA_USE_IMGUI
namespace
{
void draw_tree_node(const std::string& frame_id,
                    const TfTreeSnapshot& snapshot,
                    const std::string& filter,
                    std::string& selected_frame,
                    const TfTreePanel::FrameSelectCallback& callback,
                    bool show_static,
                    bool show_dynamic)
{
    const TfFrameStats* stats = nullptr;
    for (const auto& frame : snapshot.frames)
    {
        if (frame.frame_id == frame_id)
        {
            stats = &frame;
            break;
        }
    }

    if (!stats)
        return;
    if (stats->is_static && !show_static)
        return;
    if (!stats->is_static && !show_dynamic)
        return;
    if (!filter.empty()
        && frame_id.find(filter) == std::string::npos
        && stats->parent_frame_id.find(filter) == std::string::npos)
        return;

    const auto children_it = snapshot.children.find(frame_id);
    const bool has_children = children_it != snapshot.children.end() && !children_it->second.empty();

    ImGuiTreeNodeFlags flags = has_children ? ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
                                            : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected_frame == frame_id)
        flags |= ImGuiTreeNodeFlags_Selected;

    const bool opened = ImGui::TreeNodeEx(frame_id.c_str(), flags, "%s", frame_id.c_str());
    if (ImGui::IsItemClicked())
    {
        selected_frame = frame_id;
        if (callback)
            callback(frame_id);
    }

    ImGui::SameLine();
    if (stats->is_static)
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "[S]");
    else if (stats->hz > 0.0)
        ImGui::Text("%.1f Hz", stats->hz);
    else
        ImGui::TextDisabled("0 Hz");

    if (stats->stale)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.1f, 1.0f), "STALE");
    }

    if (has_children && opened)
    {
        for (const auto& child : children_it->second)
            draw_tree_node(child, snapshot, filter, selected_frame, callback, show_static, show_dynamic);
        ImGui::TreePop();
    }
}
}   // namespace

void TfTreePanel::draw_inline()
{
    if (!ImGui::GetCurrentContext())
        return;

    const TfTreeSnapshot snapshot = buffer_.snapshot();

    ImGui::PushItemWidth(180.0f);
    ImGui::InputTextWithHint("##tf_filter", "Filter frames...", filter_buf_, sizeof(filter_buf_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Checkbox("Static", &show_static_);
    ImGui::SameLine();
    ImGui::Checkbox("Dynamic", &show_dynamic_);
    ImGui::SameLine();
    if (ImGui::Button("Clear##tf_tree"))
        clear();

    ImGui::Text("%u frames | %u static | %u dynamic | %u stale",
                snapshot.total_frames,
                snapshot.static_frames,
                snapshot.dynamic_frames,
                snapshot.stale_frames);
    ImGui::Separator();

    FrameSelectCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = select_cb_;
    }

    const std::string filter(filter_buf_);
    ImGui::BeginChild("##tf_tree_scroll", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.55f), false);
    if (snapshot.frames.empty())
    {
        ImGui::TextDisabled("No TF frames received yet.");
    }
    else if (snapshot.roots.empty())
    {
        for (const auto& frame : snapshot.frames)
            draw_tree_node(frame.frame_id, snapshot, filter, selected_frame_, callback, show_static_, show_dynamic_);
    }
    else
    {
        for (const auto& root : snapshot.roots)
            draw_tree_node(root, snapshot, filter, selected_frame_, callback, show_static_, show_dynamic_);
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (!selected_frame_.empty())
    {
        for (const auto& frame : snapshot.frames)
        {
            if (frame.frame_id != selected_frame_)
                continue;

            ImGui::Text("Selected: %s", frame.frame_id.c_str());
            ImGui::Text("Parent: %s",
                        frame.parent_frame_id.empty() ? "(root)" : frame.parent_frame_id.c_str());
            ImGui::Text("Type: %s", frame.is_static ? "static" : "dynamic");
            if (frame.ever_received)
            {
                ImGui::Text("Age: %llu ms", static_cast<unsigned long long>(frame.age_ms));
                ImGui::Text("Translation: (%.3f, %.3f, %.3f)",
                            frame.last_transform.tx,
                            frame.last_transform.ty,
                            frame.last_transform.tz);
                ImGui::Text("Quaternion: (%.3f, %.3f, %.3f, %.3f)",
                            frame.last_transform.qx,
                            frame.last_transform.qy,
                            frame.last_transform.qz,
                            frame.last_transform.qw);
            }
            break;
        }
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("Transform Lookup"))
    {
        static char source_buf[128]{};
        static char target_buf[128]{};
        ImGui::InputText("Source##tf_lookup", source_buf, sizeof(source_buf));
        ImGui::InputText("Target##tf_lookup", target_buf, sizeof(target_buf));
        if (ImGui::Button("Lookup"))
        {
            lookup_source_ = source_buf;
            lookup_target_ = target_buf;
        }

        if (!lookup_source_.empty() && !lookup_target_.empty())
        {
            const TransformResult result = buffer_.lookup_transform(lookup_source_, lookup_target_);
            if (result.ok)
            {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "OK");
                ImGui::Text("Translation: (%.3f, %.3f, %.3f)", result.tx, result.ty, result.tz);
                ImGui::Text("Euler: roll %.1f pitch %.1f yaw %.1f",
                            result.roll_deg,
                            result.pitch_deg,
                            result.yaw_deg);
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.1f, 1.0f), "%s", result.error.c_str());
            }
        }
    }
}

void TfTreePanel::draw(bool* p_open)
{
    if (!ImGui::GetCurrentContext())
        return;
    if (!ImGui::Begin(title_.c_str(), p_open))
    {
        ImGui::End();
        return;
    }
    draw_inline();
    ImGui::End();
}

#else

void TfTreePanel::draw_inline() {}
void TfTreePanel::draw(bool*) {}

#endif

}   // namespace spectra::adapters::ros2
