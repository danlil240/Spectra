#include "topic_discovery.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace
{

std::string qos_reliability_str(rmw_qos_reliability_policy_t p)
{
    switch (p)
    {
    case RMW_QOS_POLICY_RELIABILITY_RELIABLE:   return "reliable";
    case RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT: return "best_effort";
    default:                                     return "unknown";
    }
}

std::string qos_durability_str(rmw_qos_durability_policy_t p)
{
    switch (p)
    {
    case RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL: return "transient_local";
    case RMW_QOS_POLICY_DURABILITY_VOLATILE:        return "volatile";
    default:                                         return "unknown";
    }
}

std::string qos_history_str(rmw_qos_history_policy_t p)
{
    switch (p)
    {
    case RMW_QOS_POLICY_HISTORY_KEEP_LAST: return "keep_last";
    case RMW_QOS_POLICY_HISTORY_KEEP_ALL:  return "keep_all";
    default:                                return "unknown";
    }
}

}   // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TopicDiscovery::TopicDiscovery(rclcpp::Node::SharedPtr node)
    : node_(std::move(node))
{
}

TopicDiscovery::~TopicDiscovery()
{
    stop();
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void TopicDiscovery::start()
{
    if (running_.load(std::memory_order_acquire))
        return;

    running_.store(true, std::memory_order_release);
    stop_requested_.store(false, std::memory_order_release);

    // Run graph queries on a dedicated thread so the executor stays free for
    // subscriptions and service responses.
    refresh_thread_ = std::thread([this]() {
        while (!stop_requested_.load(std::memory_order_acquire))
        {
            do_refresh();

            // Sleep for the configured interval, but wake early if stop is
            // requested.
            std::unique_lock<std::mutex> lk(stop_mutex_);
            std::chrono::milliseconds iv;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                iv = interval_;
            }
            stop_cv_.wait_for(lk, iv, [this] {
                return stop_requested_.load(std::memory_order_acquire);
            });
        }
    });
}

void TopicDiscovery::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

    running_.store(false, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
    stop_cv_.notify_all();

    if (refresh_thread_.joinable())
        refresh_thread_.join();
}

void TopicDiscovery::refresh()
{
    do_refresh();
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

void TopicDiscovery::set_refresh_interval(std::chrono::milliseconds interval)
{
    std::lock_guard<std::mutex> lock(mutex_);
    interval_ = interval;
}

std::chrono::milliseconds TopicDiscovery::refresh_interval() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return interval_;
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------

std::vector<TopicInfo> TopicDiscovery::topics() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TopicInfo> result;
    result.reserve(topic_map_.size());
    for (const auto& [_, v] : topic_map_)
        result.push_back(v);
    return result;
}

std::vector<ServiceInfo> TopicDiscovery::services() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ServiceInfo> result;
    result.reserve(service_map_.size());
    for (const auto& [_, v] : service_map_)
        result.push_back(v);
    return result;
}

std::vector<NodeInfo> TopicDiscovery::nodes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeInfo> result;
    result.reserve(node_map_.size());
    for (const auto& [_, v] : node_map_)
        result.push_back(v);
    return result;
}

bool TopicDiscovery::has_topic(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return topic_map_.count(name) > 0;
}

TopicInfo TopicDiscovery::topic(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topic_map_.find(name);
    if (it == topic_map_.end())
        return {};
    return it->second;
}

std::size_t TopicDiscovery::topic_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return topic_map_.size();
}

std::size_t TopicDiscovery::service_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return service_map_.size();
}

std::size_t TopicDiscovery::node_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return node_map_.size();
}

// ---------------------------------------------------------------------------
// callbacks
// ---------------------------------------------------------------------------

void TopicDiscovery::set_topic_callback(TopicCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    topic_cb_ = std::move(cb);
}

void TopicDiscovery::set_service_callback(ServiceCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    service_cb_ = std::move(cb);
}

void TopicDiscovery::set_node_callback(NodeCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    node_cb_ = std::move(cb);
}

void TopicDiscovery::set_refresh_done_callback(RefreshDoneCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    refresh_done_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// internal refresh
// ---------------------------------------------------------------------------

void TopicDiscovery::do_refresh()
{
    // Guard against re-entrant refresh (timer fires while manual refresh runs).
    bool expected = false;
    if (!refresh_in_progress_.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel))
        return;

    // Query the graph outside the mutex to avoid holding it during potentially
    // slow ROS2 graph calls.
    auto fresh_topics   = query_topics();
    auto fresh_services = query_services();
    auto fresh_nodes    = query_nodes();

    // Now diff and fire callbacks under the mutex.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        diff_topics(fresh_topics);
        diff_services(fresh_services);
        diff_nodes(fresh_nodes);

        if (refresh_done_cb_)
            refresh_done_cb_();
    }

    refresh_in_progress_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// diff helpers
// ---------------------------------------------------------------------------

void TopicDiscovery::diff_topics(const std::vector<TopicInfo>& fresh)
{
    // Build a set of new topic names.
    std::unordered_map<std::string, const TopicInfo*> fresh_map;
    fresh_map.reserve(fresh.size());
    for (const auto& t : fresh)
        fresh_map[t.name] = &t;

    // Fire removed callbacks for topics that have disappeared.
    std::vector<std::string> removed_keys;
    for (const auto& [name, info] : topic_map_)
    {
        if (fresh_map.count(name) == 0)
        {
            if (topic_cb_)
                topic_cb_(info, false);
            removed_keys.push_back(name);
        }
    }
    for (const auto& k : removed_keys)
        topic_map_.erase(k);

    // Fire added callbacks for new topics; update counts for existing ones.
    for (const auto& t : fresh)
    {
        auto it = topic_map_.find(t.name);
        if (it == topic_map_.end())
        {
            topic_map_[t.name] = t;
            if (topic_cb_)
                topic_cb_(t, true);
        }
        else
        {
            // Update in-place (pub/sub counts, QoS may have changed).
            it->second = t;
        }
    }
}

void TopicDiscovery::diff_services(const std::vector<ServiceInfo>& fresh)
{
    std::unordered_map<std::string, const ServiceInfo*> fresh_map;
    fresh_map.reserve(fresh.size());
    for (const auto& s : fresh)
        fresh_map[s.name] = &s;

    std::vector<std::string> removed_keys;
    for (const auto& [name, info] : service_map_)
    {
        if (fresh_map.count(name) == 0)
        {
            if (service_cb_)
                service_cb_(info, false);
            removed_keys.push_back(name);
        }
    }
    for (const auto& k : removed_keys)
        service_map_.erase(k);

    for (const auto& s : fresh)
    {
        if (service_map_.count(s.name) == 0)
        {
            service_map_[s.name] = s;
            if (service_cb_)
                service_cb_(s, true);
        }
        else
        {
            service_map_[s.name] = s;
        }
    }
}

void TopicDiscovery::diff_nodes(const std::vector<NodeInfo>& fresh)
{
    std::unordered_map<std::string, const NodeInfo*> fresh_map;
    fresh_map.reserve(fresh.size());
    for (const auto& n : fresh)
        fresh_map[n.full_name] = &n;

    std::vector<std::string> removed_keys;
    for (const auto& [name, info] : node_map_)
    {
        if (fresh_map.count(name) == 0)
        {
            if (node_cb_)
                node_cb_(info, false);
            removed_keys.push_back(name);
        }
    }
    for (const auto& k : removed_keys)
        node_map_.erase(k);

    for (const auto& n : fresh)
    {
        if (node_map_.count(n.full_name) == 0)
        {
            node_map_[n.full_name] = n;
            if (node_cb_)
                node_cb_(n, true);
        }
        else
        {
            node_map_[n.full_name] = n;
        }
    }
}

// ---------------------------------------------------------------------------
// graph query helpers
// ---------------------------------------------------------------------------

std::vector<TopicInfo> TopicDiscovery::query_topics() const
{
    // get_topic_names_and_types() returns map<name, vector<type_string>>.
    const auto name_types = node_->get_topic_names_and_types();

    std::vector<TopicInfo> result;
    result.reserve(name_types.size());

    for (const auto& [name, types] : name_types)
    {
        TopicInfo info;
        info.name  = name;
        info.types = types;

        // Publisher and subscriber counts.
        info.publisher_count  =
            static_cast<int>(node_->count_publishers(name));
        info.subscriber_count =
            static_cast<int>(node_->count_subscribers(name));

        // QoS: query publisher endpoint info for the first publisher.
        auto pub_infos = node_->get_publishers_info_by_topic(name);
        if (!pub_infos.empty())
        {
            // qos_profile() returns rclcpp::QoS; get_rmw_qos_profile() gives
            // the underlying rmw_qos_profile_t with plain enum fields.
            const rmw_qos_profile_t& rmw_qos =
                pub_infos.front().qos_profile().get_rmw_qos_profile();
            info.qos.reliability = qos_reliability_str(rmw_qos.reliability);
            info.qos.durability  = qos_durability_str(rmw_qos.durability);
            info.qos.history     = qos_history_str(rmw_qos.history);
            info.qos.depth       = static_cast<int>(rmw_qos.depth);
        }

        result.push_back(std::move(info));
    }

    return result;
}

std::vector<ServiceInfo> TopicDiscovery::query_services() const
{
    const auto name_types = node_->get_service_names_and_types();

    std::vector<ServiceInfo> result;
    result.reserve(name_types.size());

    for (const auto& [name, types] : name_types)
    {
        ServiceInfo info;
        info.name  = name;
        info.types = types;
        result.push_back(std::move(info));
    }

    return result;
}

std::vector<NodeInfo> TopicDiscovery::query_nodes() const
{
    // get_node_names() returns fully-qualified node names ("/namespace/name")
    // since ROS2 Dashing.  We split on the last '/' to recover name and
    // namespace separately.
    const auto fq_names = node_->get_node_names();

    std::vector<NodeInfo> result;
    result.reserve(fq_names.size());

    for (const auto& fq : fq_names)
    {
        NodeInfo info;
        info.full_name = fq;

        const auto slash = fq.rfind('/');
        if (slash == std::string::npos || slash == 0)
        {
            // Bare name with no namespace prefix (edge case).
            info.name       = fq;
            info.namespace_ = "/";
        }
        else
        {
            info.name       = fq.substr(slash + 1);
            info.namespace_ = fq.substr(0, slash);
            if (info.namespace_.empty())
                info.namespace_ = "/";
        }

        result.push_back(std::move(info));
    }

    return result;
}

}   // namespace spectra::adapters::ros2
