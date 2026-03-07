#pragma once

// TfTreePanel — ROS2 TF transform tree viewer.
//
// Subscribes to /tf and /tf_static via rclcpp::GenericSubscription (using
// tf2_msgs/msg/TFMessage).  Maintains a live tree of coordinate frames,
// computes per-transform Hz and age, raises stale warnings when a dynamic
// transform has not been updated within a configurable threshold.
//
// Features:
//   - Dual subscription: /tf (dynamic) + /tf_static (static, latched)
//   - Tree view: parent → child frames with depth-based indentation
//   - Per-frame Hz badge (rolling 1 s window)
//   - Per-frame age display (time since last update, in ms)
//   - Stale warning highlight when age > stale_threshold_ms
//   - Transform lookup: compute relative transform between any two frames
//   - Search/filter bar to quickly locate frames
//   - Auto-refresh layout when tree topology changes
//
// Thread-safety:
//   on_tf_message() / on_tf_static_message() are called on the ROS2 spin
//   thread.  All shared state is protected by mutex_.
//   draw() must be called from the ImGui render thread only.
//
// Non-ImGui build:
//   All draw*() methods compile to no-ops when SPECTRA_USE_IMGUI is not
//   defined.  All tree-building and transform logic remains available.
//
// Typical usage:
//   TfTreePanel panel;
//   panel.set_node(bridge.node());
//   panel.start();   // creates subscriptions
//   // In render loop:
//   panel.draw(&open);

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// Forward-declare rclcpp types to avoid pulling in all ROS2 headers
// in downstream code that only uses the panel's non-ROS2 API.
#ifdef SPECTRA_USE_ROS2
#include <rclcpp/rclcpp.hpp>
#include <tf2_msgs/msg/tf_message.hpp>
#endif

#include "tf/tf_buffer.hpp"

namespace spectra::adapters::ros2
{

class TfTreePanel
{
public:
    TfTreePanel();
    ~TfTreePanel();

    TfTreePanel(const TfTreePanel&)            = delete;
    TfTreePanel& operator=(const TfTreePanel&) = delete;
    TfTreePanel(TfTreePanel&&)                 = delete;
    TfTreePanel& operator=(TfTreePanel&&)      = delete;

    // ---------- lifecycle -----------------------------------------------

#ifdef SPECTRA_USE_ROS2
    // Wire the panel to a ROS2 node.  Must be called before start().
    void set_node(rclcpp::Node::SharedPtr node);
    rclcpp::Node::SharedPtr node() const { return node_; }
#endif

    // Create /tf and /tf_static subscriptions.
    // No-op if already started or no node is set.
    void start();

    // Cancel subscriptions and clear internal state.
    void stop();

    bool is_started() const { return started_.load(std::memory_order_acquire); }

    // ---------- configuration -------------------------------------------

    // Threshold in milliseconds; dynamic frames older than this are "stale".
    // Default: 500 ms.
    void     set_stale_threshold_ms(uint64_t ms);
    uint64_t stale_threshold_ms() const;

    // Hz computation window (default: 1000 ms).
    void     set_hz_window_ms(uint64_t ms);
    uint64_t hz_window_ms() const;

    // ImGui panel title.
    void               set_title(const std::string& t);
    const std::string& title() const;

    // ---------- data access (render-thread safe via snapshot) -----------

    // Take a thread-safe snapshot of the current tree state.
    TfTreeSnapshot snapshot() const;

    // Number of known frames.
    size_t frame_count() const;

    // True if the named frame is known.
    bool has_frame(const std::string& frame_id) const;

    // Compute the transform from source_frame to target_frame by walking the
    // tree (static transforms only for simplicity — dynamic path via chain).
    // Returns !ok if either frame is unknown or no path exists.
    TransformResult lookup_transform(const std::string& source_frame,
                                     const std::string& target_frame) const;
    bool can_transform(const std::string& source_frame,
                       const std::string& target_frame) const;

    // Inject a transform directly (used by unit tests without a ROS2 node).
    void inject_transform(const TransformStamp& ts);

    // Clear all stored transforms.
    void clear();

    // ---------- ImGui rendering (render-thread only) --------------------

    // Draw the full dockable TF tree panel.
    // Pass p_open = nullptr to draw without a close button.
    void draw(bool* p_open = nullptr);

    // Draw just the tree content inline (no ImGui::Begin/End wrapper).
    // Useful for embedding in an existing panel.
    void draw_inline();

    // ---------- callbacks -----------------------------------------------

    using FrameSelectCallback = std::function<void(const std::string& frame_id)>;
    void set_select_callback(FrameSelectCallback cb);

    TfBuffer&       buffer() { return buffer_; }
    const TfBuffer& buffer() const { return buffer_; }

private:
#ifdef SPECTRA_USE_ROS2
    void on_tf_message(const tf2_msgs::msg::TFMessage& msg, bool is_static);

    rclcpp::Node::SharedPtr                     node_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr sub_tf_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr sub_tf_static_;
#endif

    std::atomic<bool>   started_{false};
    std::string title_{"TF Frames"};
    TfBuffer    buffer_;

    mutable std::mutex callback_mutex_;

    FrameSelectCallback select_cb_;

    // UI state (render-thread only, no mutex needed)
    std::string  selected_frame_;
    std::string  lookup_source_;
    std::string  lookup_target_;
    char         filter_buf_[128]{};
    bool         show_lookup_{false};
    bool         show_static_{true};
    bool         show_dynamic_{true};
};

}   // namespace spectra::adapters::ros2
