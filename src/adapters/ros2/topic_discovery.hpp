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
#include <functional>
#include <mutex>
#include <string>
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
    std::vector<std::string> types;      // may have multiple type alternatives
    int                      publisher_count{0};
    int                      subscriber_count{0};
    QosInfo                  qos;        // QoS of first publisher (if any)
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
    void set_refresh_interval(std::chrono::milliseconds interval);
    std::chrono::milliseconds refresh_interval() const;

    // ---------- accessors (snapshot of last refresh) ---------------------

    std::vector<TopicInfo>   topics()   const;
    std::vector<ServiceInfo> services() const;
    std::vector<NodeInfo>    nodes()    const;

    // Convenience: true if a topic with the given name is currently known.
    bool has_topic(const std::string& name) const;

    // Returns a copy of the TopicInfo for the given topic name.
    // Returns an empty TopicInfo (name == "") if not found.
    TopicInfo topic(const std::string& name) const;

    // Number of known topics / services / nodes.
    std::size_t topic_count()   const;
    std::size_t service_count() const;
    std::size_t node_count()    const;

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
    // Internal refresh implementation (mutex already NOT held when called).
    void do_refresh();

    // Lazily enrich a small batch of topics with per-topic DDS data
    // (pub/sub counts, QoS).  Called after do_refresh(), outside the mutex.
    void enrich_batch();

    // Diff helpers — compare new snapshot against cache, fire callbacks.
    void diff_topics(const std::vector<TopicInfo>& fresh);
    void diff_services(const std::vector<ServiceInfo>& fresh);
    void diff_nodes(const std::vector<NodeInfo>& fresh);

    // Query helpers.
    std::vector<TopicInfo>   query_topics()   const;
    std::vector<ServiceInfo> query_services() const;
    std::vector<NodeInfo>    query_nodes()    const;

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

    std::chrono::milliseconds interval_{2000};

    // Background refresh thread (replaces the wall timer so that expensive
    // graph queries do not block the SingleThreadedExecutor and starve
    // subscription / service response delivery).
    std::thread              refresh_thread_;
    std::mutex               stop_mutex_;
    std::condition_variable  stop_cv_;

    std::atomic<bool> running_{false};
    std::atomic<bool> refresh_in_progress_{false};
    std::atomic<bool> stop_requested_{false};

    // Rolling index for lazy per-topic enrichment (pub/sub counts, QoS).
    size_t enrich_index_{0};
};

}   // namespace spectra::adapters::ros2
