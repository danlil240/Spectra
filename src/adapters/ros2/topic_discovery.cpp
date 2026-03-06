#include "topic_discovery.hpp"

#include <algorithm>

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

std::string normalize_node_namespace(const std::string& ns)
{
    if (ns.empty() || ns == "/")
        return "/";
    if (ns.front() == '/')
        return ns;
    return "/" + ns;
}

std::string make_fully_qualified_node_name(const std::string& ns, const std::string& name)
{
    const std::string normalized_ns = normalize_node_namespace(ns);
    if (name.empty())
        return normalized_ns;
    if (normalized_ns == "/")
        return "/" + name;
    return normalized_ns + "/" + name;
}

template <typename EndpointInfo>
std::vector<std::string> collect_endpoint_nodes(const std::vector<EndpointInfo>& infos)
{
    std::vector<std::string> result;
    result.reserve(infos.size());
    for (const auto& info : infos)
    {
        const std::string full_name =
            make_fully_qualified_node_name(info.node_namespace(), info.node_name());
        if (std::find(result.begin(), result.end(), full_name) == result.end())
            result.push_back(full_name);
    }
    return result;
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

    // Run DDS graph queries on a dedicated background thread instead of
    // the executor's wall-timer callback.  When graph API calls
    // (get_topic_names_and_types, count_publishers, etc.) run inside
    // spin_once(), they create an ABBA deadlock with rmw_fastrtps:
    //
    //   Executor thread: spin_once() holds wait-set state
    //     → timer callback → DDS graph API → needs participant lock
    //   FastDDS discovery thread: holds participant lock
    //     → needs to signal executor via wait-set → BLOCKED
    //
    // A dedicated thread breaks this cycle: it only needs the participant
    // lock (no executor wait-set involvement), so the discovery thread
    // can signal the executor freely and then release the participant
    // lock, unblocking the query thread.  Simple contention, not deadlock.
    refresh_thread_ = std::thread([this]() {
        while (running_.load(std::memory_order_acquire))
        {
            // Startup grace period: skip the first N cycles to let DDS
            // discovery settle after the node joins the domain.
            if (startup_grace_ > 0)
            {
                --startup_grace_;
            }
            // Full-query cooldown: skip ALL DDS graph queries when the
            // topology recently changed, letting rmw_fastrtps finish
            // processing new participants before we hit it with reads.
            else if (query_cooldown_ > 0)
            {
                --query_cooldown_;
            }
            else
            {
                do_refresh(/*full_enrich=*/false);
            }

            // Sleep for interval_, but wake early on stop.
            std::unique_lock<std::mutex> lk(stop_mutex_);
            stop_cv_.wait_for(lk, interval_, [this]() {
                return !running_.load(std::memory_order_acquire);
            });
        }
    });
}

void TopicDiscovery::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

    running_.store(false, std::memory_order_release);

    // Wake the background thread so it exits promptly.
    stop_cv_.notify_one();

    if (refresh_thread_.joinable())
        refresh_thread_.join();
}

void TopicDiscovery::refresh()
{
    do_refresh(/*full_enrich=*/true);
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

void TopicDiscovery::do_refresh(bool full_enrich)
{
    bool expected = false;
    if (!refresh_in_progress_.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel))
        return;

    auto fresh_topics   = query_topics();
    auto fresh_services = query_services();
    auto fresh_nodes    = query_nodes();

    // Collect callbacks to fire, but release the lock first.
    TopicCallback       topic_cb;
    ServiceCallback     service_cb;
    NodeCallback        node_cb;
    RefreshDoneCallback refresh_done_cb;

    std::vector<std::pair<TopicInfo, bool>>   topic_events;
    std::vector<std::pair<ServiceInfo, bool>> service_events;
    std::vector<std::pair<NodeInfo, bool>>    node_events;
    bool prev_was_empty = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Track whether the cache was previously empty (first-time
        // population vs. topology change from a populated cache).
        prev_was_empty = topic_map_.empty() &&
                         service_map_.empty() &&
                         node_map_.empty();

        // Snapshot callbacks
        topic_cb       = topic_cb_;
        service_cb     = service_cb_;
        node_cb        = node_cb_;
        refresh_done_cb = refresh_done_cb_;

        // Diff topics — collect events instead of firing inline
        // (replicate diff logic but store events)
        {
            std::unordered_map<std::string, const TopicInfo*> fresh_map;
            fresh_map.reserve(fresh_topics.size());
            for (const auto& t : fresh_topics)
                fresh_map[t.name] = &t;

            std::vector<std::string> removed_keys;
            for (const auto& [name, info] : topic_map_)
            {
                if (fresh_map.count(name) == 0)
                {
                    topic_events.emplace_back(info, false);
                    removed_keys.push_back(name);
                }
            }
            for (const auto& k : removed_keys)
                topic_map_.erase(k);

            for (const auto& t : fresh_topics)
            {
                auto it = topic_map_.find(t.name);
                if (it == topic_map_.end())
                {
                    topic_map_[t.name] = t;
                    topic_events.emplace_back(t, true);
                }
                else
                {
                    it->second.types = t.types;
                }
            }
        }

        // Diff services — same pattern as topics
        {
            std::unordered_map<std::string, const ServiceInfo*> fresh_map;
            fresh_map.reserve(fresh_services.size());
            for (const auto& s : fresh_services)
                fresh_map[s.name] = &s;

            std::vector<std::string> removed_keys;
            for (const auto& [name, info] : service_map_)
            {
                if (fresh_map.count(name) == 0)
                {
                    service_events.emplace_back(info, false);
                    removed_keys.push_back(name);
                }
            }
            for (const auto& k : removed_keys)
                service_map_.erase(k);

            for (const auto& s : fresh_services)
            {
                if (service_map_.count(s.name) == 0)
                {
                    service_map_[s.name] = s;
                    service_events.emplace_back(s, true);
                }
                else
                {
                    service_map_[s.name] = s;
                }
            }
        }

        // Diff nodes — same pattern
        {
            std::unordered_map<std::string, const NodeInfo*> fresh_map;
            fresh_map.reserve(fresh_nodes.size());
            for (const auto& n : fresh_nodes)
                fresh_map[n.full_name] = &n;

            std::vector<std::string> removed_keys;
            for (const auto& [name, info] : node_map_)
            {
                if (fresh_map.count(name) == 0)
                {
                    node_events.emplace_back(info, false);
                    removed_keys.push_back(name);
                }
            }
            for (const auto& k : removed_keys)
                node_map_.erase(k);

            for (const auto& n : fresh_nodes)
            {
                if (node_map_.count(n.full_name) == 0)
                {
                    node_map_[n.full_name] = n;
                    node_events.emplace_back(n, true);
                }
                else
                {
                    node_map_[n.full_name] = n;
                }
            }
        }

    }
    // Lock released — now fire callbacks safely

    if (topic_cb)
        for (const auto& [info, added] : topic_events)
            topic_cb(info, added);

    if (service_cb)
        for (const auto& [info, added] : service_events)
            service_cb(info, added);

    if (node_cb)
        for (const auto& [info, added] : node_events)
            node_cb(info, added);

    if (refresh_done_cb)
        refresh_done_cb();

    // If the topology changed AND we already had a populated cache,
    // skip ALL DDS queries for several cycles.  This lets rmw_fastrtps's
    // internal DDS discovery protocol finish updating the participant
    // database before we hit it with graph reads.  Without this cooldown,
    // rapid DDS graph reads contend with the FastDDS discovery thread's
    // writer lock, especially when namespaced participants join.
    //
    // We do NOT set cooldowns on the very first population (from empty
    // cache) because that is normal startup, not a discovery burst.
    const bool topology_changed = !topic_events.empty() ||
                                  !service_events.empty() ||
                                  !node_events.empty();
    if (topology_changed && !prev_was_empty)
    {
        // Skip ALL DDS queries (core + enrichment) for several cycles.
        query_cooldown_  = 5;   // skip core queries for 5 cycles (~10 s)
        enrich_cooldown_ = 8;   // skip enrichment for 8 cycles (~16 s)
    }

    if (enrich_cooldown_ > 0 && !full_enrich)
    {
        --enrich_cooldown_;
    }
    else
    {
        if (full_enrich)
            enrich_all();
        else
            enrich_batch();
    }

    refresh_in_progress_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Lazy per-topic enrichment
// ---------------------------------------------------------------------------

void TopicDiscovery::enrich_batch()
{
    // Enrich at most BATCH_SIZE topics per refresh cycle.  This spreads the
    // heavy per-topic DDS calls (count_publishers, count_subscribers,
    // get_publishers_info_by_topic) over many 2-second cycles instead of
    // issuing hundreds of concurrent DDS calls that contend with
    // rmw_fastrtps's internal participant locks.
    //
    // Keep the batch small (2) to minimise sustained DDS read-lock
    // acquisition that can starve the FastDDS discovery writer thread
    // when new participants (especially namespaced ones) are joining.
    constexpr size_t BATCH_SIZE = 2;

    // Snapshot topic names under the lock.
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        names.reserve(topic_map_.size());
        for (const auto& [name, _] : topic_map_)
            names.push_back(name);
    }

    if (names.empty())
        return;

    // Sort for stable iteration order across refresh cycles.
    std::sort(names.begin(), names.end());

    const size_t total = names.size();
    const size_t batch = std::min(BATCH_SIZE, total);
    const size_t start = enrich_index_ % total;

    for (size_t i = 0; i < batch; ++i)
    {
        const size_t idx  = (start + i) % total;
        const auto&  name = names[idx];

        const int pub_count =
            static_cast<int>(node_->count_publishers(name));
        const int sub_count =
            static_cast<int>(node_->count_subscribers(name));

        QosInfo qos;
        auto pub_infos = node_->get_publishers_info_by_topic(name);
        auto sub_infos = node_->get_subscriptions_info_by_topic(name);
        if (!pub_infos.empty())
        {
            const rmw_qos_profile_t& rmw_qos =
                pub_infos.front().qos_profile().get_rmw_qos_profile();
            qos.reliability = qos_reliability_str(rmw_qos.reliability);
            qos.durability  = qos_durability_str(rmw_qos.durability);
            qos.history     = qos_history_str(rmw_qos.history);
            qos.depth       = static_cast<int>(rmw_qos.depth);
        }

        // Write enriched data back under the lock.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = topic_map_.find(name);
            if (it != topic_map_.end())
            {
                it->second.publisher_count  = pub_count;
                it->second.subscriber_count = sub_count;
                it->second.qos              = qos;
                it->second.publisher_nodes  = collect_endpoint_nodes(pub_infos);
                it->second.subscriber_nodes = collect_endpoint_nodes(sub_infos);
            }
        }
    }

    enrich_index_ = (start + batch) % total;
}

void TopicDiscovery::enrich_all()
{
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        names.reserve(topic_map_.size());
        for (const auto& [name, _] : topic_map_)
            names.push_back(name);
    }

    for (const auto& name : names)
    {
        const int pub_count =
            static_cast<int>(node_->count_publishers(name));
        const int sub_count =
            static_cast<int>(node_->count_subscribers(name));

        QosInfo qos;
        auto pub_infos = node_->get_publishers_info_by_topic(name);
        auto sub_infos = node_->get_subscriptions_info_by_topic(name);
        if (!pub_infos.empty())
        {
            const rmw_qos_profile_t& rmw_qos =
                pub_infos.front().qos_profile().get_rmw_qos_profile();
            qos.reliability = qos_reliability_str(rmw_qos.reliability);
            qos.durability  = qos_durability_str(rmw_qos.durability);
            qos.history     = qos_history_str(rmw_qos.history);
            qos.depth       = static_cast<int>(rmw_qos.depth);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = topic_map_.find(name);
            if (it != topic_map_.end())
            {
                it->second.publisher_count  = pub_count;
                it->second.subscriber_count = sub_count;
                it->second.qos              = qos;
                it->second.publisher_nodes  = collect_endpoint_nodes(pub_infos);
                it->second.subscriber_nodes = collect_endpoint_nodes(sub_infos);
            }
        }
    }
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
            // Update types (which come from the lightweight query) but
            // preserve enriched per-topic data (pub/sub counts, QoS) that
            // was filled in by enrich_batch().  The lightweight query_topics()
            // returns 0 for counts; overwriting would discard valid data.
            it->second.types = t.types;
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
    // get_topic_names_and_types() is a single lightweight DDS call.
    // Per-topic calls (count_publishers, count_subscribers,
    // get_publishers_info_by_topic) are deferred to enrich_batch() to avoid
    // sustained DDS graph API access that contends with rmw_fastrtps's
    // internal participant locks (ROS 2 Humble / FastDDS).
    const auto name_types = node_->get_topic_names_and_types();

    std::vector<TopicInfo> result;
    result.reserve(name_types.size());

    for (const auto& [name, types] : name_types)
    {
        TopicInfo info;
        info.name  = name;
        info.types = types;
        // pub/sub counts and QoS left at defaults (0 / empty);
        // enrich_batch() fills them in gradually.
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
        if (slash == std::string::npos)
        {
            // Bare name with no namespace prefix (edge case).
            info.name       = fq;
            info.namespace_ = "/";
        }
        else if (slash == 0)
        {
            // Root namespace: "/node_name" → name = "node_name", ns = "/"
            info.name       = fq.substr(1);
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
