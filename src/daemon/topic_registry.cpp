#include "topic_registry.hpp"

#include <algorithm>

namespace spectra::daemon
{

namespace
{
uint64_t now_ns()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
}   // namespace

TopicRegistry::DeclareResult TopicRegistry::declare(const std::string& name,
                                                    ipc::TopicKind     kind,
                                                    const std::string& unit,
                                                    uint32_t           ring_capacity,
                                                    uint64_t           owner_client_id)
{
    std::lock_guard<std::mutex> lk(mutex_);

    auto it = topics_.find(name);
    if (it == topics_.end())
    {
        Topic t;
        t.kind             = kind;
        t.unit             = unit;
        t.owner_client_id  = owner_client_id;
        t.publisher_online = true;
        t.ring_capacity    = ring_capacity == 0 ? 4096u : ring_capacity;
        topics_.emplace(name, std::move(t));
        return DeclareResult::Created;
    }

    // Existing topic
    if (it->second.publisher_online && it->second.owner_client_id != owner_client_id)
    {
        return DeclareResult::Conflict;
    }

    // Reclaim or replace: keep accumulated subs, refresh metadata.
    it->second.kind             = kind;
    it->second.unit             = unit;
    it->second.owner_client_id  = owner_client_id;
    it->second.publisher_online = true;
    if (ring_capacity != 0)
        it->second.ring_capacity = ring_capacity;
    return DeclareResult::ReclaimedByOwner;
}

bool TopicRegistry::publish(const std::string&              name,
                            const std::vector<double>&      samples,
                            ipc::TopicKind*                 kind_out,
                            std::vector<TopicSubscription>* subs_out)
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto                        it = topics_.find(name);
    if (it == topics_.end())
        return false;

    Topic&   t   = it->second;
    uint64_t now = now_ns();

    // Hz estimator: EWMA over inter-arrival times (per call, not per sample).
    if (t.last_publish_ns != 0)
    {
        double dt = static_cast<double>(now - t.last_publish_ns);
        if (t.ewma_dt_ns == 0.0)
            t.ewma_dt_ns = dt;
        else
            t.ewma_dt_ns = 0.9 * t.ewma_dt_ns + 0.1 * dt;
    }
    t.last_publish_ns = now;

    // Append to ring (bounded).
    t.ring.insert(t.ring.end(), samples.begin(), samples.end());
    size_t stride = (t.kind == ipc::TopicKind::Scalar3D) ? 3u : 2u;
    size_t cap    = static_cast<size_t>(t.ring_capacity) * stride;
    while (t.ring.size() > cap)
        t.ring.pop_front();

    size_t per_sample = stride;
    t.total_samples += samples.size() / per_sample;

    if (kind_out)
        *kind_out = t.kind;
    if (subs_out)
        *subs_out = t.subs;
    return true;
}

void TopicRegistry::on_client_disconnect(uint64_t client_id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& kv : topics_)
    {
        if (kv.second.owner_client_id == client_id && kv.second.publisher_online)
            kv.second.publisher_online = false;
    }
}

bool TopicRegistry::subscribe(const std::string& name, const TopicSubscription& sub)
{
    return subscribe(name, sub, nullptr, nullptr);
}

bool TopicRegistry::subscribe(const std::string&       name,
                              const TopicSubscription& sub,
                              ipc::TopicKind*          kind_out,
                              std::vector<double>*     retained_samples_out)
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto                        it = topics_.find(name);
    if (it == topics_.end())
        return false;
    Topic& t    = it->second;
    auto&  subs = t.subs;
    if (std::find(subs.begin(), subs.end(), sub) == subs.end())
        subs.push_back(sub);
    if (kind_out)
        *kind_out = t.kind;
    if (retained_samples_out)
        retained_samples_out->assign(t.ring.begin(), t.ring.end());
    return true;
}

void TopicRegistry::unsubscribe(const TopicSubscription& sub)
{
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& kv : topics_)
    {
        auto& subs = kv.second.subs;
        subs.erase(std::remove(subs.begin(), subs.end(), sub), subs.end());
    }
}

std::vector<TopicRegistry::Info> TopicRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Info>           out;
    out.reserve(topics_.size());
    for (const auto& kv : topics_)
    {
        Info i;
        i.name             = kv.first;
        i.kind             = kv.second.kind;
        i.unit             = kv.second.unit;
        i.estimated_hz     = kv.second.ewma_dt_ns > 0.0 ? 1.0e9 / kv.second.ewma_dt_ns : 0.0;
        i.total_samples    = kv.second.total_samples;
        i.last_publish_ns  = kv.second.last_publish_ns;
        i.subscriber_count = static_cast<uint32_t>(kv.second.subs.size());
        i.publisher_online = kv.second.publisher_online;
        out.push_back(std::move(i));
    }
    return out;
}

bool TopicRegistry::exists(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return topics_.find(name) != topics_.end();
}

std::optional<ipc::TopicKind> TopicRegistry::kind_of(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto                        it = topics_.find(name);
    if (it == topics_.end())
        return std::nullopt;
    return it->second.kind;
}

}   // namespace spectra::daemon
