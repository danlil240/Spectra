#pragma once

// NodeGraphPanel — ROS2 node graph visualization panel.
//
// Queries the ROS2 graph via TopicDiscovery and renders an interactive
// node graph: ROS2 nodes are drawn as rounded rectangles, topics as
// ellipses, and publisher/subscriber edges as directed arrows.
//
// Layout engine: force-directed (Fruchterman–Reingold style), run
// incrementally each frame until convergence, capped at MAX_STEPS_PER_FRAME
// steps to keep frame time bounded.
//
// Features:
//   - Nodes as boxes (colored by namespace); topics as ellipses (gray)
//   - Directed edges: node→topic (publish), topic→node (subscribe)
//   - Force-directed layout with pan + zoom (mouse wheel / drag)
//   - Namespace filter (hide nodes/topics outside selected namespace)
//   - Click on node or topic → detail popover (name, type, pub/sub list)
//   - Auto-refresh: polls TopicDiscovery on a configurable interval
//   - "Re-layout" button forces a full layout reset
//
// Thread-safety:
//   refresh() / set_namespace_filter() may be called from any thread.
//   draw() must be called from the ImGui render thread only.
//   Internal graph state is protected by mutex_.
//
// Non-ImGui build:
//   All draw*() methods compile to no-ops when SPECTRA_USE_IMGUI is not
//   defined.  All graph-building and layout logic remains available for
//   testing.
//
// Typical usage:
//   NodeGraphPanel panel;
//   panel.set_topic_discovery(&discovery);
//   panel.set_refresh_interval(std::chrono::milliseconds(3000));
//   // In render loop:
//   panel.tick(dt_seconds);   // advances layout simulation
//   panel.draw();             // renders ImGui window

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "topic_discovery.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Graph data types (public, usable without ImGui)
// ---------------------------------------------------------------------------

enum class GraphNodeKind : uint8_t
{
    RosNode,   // a ROS2 node (box)
    Topic,     // a ROS2 topic (ellipse)
};

struct GraphNode
{
    std::string   id;             // unique key: full node/topic name
    std::string   display_name;   // short name (last component after '/')
    std::string   namespace_;     // leading namespace
    GraphNodeKind kind{GraphNodeKind::RosNode};

    // Layout position (2-D, arbitrary units — scaled at render time)
    float px{0.0f};
    float py{0.0f};
    // Velocity accumulator for force-directed step
    float vx{0.0f};
    float vy{0.0f};

    // For topics: publisher_count, subscriber_count
    int pub_count{0};
    int sub_count{0};

    bool selected{false};
};

struct GraphEdge
{
    std::string from_id;   // publisher node id  OR  topic id (for sub edges)
    std::string to_id;     // topic id  OR  subscriber node id
    // true = node publishes to topic; false = topic delivers to node
    bool is_publish{true};
};

struct GraphSnapshot
{
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
};

// ---------------------------------------------------------------------------
// NodeGraphPanel
// ---------------------------------------------------------------------------

class NodeGraphPanel
{
   public:
    NodeGraphPanel();
    ~NodeGraphPanel() = default;

    // Non-copyable, non-movable.
    NodeGraphPanel(const NodeGraphPanel&)            = delete;
    NodeGraphPanel& operator=(const NodeGraphPanel&) = delete;
    NodeGraphPanel(NodeGraphPanel&&)                 = delete;
    NodeGraphPanel& operator=(NodeGraphPanel&&)      = delete;

    // ---------- wiring ---------------------------------------------------

    // Wire to a TopicDiscovery instance (must outlive this panel).
    void set_topic_discovery(TopicDiscovery* disc);

    // ---------- lifecycle ------------------------------------------------

    // Advance the layout simulation by dt seconds.  Call once per frame
    // from the render thread before draw().  No-op until graph is built.
    void tick(float dt);

    // Force an immediate graph rebuild from TopicDiscovery.
    // Thread-safe — may be called from any thread.
    void refresh();

    // Reset node positions and re-run initial layout from scratch.
    void reset_layout();

    // ---------- configuration --------------------------------------------

    // Namespace filter: only show nodes/topics whose namespace starts with
    // this prefix.  Empty string = show all.  Thread-safe.
    void        set_namespace_filter(const std::string& prefix);
    std::string namespace_filter() const;

    // Auto-refresh interval (default 3 s).  Set before wiring.
    void                      set_refresh_interval(std::chrono::milliseconds ms);
    std::chrono::milliseconds refresh_interval() const;

    // Window title shown in ImGui title bar.
    void        set_title(const std::string& title);
    std::string title() const;

    // Force-directed layout parameters.
    void  set_repulsion(float k);   // repulsion constant (default 150)
    float repulsion() const;
    void  set_attraction(float k);   // spring constant  (default 0.06)
    float attraction() const;
    void  set_damping(float d);   // velocity damping per tick (default 0.85)
    float damping() const;
    void  set_ideal_length(float l);   // ideal edge length (default 200)
    float ideal_length() const;

    // ---------- accessors (for testing, thread-safe) ---------------------

    // Current node/edge count after last rebuild.
    std::size_t node_count() const;
    std::size_t edge_count() const;

    // Returns a snapshot of the current graph (nodes + edges).
    GraphSnapshot snapshot() const;

    // Returns the selected node/topic id ("" if none selected).
    std::string selected_id() const;

    // true once the graph has been built at least once.
    bool is_built() const;

    // true while layout simulation has not yet converged.
    bool is_animating() const;

    // ---------- ImGui rendering ------------------------------------------

    // Draw the panel as a dockable ImGui window.
    // p_open: if non-null, a close button is shown.
    void draw(bool* p_open = nullptr);

    // ---------- callbacks ------------------------------------------------

    // Called when a node or topic is selected (single click).
    using SelectCallback = std::function<void(const GraphNode&)>;
    void set_select_callback(SelectCallback cb);

    // Called when user double-clicks a node (e.g. to open topic monitor).
    using ActivateCallback = std::function<void(const GraphNode&)>;
    void set_activate_callback(ActivateCallback cb);

    // Called when user clicks a ROS node — provides the node's fully-qualified name
    // so the caller can filter another panel (e.g. TopicListPanel) to that node's topics.
    // Signature: void(const std::string& node_name)
    using NodeFilterCallback = std::function<void(const std::string& node_name)>;
    void set_node_filter_callback(NodeFilterCallback cb);

    // Returns the name of the currently selected graph node (empty = none).
    // Filtered topic list can use this on each refresh.
    std::string selected_ros_node() const;

    // ---------- graph building helpers (public for testing) --------------

    // Build the internal graph from a topics + nodes snapshot.
    // Existing node positions are preserved for nodes still present.
    void build_graph(const std::vector<TopicInfo>& topics, const std::vector<NodeInfo>& nodes);

    // Run N steps of the force-directed layout.
    void layout_steps(int n);

    // Single layout step (exposed for unit tests — acquires lock).
    void layout_step();

   private:
    // ---------- internal graph state (all protected by mutex_) ----------

    void rebuild_from_discovery();
    void scatter_new_nodes();   // random initial position for new nodes

    // Apply filter: returns true if a node/topic should be visible.
    bool passes_filter(const GraphNode& n) const;

    // ImGui helpers (only compiled with SPECTRA_USE_IMGUI)
    void draw_graph_canvas();
    void draw_node(const GraphNode& n, float ox, float oy, float scale) const;
    void draw_edge(const GraphEdge& e, float ox, float oy, float scale) const;
    void draw_detail_popup(const GraphNode& n);
    void draw_toolbar();

    // Stable id → index lookup rebuilt after each graph change.
    void rebuild_index();
    // Lockless layout step — mutex_ must already be held by caller.
    void layout_step_unlocked();

    // ---------- data ----------------------------------------------------

    mutable std::mutex mutex_;

    TopicDiscovery* discovery_{nullptr};

    std::vector<GraphNode> nodes_;
    std::vector<GraphEdge> edges_;

    // id → index in nodes_ (rebuilt in rebuild_index())
    std::unordered_map<std::string, std::size_t> node_index_;

    std::string namespace_filter_;
    std::string title_{"Node Graph"};
    std::string selected_id_;

    // Force-directed parameters
    float repulsion_{150.0f};
    float attraction_{0.06f};
    float damping_{0.85f};
    float ideal_length_{200.0f};

    // Layout convergence tracking
    float             max_velocity_{0.0f};
    bool              layout_converged_{false};
    std::atomic<bool> built_{false};

    // Auto-refresh
    std::chrono::milliseconds             refresh_interval_{3000};
    std::chrono::steady_clock::time_point last_refresh_time_{};
    bool                                  first_refresh_{true};

    // View state (pan + zoom, render thread only — no lock needed)
    float view_ox_{0.0f};
    float view_oy_{0.0f};
    float view_scale_{1.0f};
    bool  dragging_canvas_{false};

    // Callbacks (protected by mutex_)
    SelectCallback     select_cb_;
    ActivateCallback   activate_cb_;
    NodeFilterCallback node_filter_cb_;

    // Random seed for initial scatter (not cryptographic)
    uint32_t rng_state_{12345};
    float    rng_next();   // simple xorshift → [0,1)

    static constexpr int   MAX_STEPS_PER_FRAME   = 6;
    static constexpr float CONVERGENCE_THRESHOLD = 0.5f;
    static constexpr float MIN_SCALE             = 0.05f;
    static constexpr float MAX_SCALE             = 5.0f;
};

}   // namespace spectra::adapters::ros2
