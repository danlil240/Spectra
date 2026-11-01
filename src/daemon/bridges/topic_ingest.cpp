#include "topic_ingest.hpp"

namespace spectra::daemon
{

bool TopicIngestQueue::push(TopicIngestBatch&& batch)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (queue_.size() >= max_batches_)
    {
        ++dropped_;
        return false;
    }
    queue_.push_back(std::move(batch));
    return true;
}

std::vector<TopicIngestBatch> TopicIngestQueue::drain()
{
    std::deque<TopicIngestBatch> taken;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        taken.swap(queue_);
    }
    return {std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end())};
}

size_t TopicIngestQueue::size() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.size();
}

uint64_t TopicIngestQueue::dropped() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return dropped_;
}

void apply_ingest_batch(TopicRegistry&          registry,
                        const TopicIngestBatch& batch,
                        IngestResult&           result)
{
    std::vector<double>            sample(2);
    std::vector<TopicSubscription> subs;
    for (const auto& f : batch.fields)
    {
        auto ensured = registry.ensure_declared(f.name,
                                                ipc::TopicKind::Scalar2D,
                                                std::string{},
                                                batch.ring_capacity,
                                                batch.owner_id);
        if (ensured == TopicRegistry::EnsureResult::Conflict)
        {
            ++result.samples_rejected;
            continue;
        }
        if (ensured == TopicRegistry::EnsureResult::Declared)
        {
            result.topic_list_changed = true;
            result.declared.push_back(f.name);
        }

        sample[0]           = f.x;
        sample[1]           = f.y;
        ipc::TopicKind kind = ipc::TopicKind::Scalar2D;
        subs.clear();
        if (!registry.publish(f.name, sample, &kind, &subs, batch.receive_ns))
            continue;
        ++result.samples_published;
        if (subs.empty())
            continue;

        auto [idx_it, inserted] = result.fanout_index.try_emplace(f.name, result.fanout.size());
        if (inserted)
        {
            TopicFanout t;
            t.name = f.name;
            t.kind = kind;
            result.fanout.push_back(std::move(t));
        }
        TopicFanout& t = result.fanout[idx_it->second];
        t.samples.push_back(f.x);
        t.samples.push_back(f.y);
        t.subs = subs;   // latest subscriber set wins
    }
}

IngestResult apply_ingest_batches(TopicRegistry&                       registry,
                                  const std::vector<TopicIngestBatch>& batches)
{
    IngestResult result;
    for (const auto& b : batches)
        apply_ingest_batch(registry, b, result);
    return result;
}

}   // namespace spectra::daemon
