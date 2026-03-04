// NodeGraphPanel — implementation.
//
// Force-directed layout: Fruchterman–Reingold style.
//   - Repulsion between every pair of nodes  (O(n²), acceptable for n ≤ 200)
//   - Spring attraction along edges
//   - Velocity damping each step
//   - Bounded step count per frame (MAX_STEPS_PER_FRAME)
//   - Convergence detected when max velocity < CONVERGENCE_THRESHOLD

#include "ui/node_graph_panel.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#ifdef SPECTRA_USE_IMGUI
#include "imgui.h"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::string last_component(const std::string& s)
{
    auto pos = s.rfind('/');
    if (pos == std::string::npos || pos + 1 >= s.size())
        return s;
    return s.substr(pos + 1);
}

static std::string node_namespace(const std::string& full)
{
    auto pos = full.rfind('/');
    if (pos == std::string::npos || pos == 0)
        return "/";
    return full.substr(0, pos);
}

// Deterministic hash of a string to a hue in [0, 1)
static float string_to_hue(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 16777619u;
    }
    return static_cast<float>(h % 1000) / 1000.0f;
}

// HSV → RGB  (s=0.55, v=0.82 for pastel namespace colors)
static void hsv_to_rgb(float h, float s, float v,
                       float& r, float& g, float& b)
{
    int   i = static_cast<int>(h * 6.0f);
    float f = h * 6.0f - static_cast<float>(i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
}

// ---------------------------------------------------------------------------
// NodeGraphPanel — construction
// ---------------------------------------------------------------------------

NodeGraphPanel::NodeGraphPanel()
{
    last_refresh_time_ = std::chrono::steady_clock::now() -
                         std::chrono::milliseconds(refresh_interval_.count() + 1);
}

// ---------------------------------------------------------------------------
// wiring
// ---------------------------------------------------------------------------

void NodeGraphPanel::set_topic_discovery(TopicDiscovery* disc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    discovery_ = disc;
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void NodeGraphPanel::tick(float dt)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Auto-refresh check
        auto now = std::chrono::steady_clock::now();
        if (discovery_ &&
            (first_refresh_ ||
             now - last_refresh_time_ >= refresh_interval_))
        {
            first_refresh_   = false;
            last_refresh_time_ = now;
            rebuild_from_discovery();
        }
    }

    if (!built_.load())
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!layout_converged_)
        {
            for (int i = 0; i < MAX_STEPS_PER_FRAME; ++i)
            {
                layout_step_unlocked();
                if (layout_converged_)
                    break;
            }
        }
    }
    (void)dt;
}

void NodeGraphPanel::refresh()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (discovery_)
        rebuild_from_discovery();
}

void NodeGraphPanel::reset_layout()
{
    std::lock_guard<std::mutex> lock(mutex_);
    scatter_new_nodes();
    layout_converged_ = false;
    max_velocity_     = 1e9f;
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

void NodeGraphPanel::set_namespace_filter(const std::string& prefix)
{
    std::lock_guard<std::mutex> lock(mutex_);
    namespace_filter_ = prefix;
}

std::string NodeGraphPanel::namespace_filter() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return namespace_filter_;
}

void NodeGraphPanel::set_refresh_interval(std::chrono::milliseconds ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    refresh_interval_ = ms;
}

std::chrono::milliseconds NodeGraphPanel::refresh_interval() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return refresh_interval_;
}

void NodeGraphPanel::set_title(const std::string& title)
{
    std::lock_guard<std::mutex> lock(mutex_);
    title_ = title;
}

std::string NodeGraphPanel::title() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return title_;
}

void NodeGraphPanel::set_repulsion(float k)
{
    std::lock_guard<std::mutex> lock(mutex_);
    repulsion_ = k;
}

float NodeGraphPanel::repulsion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return repulsion_;
}

void NodeGraphPanel::set_attraction(float k)
{
    std::lock_guard<std::mutex> lock(mutex_);
    attraction_ = k;
}

float NodeGraphPanel::attraction() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return attraction_;
}

void NodeGraphPanel::set_damping(float d)
{
    std::lock_guard<std::mutex> lock(mutex_);
    damping_ = d;
}

float NodeGraphPanel::damping() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return damping_;
}

void NodeGraphPanel::set_ideal_length(float l)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ideal_length_ = l;
}

float NodeGraphPanel::ideal_length() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ideal_length_;
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------

std::size_t NodeGraphPanel::node_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

std::size_t NodeGraphPanel::edge_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return edges_.size();
}

GraphSnapshot NodeGraphPanel::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {nodes_, edges_};
}

std::string NodeGraphPanel::selected_id() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_id_;
}

bool NodeGraphPanel::is_built() const
{
    return built_.load();
}

bool NodeGraphPanel::is_animating() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !layout_converged_;
}

// ---------------------------------------------------------------------------
// callbacks
// ---------------------------------------------------------------------------

void NodeGraphPanel::set_select_callback(SelectCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    select_cb_ = std::move(cb);
}

void NodeGraphPanel::set_activate_callback(ActivateCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    activate_cb_ = std::move(cb);
}

void NodeGraphPanel::set_node_filter_callback(NodeFilterCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    node_filter_cb_ = std::move(cb);
}

std::string NodeGraphPanel::selected_ros_node() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Return only if the selected item is a ROS node (not a topic).
    if (selected_id_.empty()) return {};
    auto it = node_index_.find(selected_id_);
    if (it == node_index_.end()) return {};
    const GraphNode& n = nodes_[it->second];
    return (n.kind == GraphNodeKind::RosNode) ? n.id : std::string{};
}

// ---------------------------------------------------------------------------
// graph building
// ---------------------------------------------------------------------------

void NodeGraphPanel::build_graph(const std::vector<TopicInfo>& topics,
                                 const std::vector<NodeInfo>&  nodes)
{
    // Preserve existing positions for nodes that survive the rebuild.
    std::unordered_map<std::string, std::pair<float, float>> old_pos;
    for (const auto& n : nodes_)
        old_pos[n.id] = {n.px, n.py};

    nodes_.clear();
    edges_.clear();

    // Add ROS2 nodes
    for (const auto& ni : nodes)
    {
        GraphNode gn;
        gn.id           = ni.full_name.empty() ? (ni.namespace_ + "/" + ni.name)
                                                : ni.full_name;
        gn.display_name = ni.name;
        gn.namespace_   = ni.namespace_;
        gn.kind         = GraphNodeKind::RosNode;

        auto it = old_pos.find(gn.id);
        if (it != old_pos.end())
        {
            gn.px = it->second.first;
            gn.py = it->second.second;
        }
        nodes_.push_back(std::move(gn));
    }

    // Add topic nodes and edges
    for (const auto& ti : topics)
    {
        GraphNode tn;
        tn.id           = ti.name;
        tn.display_name = last_component(ti.name);
        tn.namespace_   = node_namespace(ti.name);
        tn.kind         = GraphNodeKind::Topic;
        tn.pub_count    = ti.publisher_count;
        tn.sub_count    = ti.subscriber_count;

        auto it = old_pos.find(tn.id);
        if (it != old_pos.end())
        {
            tn.px = it->second.first;
            tn.py = it->second.second;
        }
        nodes_.push_back(std::move(tn));

        // We don't have per-node pub/sub detail from TopicDiscovery directly,
        // so we create edges from the topic's publisher_count / subscriber_count
        // as summaries.  When a full node→topic mapping is available (via
        // get_publishers_info_by_topic), we would add individual edges.
        // For now: one synthetic "publisher" edge per topic with publishers,
        //          one synthetic "subscriber" edge per topic with subscribers.
        // This keeps the graph non-empty and meaningful.
        if (ti.publisher_count > 0)
        {
            GraphEdge e;
            e.from_id    = ti.name + "__pub_summary";
            e.to_id      = ti.name;
            e.is_publish = true;
            // We skip adding the edge if there's no matching node — the
            // summary node is virtual and only used for layout hints.
            // Instead, add a topic→sink marker only when node graph gives us
            // actual publisher node names. (Discovery upgrade in F1-ext.)
        }
    }

    rebuild_index();
    scatter_new_nodes();   // only positions new nodes (old_pos preserved above)

    layout_converged_ = false;
    max_velocity_     = 1e9f;
    built_.store(true);
}

// ---------------------------------------------------------------------------
// Internal: rebuild from discovery
// ---------------------------------------------------------------------------

void NodeGraphPanel::rebuild_from_discovery()
{
    if (!discovery_)
        return;

    // Snapshot without holding our mutex_ (discovery has its own lock)
    auto topics = discovery_->topics();
    auto rnodes = discovery_->nodes();

    build_graph(topics, rnodes);
}

// ---------------------------------------------------------------------------
// Internal: scatter new nodes (those still at 0,0)
// ---------------------------------------------------------------------------

void NodeGraphPanel::scatter_new_nodes()
{
    const float spread = ideal_length_ * static_cast<float>(nodes_.size() + 1) * 0.5f;
    for (auto& n : nodes_)
    {
        if (n.px == 0.0f && n.py == 0.0f)
        {
            // Uniformly scatter in a circle of radius spread
            float angle  = rng_next() * 6.2831853f;
            float radius = rng_next() * spread;
            n.px = std::cos(angle) * radius;
            n.py = std::sin(angle) * radius;
        }
        n.vx = 0.0f;
        n.vy = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Internal: rebuild id→index lookup
// ---------------------------------------------------------------------------

void NodeGraphPanel::rebuild_index()
{
    node_index_.clear();
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        node_index_[nodes_[i].id] = i;
}

// ---------------------------------------------------------------------------
// Internal: passes_filter
// ---------------------------------------------------------------------------

bool NodeGraphPanel::passes_filter(const GraphNode& n) const
{
    if (namespace_filter_.empty())
        return true;
    return n.namespace_.rfind(namespace_filter_, 0) == 0 ||
           n.id.rfind(namespace_filter_, 0) == 0;
}

// ---------------------------------------------------------------------------
// Layout — single step (mutex_ must already be held)
// ---------------------------------------------------------------------------

void NodeGraphPanel::layout_step_unlocked()
{
    const std::size_t N = nodes_.size();
    if (N == 0)
    {
        layout_converged_ = true;
        return;
    }

    const float k2 = repulsion_ * repulsion_;

    // Accumulate forces into (vx, vy) displacements
    std::vector<float> fx(N, 0.0f);
    std::vector<float> fy(N, 0.0f);

    // Repulsion: every pair
    for (std::size_t i = 0; i < N; ++i)
    {
        for (std::size_t j = i + 1; j < N; ++j)
        {
            float dx = nodes_[i].px - nodes_[j].px;
            float dy = nodes_[i].py - nodes_[j].py;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < 1.0f) dist2 = 1.0f;
            float force = k2 / dist2;
            float nx    = dx / std::sqrt(dist2);
            float ny    = dy / std::sqrt(dist2);
            fx[i] += force * nx;
            fy[i] += force * ny;
            fx[j] -= force * nx;
            fy[j] -= force * ny;
        }
    }

    // Attraction: along edges
    for (const auto& e : edges_)
    {
        auto ia = node_index_.find(e.from_id);
        auto ib = node_index_.find(e.to_id);
        if (ia == node_index_.end() || ib == node_index_.end())
            continue;

        std::size_t i = ia->second;
        std::size_t j = ib->second;

        float dx   = nodes_[j].px - nodes_[i].px;
        float dy   = nodes_[j].py - nodes_[i].py;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) dist = 1.0f;
        float force = attraction_ * (dist - ideal_length_);
        float nx    = dx / dist;
        float ny    = dy / dist;
        fx[i] += force * nx;
        fy[i] += force * ny;
        fx[j] -= force * nx;
        fy[j] -= force * ny;
    }

    // Update velocities + positions
    float max_v = 0.0f;
    for (std::size_t i = 0; i < N; ++i)
    {
        nodes_[i].vx = (nodes_[i].vx + fx[i]) * damping_;
        nodes_[i].vy = (nodes_[i].vy + fy[i]) * damping_;
        nodes_[i].px += nodes_[i].vx;
        nodes_[i].py += nodes_[i].vy;
        float v = std::sqrt(nodes_[i].vx * nodes_[i].vx +
                             nodes_[i].vy * nodes_[i].vy);
        if (v > max_v) max_v = v;
    }

    max_velocity_     = max_v;
    layout_converged_ = max_v < CONVERGENCE_THRESHOLD;
}

void NodeGraphPanel::layout_steps(int n)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < n; ++i)
        layout_step_unlocked();
}

void NodeGraphPanel::layout_step()
{
    std::lock_guard<std::mutex> lock(mutex_);
    layout_step_unlocked();
}

// ---------------------------------------------------------------------------
// RNG (xorshift32, not cryptographic)
// ---------------------------------------------------------------------------

float NodeGraphPanel::rng_next()
{
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return static_cast<float>(rng_state_ & 0x7FFFFFFFu) /
           static_cast<float>(0x7FFFFFFFu);
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void NodeGraphPanel::draw(bool* p_open)
{
    std::string win_title;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        win_title = title_ + "###NodeGraphPanel";
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(win_title.c_str(), p_open, flags))
    {
        ImGui::End();
        return;
    }

    draw_toolbar();
    draw_graph_canvas();

    ImGui::End();
}

void NodeGraphPanel::draw_toolbar()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Namespace filter input
    ImGui::SetNextItemWidth(180.0f);
    char buf[256];
    std::strncpy(buf, namespace_filter_.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputTextWithHint("##ns_filter", "Namespace filter…", buf, sizeof(buf)))
        namespace_filter_ = buf;

    ImGui::SameLine();
    if (ImGui::Button("Re-layout"))
    {
        scatter_new_nodes();
        layout_converged_ = false;
        max_velocity_     = 1e9f;
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        if (discovery_)
            rebuild_from_discovery();
    }

    ImGui::SameLine();
    std::size_t vis_nodes = 0;
    std::size_t vis_edges = 0;
    for (const auto& n : nodes_)
        if (passes_filter(n)) ++vis_nodes;
    for (const auto& e : edges_)
    {
        auto ia = node_index_.find(e.from_id);
        auto ib = node_index_.find(e.to_id);
        if (ia != node_index_.end() && ib != node_index_.end() &&
            passes_filter(nodes_[ia->second]) &&
            passes_filter(nodes_[ib->second]))
            ++vis_edges;
    }

    char stats[64];
    std::snprintf(stats, sizeof(stats), "%zu nodes  %zu edges",
                  vis_nodes, vis_edges);
    ImGui::TextDisabled("%s", stats);

    if (!layout_converged_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "(simulating…)");
    }
}

void NodeGraphPanel::draw_graph_canvas()
{
    ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 50 || canvas_size.y < 50)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(canvas_pos,
                      ImVec2(canvas_pos.x + canvas_size.x,
                             canvas_pos.y + canvas_size.y),
                      IM_COL32(22, 27, 34, 255));
    dl->AddRect(canvas_pos,
                ImVec2(canvas_pos.x + canvas_size.x,
                       canvas_pos.y + canvas_size.y),
                IM_COL32(48, 54, 61, 255));

    // Invisible button for mouse interaction (pan + zoom)
    ImGui::SetCursorScreenPos(canvas_pos);
    ImGui::InvisibleButton("##canvas",
                           ImVec2(canvas_size.x, canvas_size.y),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight);

    // Pan via left-drag on canvas background
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        view_ox_ += delta.x;
        view_oy_ += delta.y;
    }

    // Zoom via scroll wheel
    if (ImGui::IsItemHovered())
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            float factor = (wheel > 0) ? 1.1f : (1.0f / 1.1f);
            // Zoom toward mouse position
            ImVec2 mouse = ImGui::GetIO().MousePos;
            float mx = (mouse.x - canvas_pos.x - view_ox_) / view_scale_;
            float my = (mouse.y - canvas_pos.y - view_oy_) / view_scale_;
            view_scale_ = std::clamp(view_scale_ * factor, MIN_SCALE, MAX_SCALE);
            view_ox_    = mouse.x - canvas_pos.x - mx * view_scale_;
            view_oy_    = mouse.y - canvas_pos.y - my * view_scale_;
        }
    }

    // Clip drawing to canvas
    dl->PushClipRect(canvas_pos,
                     ImVec2(canvas_pos.x + canvas_size.x,
                            canvas_pos.y + canvas_size.y),
                     true);

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Draw edges first (below nodes)
        for (const auto& e : edges_)
            draw_edge(e, canvas_pos.x + view_ox_, canvas_pos.y + view_oy_, view_scale_);

        // Draw nodes
        for (auto& n : nodes_)
        {
            if (!passes_filter(n))
                continue;
            draw_node(n, canvas_pos.x + view_ox_, canvas_pos.y + view_oy_, view_scale_);
        }

        // Detail popup for selected node
        if (!selected_id_.empty())
        {
            auto it = node_index_.find(selected_id_);
            if (it != node_index_.end())
                draw_detail_popup(nodes_[it->second]);
        }
    }

    dl->PopClipRect();

    // Hit-test for clicks AFTER drawing (so we read the correct node positions)
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float  wx    = (mouse.x - canvas_pos.x - view_ox_) / view_scale_;
        float  wy    = (mouse.y - canvas_pos.y - view_oy_) / view_scale_;

        std::lock_guard<std::mutex> lock(mutex_);
        std::string hit;
        float       best_dist2 = 50.0f * 50.0f;  // max click radius
        for (const auto& n : nodes_)
        {
            if (!passes_filter(n)) continue;
            float dx    = wx - n.px;
            float dy    = wy - n.py;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < best_dist2)
            {
                best_dist2 = dist2;
                hit        = n.id;
            }
        }

        if (hit != selected_id_)
        {
            // Deselect previous
            if (!selected_id_.empty())
            {
                auto it = node_index_.find(selected_id_);
                if (it != node_index_.end())
                    nodes_[it->second].selected = false;
            }
            selected_id_ = hit;
            if (!hit.empty())
            {
                auto it = node_index_.find(hit);
                if (it != node_index_.end())
                {
                    const GraphNode& gn = nodes_[it->second];
                    nodes_[it->second].selected = true;
                    if (select_cb_)
                        select_cb_(gn);
                    // Fire the topic-filter callback only for ROS nodes.
                    if (node_filter_cb_ && gn.kind == GraphNodeKind::RosNode)
                        node_filter_cb_(gn.id);
                }
            }
        }
        else if (!hit.empty() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            auto it = node_index_.find(hit);
            if (it != node_index_.end() && activate_cb_)
                activate_cb_(nodes_[it->second]);
        }
    }
}

void NodeGraphPanel::draw_node(const GraphNode& n,
                                float ox, float oy, float scale) const
{
    if (!passes_filter(n))
        return;

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    float       cx   = ox + n.px * scale;
    float       cy   = oy + n.py * scale;

    if (n.kind == GraphNodeKind::RosNode)
    {
        // Box: 80×30 px at scale=1
        float hw = 40.0f * scale;
        float hh = 15.0f * scale;

        float hue = string_to_hue(n.namespace_);
        float r, g, b;
        hsv_to_rgb(hue, 0.45f, 0.60f, r, g, b);
        ImU32 fill  = IM_COL32(static_cast<int>(r * 255),
                               static_cast<int>(g * 255),
                               static_cast<int>(b * 255), 200);
        ImU32 border = n.selected
                       ? IM_COL32(255, 200, 80, 255)
                       : IM_COL32(120, 140, 160, 200);

        dl->AddRectFilled(ImVec2(cx - hw, cy - hh),
                          ImVec2(cx + hw, cy + hh),
                          fill, 4.0f * scale);
        dl->AddRect(ImVec2(cx - hw, cy - hh),
                    ImVec2(cx + hw, cy + hh),
                    border, 4.0f * scale, 0, n.selected ? 2.0f : 1.0f);

        // Label
        if (scale > 0.3f)
        {
            ImVec2 text_size = ImGui::CalcTextSize(n.display_name.c_str());
            dl->AddText(ImVec2(cx - text_size.x * 0.5f, cy - text_size.y * 0.5f),
                        IM_COL32(220, 230, 240, 255),
                        n.display_name.c_str());
        }
    }
    else
    {
        // Ellipse for topics (approx with circle + rect)
        float rx = 30.0f * scale;
        float ry = 12.0f * scale;

        ImU32 fill  = IM_COL32(50, 70, 90, 180);
        ImU32 border = n.selected
                       ? IM_COL32(255, 200, 80, 255)
                       : IM_COL32(100, 160, 200, 180);

        // Approximate ellipse with 20-segment polygon
        dl->AddEllipseFilled(ImVec2(cx, cy), ImVec2(rx, ry), fill);
        dl->AddEllipse(ImVec2(cx, cy), ImVec2(rx, ry), border,
                       0.0f, 20, n.selected ? 2.0f : 1.0f);

        if (scale > 0.35f)
        {
            ImVec2 text_size = ImGui::CalcTextSize(n.display_name.c_str());
            dl->AddText(ImVec2(cx - text_size.x * 0.5f, cy - text_size.y * 0.5f),
                        IM_COL32(160, 210, 240, 255),
                        n.display_name.c_str());
        }
    }
}

void NodeGraphPanel::draw_edge(const GraphEdge& e,
                                float ox, float oy, float scale) const
{
    auto ia = node_index_.find(e.from_id);
    auto ib = node_index_.find(e.to_id);
    if (ia == node_index_.end() || ib == node_index_.end())
        return;

    const GraphNode& A = nodes_[ia->second];
    const GraphNode& B = nodes_[ib->second];

    if (!passes_filter(A) || !passes_filter(B))
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float ax = ox + A.px * scale;
    float ay = oy + A.py * scale;
    float bx = ox + B.px * scale;
    float by = oy + B.py * scale;

    ImU32 col = e.is_publish
                ? IM_COL32(100, 200, 120, 160)   // publish = green
                : IM_COL32(200, 120, 100, 160);  // subscribe = red-orange

    dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), col, 1.2f);

    // Arrowhead at B
    float dx   = bx - ax;
    float dy   = by - ay;
    float len  = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) return;
    dx /= len;
    dy /= len;

    float arrow_len  = 10.0f * scale;
    float arrow_half = 4.5f  * scale;

    float tip_x = bx - dx * 3.0f * scale;  // slight inset
    float tip_y = by - dy * 3.0f * scale;

    float base_x = tip_x - dx * arrow_len;
    float base_y = tip_y - dy * arrow_len;

    dl->AddTriangleFilled(
        ImVec2(tip_x, tip_y),
        ImVec2(base_x - dy * arrow_half, base_y + dx * arrow_half),
        ImVec2(base_x + dy * arrow_half, base_y - dx * arrow_half),
        col);
}

void NodeGraphPanel::draw_detail_popup(const GraphNode& n)
{
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);

    std::string popup_id = "##detail_" + n.id;
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().MousePos.x + 12,
               ImGui::GetIO().MousePos.y + 12),
        ImGuiCond_Always);

    if (ImGui::BeginTooltip())
    {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1.0f),
                           "%s",
                           n.kind == GraphNodeKind::RosNode ? "ROS2 Node"
                                                            : "Topic");
        ImGui::Separator();
        ImGui::TextWrapped("%s", n.id.c_str());
        ImGui::Spacing();

        if (n.kind == GraphNodeKind::Topic)
        {
            ImGui::Text("Publishers:   %d", n.pub_count);
            ImGui::Text("Subscribers:  %d", n.sub_count);
        }
        else
        {
            ImGui::Text("Namespace: %s", n.namespace_.c_str());
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Click to select  •  Dbl-click to open");
        ImGui::EndTooltip();
    }
}

#else  // !SPECTRA_USE_IMGUI

void NodeGraphPanel::draw(bool*) {}

#endif  // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
