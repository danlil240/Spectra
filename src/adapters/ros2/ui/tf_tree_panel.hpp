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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward-declare rclcpp types to avoid pulling in all ROS2 headers
// in downstream code that only uses the panel's non-ROS2 API.
#ifdef SPECTRA_USE_ROS2
#include <rclcpp/rclcpp.hpp>
#include <tf2_msgs/msg/tf_message.hpp>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Transform stamp — lightweight record of one received transform
// ---------------------------------------------------------------------------

struct TransformStamp
{
    std::string parent_frame;
    std::string child_frame;

    // Translation
    double tx{0.0};
    double ty{0.0};
    double tz{0.0};

    // Rotation (quaternion)
    double qx{0.0};
    double qy{0.0};
    double qz{0.0};
    double qw{1.0};

    // Wall time of receipt (nanoseconds since epoch)
    uint64_t recv_ns{0};

    bool is_static{false};
};

// ---------------------------------------------------------------------------
// TfFrameStats — per-frame rolling statistics (Hz + age)
// ---------------------------------------------------------------------------

struct TfFrameStats
{
    std::string frame_id;
    std::string parent_frame_id;   // empty for root frames

    bool        is_static{false};

    // Last received transform data
    TransformStamp last_transform;

    // Rolling receive timestamps (for Hz computation)
    std::deque<uint64_t> recv_timestamps_ns;

    // Computed fields (updated by compute())
    double   hz{0.0};              // messages/second
    uint64_t age_ms{0};            // ms since last update (0 if never received)
    bool     stale{false};         // age_ms > stale_threshold_ms
    bool     ever_received{false}; // received at least one transform

    // Update with a new arrival; prune old timestamps
    void push(uint64_t now_ns, uint64_t stale_threshold_ms);
    void compute(uint64_t now_ns, uint64_t stale_threshold_ms,
                 uint64_t hz_window_ns = 1'000'000'000ULL);
};

// ---------------------------------------------------------------------------
// TfTreeSnapshot — immutable snapshot of the full TF tree (copy for rendering)
// ---------------------------------------------------------------------------

struct TfTreeSnapshot
{
    // Flat list of all known frames (order = discovery order)
    std::vector<TfFrameStats> frames;

    // Root frames (those with no known parent or parent == "map" etc.)
    std::vector<std::string> roots;

    // Children map: parent_frame → vector of child frame_ids
    std::unordered_map<std::string, std::vector<std::string>> children;

    uint64_t snapshot_ns{0};   // wall time when snapshot was taken
    uint32_t total_frames{0};
    uint32_t static_frames{0};
    uint32_t dynamic_frames{0};
    uint32_t stale_frames{0};
};

// ---------------------------------------------------------------------------
// TransformResult — output of lookup_transform()
// ---------------------------------------------------------------------------

struct TransformResult
{
    bool   ok{false};
    double tx{0.0}, ty{0.0}, tz{0.0};
    double qx{0.0}, qy{0.0}, qz{0.0}, qw{1.0};
    std::string error;   // human-readable reason if !ok

    // Euler angles in degrees (computed from quaternion)
    double roll_deg{0.0}, pitch_deg{0.0}, yaw_deg{0.0};
};

// ---------------------------------------------------------------------------
// TfTreePanel
// ---------------------------------------------------------------------------

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

private:
    // Internal helpers (mutex already held by caller unless noted)
    void rebuild_tree_unlocked();
    void prune_old_timestamps_unlocked(uint64_t now_ns);
    void update_frame_unlocked(const TransformStamp& ts, uint64_t now_ns);

    // Walk tree upward from frame to root; returns chain (leaf→root order).
    std::vector<std::string> chain_to_root_unlocked(const std::string& frame) const;

    // Compose two transforms: parent * child → result
    static TransformStamp compose(const TransformStamp& parent,
                                  const TransformStamp& child);

    // Invert a transform
    static TransformStamp invert(const TransformStamp& t);

    // Quaternion → Euler (ZYX convention), output in degrees
    static void quat_to_euler_deg(double qx, double qy, double qz, double qw,
                                  double& roll, double& pitch, double& yaw);

#ifdef SPECTRA_USE_ROS2
    void on_tf_message(const tf2_msgs::msg::TFMessage& msg, bool is_static);

    rclcpp::Node::SharedPtr                     node_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr sub_tf_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr sub_tf_static_;
#endif

    // Per-frame data (keyed by child_frame_id)
    std::unordered_map<std::string, TfFrameStats> frames_;

    // Tree topology (child → parent)
    std::unordered_map<std::string, std::string> parent_of_;

    // Children map (parent → sorted child list)
    std::unordered_map<std::string, std::vector<std::string>> children_of_;

    mutable std::mutex mutex_;

    std::atomic<bool>   started_{false};

    uint64_t stale_threshold_ms_{500};
    uint64_t hz_window_ms_{1000};
    std::string title_{"TF Frames"};

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
