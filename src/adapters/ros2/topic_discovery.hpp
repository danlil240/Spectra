#pragma once

// TopicDiscovery — periodic ROS2 graph discovery service.
//
// Queries the ROS2 graph for topics, services, and nodes.  Runs on a
// rclcpp wall-timer (default 2 s) so the caller never polls manually.
// Thread-safe: all public methods may be called from any thread.
//
// Typical usage:
//   TopicDiscovery disc(bridge.node());
//   disc.set_topic_callback([](const TopicInfo& t, bool added) { ... });
//   disc.set_refresh_interval(std::chrono::milliseconds(2000));
//   disc.start();        // arms the periodic timer
//   disc.refresh();      // optional immediate first refresh
//   // ...
//   disc.stop();

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

struct QosInfo
{
    std::string reliability;   // "reliable" | "best_effort" | "unknown"
    std::string durability;    // "transient_local" | "volatile" | "unknown"
    std::string history;       // "keep_last" | "keep_all" | "unknown"
    int         depth{0};
};

struct TopicInfo
{
    std::string              name;
    std::vector<std::string> types;   // may have multiple type alternatives
    int                      publisher_count{0};
    int                      subscriber_count{0};
    int                      local_publisher_count{0};
    int                      local_subscriber_count{0};
    QosInfo                  qos;                // QoS of first publisher (if any)
    std::vector<std::string> publisher_nodes;    // fully-qualified publisher node names
    std::vector<std::string> subscriber_nodes;   // fully-qualified subscriber node names
};

struct ServiceInfo
{
    std::string              name;
    std::vector<std::string> types;
};

struct NodeInfo
{
    std::string name;
    std::string namespace_;
    std::string full_name;   // namespace + "/" + name  (normalised)
};

// ---------------------------------------------------------------------------
// TopicDiscovery
// ---------------------------------------------------------------------------

class TopicDiscovery
{
   public:
    // Construct with a shared node pointer.  The node must outlive this object.
    explicit TopicDiscovery(rclcpp::Node::SharedPtr node);

    ~TopicDiscovery();

    // Non-copyable, non-movable.
    TopicDiscovery(const TopicDiscovery&)            = delete;
    TopicDiscovery& operator=(const TopicDiscovery&) = delete;
    TopicDiscovery(TopicDiscovery&&)                 = delete;
    TopicDiscovery& operator=(TopicDiscovery&&)      = delete;

    // ---------- lifecycle ------------------------------------------------

    // Arm the periodic refresh timer.  Safe to call multiple times.
    void start();

    // Disarm the timer.  Does not clear cached data.
    void stop();

    // Force an immediate synchronous refresh (may be called from any thread).
    // Fires add/remove callbacks for any changes detected.
    void refresh();

    // ---------- configuration --------------------------------------------

    // Set the periodic refresh interval (default 2 s).  Takes effect on the
    // next start() call.
    void                      set_refresh_interval(std::chrono::milliseconds interval);
    std::chrono::milliseconds refresh_interval() const;

    // Set the fully-qualified name of the spectra-ros own node to hide from
    // all panel data (node list, publisher_nodes, subscriber_nodes).
    // Pass an empty string to disable filtering (default).
    void               set_self_node_name(const std::string& fq_name);
    const std::string& self_node_name() const;

    // ---------- accessors (snapshot of last refresh) ---------------------

    std::vector<TopicInfo>   topics() const;
    std::vector<ServiceInfo> services() const;
    std::vector<NodeInfo>    nodes() const;

    // Convenience: true if a topic with the given name is currently known.
    bool has_topic(const std::string& name) const;

    // Returns a copy of the TopicInfo for the given topic name.
    // Returns an empty TopicInfo (name == "") if not found.
    TopicInfo topic(const std::string& name) const;

    // Number of known topics / services / nodes.
    std::size_t topic_count() const;
    std::size_t service_count() const;
    std::size_t node_count() const;

    // ---------- callbacks ------------------------------------------------

    // Called (on the calling thread of refresh()) when a topic is added or
    // removed.  `added` == true for new topics, false for removed ones.
    using TopicCallback = std::function<void(const TopicInfo&, bool added)>;
    void set_topic_callback(TopicCallback cb);

    // Called when a service is added or removed.
    using ServiceCallback = std::function<void(const ServiceInfo&, bool added)>;
    void set_service_callback(ServiceCallback cb);

    // Called when a node is added or removed.
    using NodeCallback = std::function<void(const NodeInfo&, bool added)>;
    void set_node_callback(NodeCallback cb);

    // Called after every completed refresh (whether or not anything changed).
    using RefreshDoneCallback = std::function<void()>;
    void set_refresh_done_callback(RefreshDoneCallback cb);

   private:
    // Returns true if fq_name should be hidden from external consumers.
    bool is_self(const std::string& fq_name) const;

    // Internal refresh implementation (mutex already NOT held when called).
    void do_refresh(bool full_enrich = false);

    // Lazily enrich a small batch of topics with per-topic DDS data
    // (pub/sub counts, QoS).  Called after do_refresh(), outside the mutex.
    void enrich_batch();

    // Enrich ALL topics at once (used by synchronous refresh()).
    void enrich_all();

    // Diff helpers — compare new snapshot against cache, fire callbacks.
    void diff_topics(const std::vector<TopicInfo>& fresh);
    void diff_services(const std::vector<ServiceInfo>& fresh);
    void diff_nodes(const std::vector<NodeInfo>& fresh);

    // Query helpers.
    std::vector<TopicInfo>   query_topics() const;
    std::vector<ServiceInfo> query_services() const;
    std::vector<NodeInfo>    query_nodes() const;

    rclcpp::Node::SharedPtr node_;

    mutable std::mutex mutex_;

    // Cached state from last refresh.
    std::unordered_map<std::string, TopicInfo>   topic_map_;
    std::unordered_map<std::string, ServiceInfo> service_map_;
    std::unordered_map<std::string, NodeInfo>    node_map_;

    // Callbacks (protected by mutex_).
    TopicCallback       topic_cb_;
    ServiceCallback     service_cb_;
    NodeCallback        node_cb_;
    RefreshDoneCallback refresh_done_cb_;

    std::string               self_node_name_;   // own node — hidden from panels
    std::chrono::milliseconds interval_{2000};

    // Dedicated background thread for DDS graph queries.  Running graph
    // API calls (get_topic_names_and_types, count_publishers, etc.) on the
    // executor thread causes an ABBA deadlock with rmw_fastrtps: the
    // executor's spin_once() holds wait-set state while the timer callback
    // calls DDS graph API needing the FastDDS participant lock, while the
    // discovery thread holds the participant lock and needs to signal the
    // executor via the wait-set.  A dedicated thread breaks this cycle
    // because it never holds executor wait-set state.
    std::thread             refresh_thread_;
    std::mutex              stop_mutex_;   // protects stop_cv_ wait
    std::condition_variable stop_cv_;      // wakes thread for early shutdown

    std::atomic<bool> running_{false};
    std::atomic<bool> refresh_in_progress_{false};

    // Rolling index for lazy per-topic enrichment (pub/sub counts, QoS).
    size_t enrich_index_{0};

    // Cooldown counter: when the DDS topology changes (new topics/services/
    // nodes appear), we skip ALL DDS graph queries for a few refresh
    // cycles to let rmw_fastrtps's internal discovery settle.  Without
    // this cooldown, rapid DDS graph reads contend with the FastDDS
    // discovery thread's writer lock on the participant database.
    int enrich_cooldown_{0};

    // Full-query cooldown: when > 0, skip the core graph queries
    // (query_topics, query_services, query_nodes) in addition to
    // enrichment.  Set on topology change to avoid hammering DDS during
    // active discovery bursts (e.g. namespaced node join).
    int query_cooldown_{0};

    // Startup grace period: skip the first N refresh cycles to let DDS
    // discovery settle after the node joins the domain.  Prevents the
    // very first graph query from deadlocking with FastDDS when other
    // participants are already present.
    int startup_grace_{2};
};

}   // namespace spectra::adapters::ros2
