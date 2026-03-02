// tf_tree_panel.cpp — TF transform tree viewer implementation

#include "ui/tf_tree_panel.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers — wall clock
// ---------------------------------------------------------------------------

static uint64_t now_ns()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch())
        .count());
}

// ---------------------------------------------------------------------------
// TfFrameStats
// ---------------------------------------------------------------------------

void TfFrameStats::push(uint64_t now_ns_val, uint64_t stale_threshold_ms_val)
{
    recv_timestamps_ns.push_back(now_ns_val);
    ever_received = true;
    stale = false;   // just received — reset stale
    (void)stale_threshold_ms_val;
}

void TfFrameStats::compute(uint64_t now_ns_val, uint64_t stale_threshold_ms_val,
                            uint64_t hz_window_ns)
{
    // Prune timestamps older than the Hz window
    const uint64_t window_start = (now_ns_val > hz_window_ns)
                                      ? (now_ns_val - hz_window_ns)
                                      : 0;
    while (!recv_timestamps_ns.empty() &&
           recv_timestamps_ns.front() < window_start) {
        recv_timestamps_ns.pop_front();
    }

    // Hz = count in window / window_duration_s
    const double window_s = static_cast<double>(hz_window_ns) / 1e9;
    hz = (window_s > 0.0)
             ? static_cast<double>(recv_timestamps_ns.size()) / window_s
             : 0.0;

    // Age
    if (ever_received) {
        const uint64_t last = last_transform.recv_ns;
        age_ms = (now_ns_val >= last) ? (now_ns_val - last) / 1'000'000ULL : 0ULL;
    } else {
        age_ms = 0;
    }

    // Stale check — static transforms are never stale
    if (is_static) {
        stale = false;
    } else {
        stale = ever_received && (age_ms > stale_threshold_ms_val);
    }
}

// ---------------------------------------------------------------------------
// TfTreePanel — construction / destruction
// ---------------------------------------------------------------------------

TfTreePanel::TfTreePanel() = default;

TfTreePanel::~TfTreePanel()
{
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_ROS2
void TfTreePanel::set_node(rclcpp::Node::SharedPtr node)
{
    std::lock_guard<std::mutex> lk(mutex_);
    node_ = std::move(node);
}
#endif

void TfTreePanel::start()
{
    if (started_.load(std::memory_order_acquire)) return;

#ifdef SPECTRA_USE_ROS2
    std::lock_guard<std::mutex> lk(mutex_);
    if (!node_) return;

    // Subscribe /tf (dynamic)
    sub_tf_ = node_->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf",
        rclcpp::QoS(rclcpp::KeepLast(100)),
        [this](const tf2_msgs::msg::TFMessage::SharedPtr msg) {
            on_tf_message(*msg, false);
        });

    // Subscribe /tf_static (static, transient_local = latched equivalent)
    rclcpp::QoS static_qos(rclcpp::KeepLast(100));
    static_qos.transient_local();
    sub_tf_static_ = node_->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf_static",
        static_qos,
        [this](const tf2_msgs::msg::TFMessage::SharedPtr msg) {
            on_tf_message(*msg, true);
        });
#endif

    started_.store(true, std::memory_order_release);
}

void TfTreePanel::stop()
{
    if (!started_.load(std::memory_order_acquire)) return;
    started_.store(false, std::memory_order_release);

#ifdef SPECTRA_USE_ROS2
    std::lock_guard<std::mutex> lk(mutex_);
    sub_tf_.reset();
    sub_tf_static_.reset();
#endif
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void TfTreePanel::set_stale_threshold_ms(uint64_t ms)
{
    std::lock_guard<std::mutex> lk(mutex_);
    stale_threshold_ms_ = ms;
}

uint64_t TfTreePanel::stale_threshold_ms() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return stale_threshold_ms_;
}

void TfTreePanel::set_hz_window_ms(uint64_t ms)
{
    std::lock_guard<std::mutex> lk(mutex_);
    hz_window_ms_ = ms;
}

uint64_t TfTreePanel::hz_window_ms() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return hz_window_ms_;
}

void TfTreePanel::set_title(const std::string& t)
{
    std::lock_guard<std::mutex> lk(mutex_);
    title_ = t;
}

const std::string& TfTreePanel::title() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return title_;
}

// ---------------------------------------------------------------------------
// Internal — update one frame (mutex held by caller)
// ---------------------------------------------------------------------------

void TfTreePanel::update_frame_unlocked(const TransformStamp& ts, uint64_t recv_ns_val)
{
    auto& stats = frames_[ts.child_frame];
    stats.frame_id = ts.child_frame;
    stats.parent_frame_id = ts.parent_frame;
    stats.is_static = ts.is_static;
    stats.last_transform = ts;
    stats.last_transform.recv_ns = recv_ns_val;
    stats.push(recv_ns_val, stale_threshold_ms_);

    // Update topology
    parent_of_[ts.child_frame] = ts.parent_frame;

    // Ensure parent entry exists (so it shows as a root if not a child)
    if (frames_.find(ts.parent_frame) == frames_.end()) {
        TfFrameStats& pstats = frames_[ts.parent_frame];
        pstats.frame_id = ts.parent_frame;
        pstats.is_static = ts.is_static;
    }

    rebuild_tree_unlocked();
}

void TfTreePanel::rebuild_tree_unlocked()
{
    children_of_.clear();

    for (const auto& [child, parent] : parent_of_) {
        children_of_[parent].push_back(child);
    }

    // Sort children for deterministic display
    for (auto& [_, vec] : children_of_) {
        std::sort(vec.begin(), vec.end());
    }
}

// ---------------------------------------------------------------------------
// Internal — ROS2 message callback
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_ROS2
void TfTreePanel::on_tf_message(const tf2_msgs::msg::TFMessage& msg, bool is_static)
{
    const uint64_t recv_ns_val = now_ns();
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& t : msg.transforms) {
        TransformStamp ts;
        ts.parent_frame = t.header.frame_id;
        ts.child_frame  = t.child_frame_id;
        ts.tx = t.transform.translation.x;
        ts.ty = t.transform.translation.y;
        ts.tz = t.transform.translation.z;
        ts.qx = t.transform.rotation.x;
        ts.qy = t.transform.rotation.y;
        ts.qz = t.transform.rotation.z;
        ts.qw = t.transform.rotation.w;
        ts.recv_ns  = recv_ns_val;
        ts.is_static = is_static;
        update_frame_unlocked(ts, recv_ns_val);
    }
}
#endif

// ---------------------------------------------------------------------------
// Public API — inject (used by tests)
// ---------------------------------------------------------------------------

void TfTreePanel::inject_transform(const TransformStamp& ts)
{
    const uint64_t recv_ns_val = (ts.recv_ns != 0) ? ts.recv_ns : now_ns();
    std::lock_guard<std::mutex> lk(mutex_);
    TransformStamp ts_copy = ts;
    ts_copy.recv_ns = recv_ns_val;
    update_frame_unlocked(ts_copy, recv_ns_val);
}

void TfTreePanel::clear()
{
    std::lock_guard<std::mutex> lk(mutex_);
    frames_.clear();
    parent_of_.clear();
    children_of_.clear();
}

// ---------------------------------------------------------------------------
// Public API — data access
// ---------------------------------------------------------------------------

size_t TfTreePanel::frame_count() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return frames_.size();
}

bool TfTreePanel::has_frame(const std::string& frame_id) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return frames_.find(frame_id) != frames_.end();
}

TfTreeSnapshot TfTreePanel::snapshot() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    const uint64_t snap_ns = now_ns();
    const uint64_t stale_thresh = stale_threshold_ms_;
    const uint64_t hz_window_ns = hz_window_ms_ * 1'000'000ULL;

    TfTreeSnapshot s;
    s.snapshot_ns = snap_ns;

    // Copy and compute stats for each frame
    for (auto& [id, stats] : frames_) {
        TfFrameStats copy = stats;
        copy.compute(snap_ns, stale_thresh, hz_window_ns);
        s.frames.push_back(std::move(copy));
    }

    // Sort frames for deterministic ordering
    std::sort(s.frames.begin(), s.frames.end(),
              [](const TfFrameStats& a, const TfFrameStats& b) {
                  return a.frame_id < b.frame_id;
              });

    // Build children map from snapshot frames
    for (const auto& f : s.frames) {
        if (!f.parent_frame_id.empty()) {
            s.children[f.parent_frame_id].push_back(f.frame_id);
        }
    }
    for (auto& [_, vec] : s.children) {
        std::sort(vec.begin(), vec.end());
    }

    // Determine roots: frames whose parent either does not exist or is empty
    for (const auto& f : s.frames) {
        const bool has_parent = !f.parent_frame_id.empty() &&
                                (frames_.find(f.parent_frame_id) != frames_.end());
        if (!has_parent) {
            s.roots.push_back(f.frame_id);
        }
    }
    std::sort(s.roots.begin(), s.roots.end());

    // Counters
    s.total_frames = static_cast<uint32_t>(s.frames.size());
    for (const auto& f : s.frames) {
        if (f.is_static)      ++s.static_frames;
        else                  ++s.dynamic_frames;
        if (f.stale)          ++s.stale_frames;
    }

    return s;
}

// ---------------------------------------------------------------------------
// Transform lookup — chain composition
// ---------------------------------------------------------------------------

std::vector<std::string> TfTreePanel::chain_to_root_unlocked(
    const std::string& frame) const
{
    std::vector<std::string> chain;
    std::string current = frame;
    // Guard against cycles (max depth = frame count)
    const size_t max_depth = frames_.size() + 1;
    while (chain.size() < max_depth) {
        chain.push_back(current);
        auto it = parent_of_.find(current);
        if (it == parent_of_.end()) break;
        current = it->second;
    }
    return chain;
}

// static
TransformStamp TfTreePanel::invert(const TransformStamp& t)
{
    // Inverse of quaternion
    const double qw = t.qw, qx = -t.qx, qy = -t.qy, qz = -t.qz;

    // Inverse translation: R^{-1} * (-p)
    // R^{-1} q v = conjugate(q) * v * q
    const double tx = -(2.0 * (0.5 - qy * qy - qz * qz) * (-t.tx) +
                        2.0 * (qx * qy + qw * qz) * (-t.ty) +
                        2.0 * (qx * qz - qw * qy) * (-t.tz));
    const double ty = -(2.0 * (qx * qy - qw * qz) * (-t.tx) +
                        2.0 * (0.5 - qx * qx - qz * qz) * (-t.ty) +
                        2.0 * (qy * qz + qw * qx) * (-t.tz));
    const double tz = -(2.0 * (qx * qz + qw * qy) * (-t.tx) +
                        2.0 * (qy * qz - qw * qx) * (-t.ty) +
                        2.0 * (0.5 - qx * qx - qy * qy) * (-t.tz));

    TransformStamp inv;
    inv.parent_frame = t.child_frame;
    inv.child_frame  = t.parent_frame;
    inv.tx = tx; inv.ty = ty; inv.tz = tz;
    inv.qx = qx; inv.qy = qy; inv.qz = qz; inv.qw = qw;
    inv.recv_ns   = t.recv_ns;
    inv.is_static = t.is_static;
    return inv;
}

// static
TransformStamp TfTreePanel::compose(const TransformStamp& a, const TransformStamp& b)
{
    // Compose: result = a * b
    // Translation: a.t + R_a * b.t
    const double ax = a.qx, ay = a.qy, az = a.qz, aw = a.qw;
    const double bt_x = b.tx, bt_y = b.ty, bt_z = b.tz;

    // Rotate b's translation by a's rotation
    const double rtx = 2.0 * (0.5 - ay * ay - az * az) * bt_x +
                       2.0 * (ax * ay - aw * az) * bt_y +
                       2.0 * (ax * az + aw * ay) * bt_z;
    const double rty = 2.0 * (ax * ay + aw * az) * bt_x +
                       2.0 * (0.5 - ax * ax - az * az) * bt_y +
                       2.0 * (ay * az - aw * ax) * bt_z;
    const double rtz = 2.0 * (ax * az - aw * ay) * bt_x +
                       2.0 * (ay * az + aw * ax) * bt_y +
                       2.0 * (0.5 - ax * ax - ay * ay) * bt_z;

    // Quaternion multiplication: a * b
    const double bx = b.qx, by = b.qy, bz = b.qz, bw = b.qw;
    TransformStamp result;
    result.tx = a.tx + rtx;
    result.ty = a.ty + rty;
    result.tz = a.tz + rtz;
    result.qw = aw * bw - ax * bx - ay * by - az * bz;
    result.qx = aw * bx + ax * bw + ay * bz - az * by;
    result.qy = aw * by - ax * bz + ay * bw + az * bx;
    result.qz = aw * bz + ax * by - ay * bx + az * bw;
    result.is_static = a.is_static && b.is_static;
    return result;
}

// static
void TfTreePanel::quat_to_euler_deg(double qx, double qy, double qz, double qw,
                                     double& roll, double& pitch, double& yaw)
{
    constexpr double RAD_TO_DEG = 180.0 / M_PI;

    // ZYX / roll-pitch-yaw convention
    const double sinr_cosp = 2.0 * (qw * qx + qy * qz);
    const double cosr_cosp = 1.0 - 2.0 * (qx * qx + qy * qy);
    roll = std::atan2(sinr_cosp, cosr_cosp) * RAD_TO_DEG;

    const double sinp = 2.0 * (qw * qy - qz * qx);
    if (std::fabs(sinp) >= 1.0)
        pitch = std::copysign(90.0, sinp);
    else
        pitch = std::asin(sinp) * RAD_TO_DEG;

    const double siny_cosp = 2.0 * (qw * qz + qx * qy);
    const double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
    yaw = std::atan2(siny_cosp, cosy_cosp) * RAD_TO_DEG;
}

TransformResult TfTreePanel::lookup_transform(const std::string& source_frame,
                                               const std::string& target_frame) const
{
    TransformResult result;

    if (source_frame == target_frame) {
        result.ok = true;
        result.qw = 1.0;
        return result;
    }

    std::lock_guard<std::mutex> lk(mutex_);

    if (frames_.find(source_frame) == frames_.end()) {
        result.error = "Unknown source frame: " + source_frame;
        return result;
    }
    if (frames_.find(target_frame) == frames_.end()) {
        result.error = "Unknown target frame: " + target_frame;
        return result;
    }

    // Get chain from each frame to root
    const auto source_chain = chain_to_root_unlocked(source_frame);
    const auto target_chain = chain_to_root_unlocked(target_frame);

    // Find LCA (Lowest Common Ancestor)
    std::unordered_map<std::string, size_t> source_idx;
    for (size_t i = 0; i < source_chain.size(); ++i) {
        source_idx[source_chain[i]] = i;
    }

    std::string lca;
    size_t target_lca_idx = 0;
    for (size_t i = 0; i < target_chain.size(); ++i) {
        if (source_idx.find(target_chain[i]) != source_idx.end()) {
            lca = target_chain[i];
            target_lca_idx = i;
            break;
        }
    }

    if (lca.empty()) {
        result.error = "No common ancestor between '" + source_frame +
                       "' and '" + target_frame + "'";
        return result;
    }

    const size_t source_lca_idx = source_idx[lca];

    // Build transform from source → LCA (upward chain, invert each step)
    // T(source → LCA) = T(lca_parent → source)^{-1} * ... composed upward
    // Accumulate: identity, then walk source → lca inverting each edge
    TransformStamp identity;
    identity.qw = 1.0;

    TransformStamp t_source_to_lca = identity;
    for (size_t i = 0; i < source_lca_idx; ++i) {
        const auto& child_frame_id = source_chain[i];
        auto it = frames_.find(child_frame_id);
        if (it == frames_.end()) {
            result.error = "Missing frame data for: " + child_frame_id;
            return result;
        }
        const TransformStamp& edge = it->second.last_transform;
        if (!it->second.ever_received) {
            result.error = "No transform received for: " + child_frame_id;
            return result;
        }
        // edge goes child→parent; invert to get parent→child, accumulate
        const TransformStamp inv_edge = invert(edge);
        t_source_to_lca = compose(inv_edge, t_source_to_lca);
    }

    // Build transform from LCA → target (downward chain)
    // T(LCA → target) = T(LCA → child1) * T(child1 → child2) * ...
    TransformStamp t_lca_to_target = identity;
    for (size_t i = static_cast<size_t>(target_lca_idx); i > 0; --i) {
        const auto& child_frame_id = target_chain[i - 1];
        auto it = frames_.find(child_frame_id);
        if (it == frames_.end()) {
            result.error = "Missing frame data for: " + child_frame_id;
            return result;
        }
        if (!it->second.ever_received) {
            result.error = "No transform received for: " + child_frame_id;
            return result;
        }
        const TransformStamp& edge = it->second.last_transform;
        t_lca_to_target = compose(t_lca_to_target, edge);
    }

    // Final: source → target = source→LCA * LCA→target
    const TransformStamp final_t = compose(t_source_to_lca, t_lca_to_target);

    result.ok = true;
    result.tx = final_t.tx; result.ty = final_t.ty; result.tz = final_t.tz;
    result.qx = final_t.qx; result.qy = final_t.qy;
    result.qz = final_t.qz; result.qw = final_t.qw;
    quat_to_euler_deg(result.qx, result.qy, result.qz, result.qw,
                      result.roll_deg, result.pitch_deg, result.yaw_deg);
    return result;
}

// ---------------------------------------------------------------------------
// Callback
// ---------------------------------------------------------------------------

void TfTreePanel::set_select_callback(FrameSelectCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    select_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

// Helper: draw a single row for a frame
static void draw_frame_row(const TfFrameStats& f, bool selected,
                            const std::string& filter,
                            std::string& out_selected,
                            TfTreePanel::FrameSelectCallback& select_cb)
{
    // Apply filter
    if (!filter.empty() &&
        f.frame_id.find(filter) == std::string::npos &&
        f.parent_frame_id.find(filter) == std::string::npos)
        return;

    // Stale color
    if (f.stale) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.1f, 1.0f));
    }

    // Selectable label
    char label[256];
    std::snprintf(label, sizeof(label), "##row_%s", f.frame_id.c_str());
    bool is_sel = selected;
    if (ImGui::Selectable(label, is_sel,
                          ImGuiSelectableFlags_SpanAllColumns,
                          ImVec2(0.f, 0.f))) {
        out_selected = f.frame_id;
        if (select_cb) select_cb(f.frame_id);
    }
    ImGui::SameLine();

    // Frame name
    if (f.is_static) {
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.f), "%s", f.frame_id.c_str());
    } else {
        ImGui::TextUnformatted(f.frame_id.c_str());
    }

    if (f.stale) ImGui::PopStyleColor();

    // Hz badge
    ImGui::NextColumn();
    if (f.is_static) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.f), "static");
    } else if (f.hz > 0.1) {
        ImGui::Text("%.1f Hz", f.hz);
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "—");
    }

    // Age badge
    ImGui::NextColumn();
    if (!f.ever_received) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "never");
    } else if (f.stale) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.1f, 1.f),
                           "%llu ms !", static_cast<unsigned long long>(f.age_ms));
    } else {
        ImGui::Text("%llu ms", static_cast<unsigned long long>(f.age_ms));
    }

    ImGui::NextColumn();
}

// Recursive tree renderer
static void draw_tree_node(const std::string& frame_id,
                            const TfTreeSnapshot& snap,
                            int depth,
                            const std::string& filter,
                            std::string& out_selected,
                            TfTreePanel::FrameSelectCallback& select_cb,
                            bool show_static, bool show_dynamic)
{
    // Find stats for this frame
    const TfFrameStats* stats_ptr = nullptr;
    for (const auto& f : snap.frames) {
        if (f.frame_id == frame_id) { stats_ptr = &f; break; }
    }

    const bool is_static = stats_ptr ? stats_ptr->is_static : false;
    if (is_static  && !show_static)  return;
    if (!is_static && !show_dynamic) return;

    // Check if this frame passes filter (or any child does — simplified)
    const bool passes_filter = filter.empty() ||
                               frame_id.find(filter) != std::string::npos;

    auto children_it = snap.children.find(frame_id);
    const bool has_children = (children_it != snap.children.end() &&
                                !children_it->second.empty());

    // Indent by depth
    if (depth > 0) ImGui::Indent(12.f * static_cast<float>(depth));

    if (has_children) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (stats_ptr && out_selected == frame_id)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool node_open = ImGui::TreeNodeEx(frame_id.c_str(), flags, "%s",
                                           frame_id.c_str());
        if (ImGui::IsItemClicked()) {
            out_selected = frame_id;
            if (select_cb) select_cb(frame_id);
        }

        // Columns: Hz and Age inline after the name
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.f);
        if (stats_ptr) {
            if (stats_ptr->is_static) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.f), "[S]");
            } else if (stats_ptr->hz > 0.1) {
                ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.f, 1.f),
                                   "  %.0f Hz", stats_ptr->hz);
            }
            if (stats_ptr->stale) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.3f, 0.0f, 1.f), "STALE");
            }
        }

        if (node_open) {
            if (children_it != snap.children.end()) {
                for (const auto& child : children_it->second) {
                    draw_tree_node(child, snap, 0, filter, out_selected,
                                   select_cb, show_static, show_dynamic);
                }
            }
            ImGui::TreePop();
        }
    } else {
        // Leaf node
        ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (out_selected == frame_id)
            leaf_flags |= ImGuiTreeNodeFlags_Selected;

        if (passes_filter || !filter.empty()) {
            ImGui::TreeNodeEx(frame_id.c_str(), leaf_flags, "%s",
                              frame_id.c_str());
            if (ImGui::IsItemClicked()) {
                out_selected = frame_id;
                if (select_cb) select_cb(frame_id);
            }

            if (stats_ptr) {
                ImGui::SameLine();
                if (stats_ptr->is_static) {
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.f), "[S]");
                } else if (stats_ptr->hz > 0.1) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.f, 1.f),
                                       "  %.0f Hz", stats_ptr->hz);
                }
                if (stats_ptr->stale) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.f, 0.3f, 0.0f, 1.f), "STALE");
                }
            }
        }
    }

    if (depth > 0) ImGui::Unindent(12.f * static_cast<float>(depth));
}

void TfTreePanel::draw_inline()
{
    TfTreeSnapshot snap = snapshot();

    // ── Toolbar ───────────────────────────────────────────────────────────────
    ImGui::PushItemWidth(140.f);
    ImGui::InputTextWithHint("##tf_filter", "Filter frames…",
                             filter_buf_, sizeof(filter_buf_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Checkbox("Static", &show_static_);
    ImGui::SameLine();
    ImGui::Checkbox("Dynamic", &show_dynamic_);
    ImGui::SameLine();
    if (ImGui::Button("Clear##tf")) { clear(); }

    // Status bar
    {
        char status[128];
        std::snprintf(status, sizeof(status),
                      "%u frames  |  %u static  |  %u dynamic  |  %u stale",
                      snap.total_frames, snap.static_frames,
                      snap.dynamic_frames, snap.stale_frames);
        if (snap.stale_frames > 0) {
            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.1f, 1.f), "%s", status);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.f), "%s", status);
        }
    }

    ImGui::Separator();

    const std::string filter_str(filter_buf_);

    // Get the select callback (need copy for lambda use — render thread only)
    TfTreePanel::FrameSelectCallback sel_cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        sel_cb = select_cb_;
    }

    // ── Tree view ─────────────────────────────────────────────────────────────
    const float avail_h = ImGui::GetContentRegionAvail().y * 0.55f;
    ImGui::BeginChild("##tf_tree_scroll", ImVec2(0, avail_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (snap.frames.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                           "No TF frames received yet.");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                           "Waiting for /tf or /tf_static...");
    } else {
        // Draw roots; each root recursively draws its subtree
        if (snap.roots.empty()) {
            // All frames have known parents — pick alphabetically first as root
            for (const auto& f : snap.frames) {
                draw_tree_node(f.frame_id, snap, 0, filter_str,
                               selected_frame_, sel_cb,
                               show_static_, show_dynamic_);
            }
        } else {
            for (const auto& root : snap.roots) {
                draw_tree_node(root, snap, 0, filter_str,
                               selected_frame_, sel_cb,
                               show_static_, show_dynamic_);
            }
        }
    }

    ImGui::EndChild();

    // ── Selected frame detail ─────────────────────────────────────────────────
    ImGui::Separator();
    if (!selected_frame_.empty()) {
        ImGui::Text("Selected: %s", selected_frame_.c_str());

        // Find stats for selected frame
        for (const auto& f : snap.frames) {
            if (f.frame_id == selected_frame_) {
                ImGui::Text("  Parent : %s",
                            f.parent_frame_id.empty() ? "(root)" :
                            f.parent_frame_id.c_str());
                ImGui::Text("  Type   : %s", f.is_static ? "static" : "dynamic");
                if (f.ever_received) {
                    ImGui::Text("  Hz     : %.2f", f.hz);
                    ImGui::Text("  Age    : %llu ms", static_cast<unsigned long long>(f.age_ms));
                    const TransformStamp& t = f.last_transform;
                    ImGui::Text("  Trans  : (%.4f, %.4f, %.4f)",
                                t.tx, t.ty, t.tz);
                    ImGui::Text("  Quat   : (%.4f, %.4f, %.4f, %.4f)",
                                t.qx, t.qy, t.qz, t.qw);
                    double roll, pitch, yaw;
                    quat_to_euler_deg(t.qx, t.qy, t.qz, t.qw, roll, pitch, yaw);
                    ImGui::Text("  Euler  : r=%.1f° p=%.1f° y=%.1f°",
                                roll, pitch, yaw);
                    if (f.stale) {
                        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.f, 1.f),
                                           "  *** STALE ***");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                                       "  (no transform received yet)");
                }
                break;
            }
        }

        ImGui::Separator();
    }

    // ── Transform lookup ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Transform Lookup")) {
        ImGui::PushItemWidth(140.f);
        ImGui::InputTextWithHint("##tf_src", "Source frame", &lookup_source_[0],
                                 lookup_source_.capacity() + 1);
        // Use a buffer approach
        static char src_buf[128] = {};
        static char tgt_buf[128] = {};
        ImGui::InputText("Source##lk", src_buf, sizeof(src_buf));
        ImGui::InputText("Target##lk", tgt_buf, sizeof(tgt_buf));
        ImGui::PopItemWidth();
        if (ImGui::Button("Lookup")) {
            lookup_source_ = src_buf;
            lookup_target_ = tgt_buf;
        }
        if (!lookup_source_.empty() && !lookup_target_.empty()) {
            const TransformResult tr =
                lookup_transform(lookup_source_, lookup_target_);
            if (tr.ok) {
                ImGui::TextColored(ImVec4(0.5f, 1.f, 0.5f, 1.f), "OK");
                ImGui::Text("  Trans : (%.4f, %.4f, %.4f)",
                            tr.tx, tr.ty, tr.tz);
                ImGui::Text("  Euler : r=%.1f° p=%.1f° y=%.1f°",
                            tr.roll_deg, tr.pitch_deg, tr.yaw_deg);
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.3f, 0.1f, 1.f),
                                   "Error: %s", tr.error.c_str());
            }
        }
    }
}

void TfTreePanel::draw(bool* p_open)
{
    std::string panel_title;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        panel_title = title_;
    }

    const ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (!ImGui::Begin(panel_title.c_str(), p_open, flags)) {
        ImGui::End();
        return;
    }

    draw_inline();

    ImGui::End();
}

#else // !SPECTRA_USE_IMGUI

void TfTreePanel::draw_inline() {}
void TfTreePanel::draw(bool*) {}

#endif  // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
