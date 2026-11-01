// topic_ingest.hpp — Hand-off from bridge threads to the topic store.
//
// TopicIngestQueue is the TopicSink used by spectra-backend: bridge threads
// push() decoded messages under a mutex, and the daemon's event loop drains
// them once per iteration.  The loop then calls apply_ingest_batches(), which
// declares unseen topics, publishes every field into the TopicRegistry and
// returns the samples each subscribed topic must fan out to its series.  The
// FigureModel and client connections are only ever touched by that loop.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../topic_registry.hpp"
#include "topic_bridge.hpp"

namespace spectra::daemon
{

class TopicIngestQueue final : public TopicSink
{
   public:
    // Batches beyond `max_batches` are dropped (the event loop is stalled or
    // a sender floods the bridge); dropped() counts them.
    explicit TopicIngestQueue(size_t max_batches = 65536) : max_batches_(max_batches) {}

    bool push(TopicIngestBatch&& batch) override;

    // Take every queued batch, oldest first.
    std::vector<TopicIngestBatch> drain();

    size_t   size() const;
    uint64_t dropped() const;

   private:
    mutable std::mutex           mutex_;
    std::deque<TopicIngestBatch> queue_;
    size_t                       max_batches_;
    uint64_t                     dropped_ = 0;
};

// Samples a drained set of batches delivers to one topic's subscribers.
struct TopicFanout
{
    std::string                    name;
    ipc::TopicKind                 kind = ipc::TopicKind::Scalar2D;
    std::vector<double>            samples;   // interleaved, topic stride
    std::vector<TopicSubscription> subs;
};

struct IngestResult
{
    bool topic_list_changed = false;     // a topic was created or came back online
    std::vector<std::string> declared;   // topics created or reclaimed, in order
    uint64_t                 samples_published = 0;
    uint64_t                 samples_rejected  = 0;   // topic owned by another live publisher
    // Topics with subscribers, one entry per topic (samples coalesced in
    // arrival order), in first-seen order.
    std::vector<TopicFanout> fanout;
    // Topic name -> index into fanout.
    std::unordered_map<std::string, size_t> fanout_index;
};

// Declare (on first sight) and publish every field of `batch` as a Scalar2D
// topic owned by batch.owner_id.  Accumulates into `result`.
void apply_ingest_batch(TopicRegistry&          registry,
                        const TopicIngestBatch& batch,
                        IngestResult&           result);

IngestResult apply_ingest_batches(TopicRegistry&                       registry,
                                  const std::vector<TopicIngestBatch>& batches);

}   // namespace spectra::daemon
