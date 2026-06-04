#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ipc/message.hpp"

namespace spectra::daemon
{

// A subscriber routes incoming topic samples to a specific (figure, axes, series).
struct TopicSubscription
{
    uint64_t figure_id    = 0;
    uint32_t axes_index   = 0;
    uint32_t series_index = 0;

    bool operator==(const TopicSubscription& o) const noexcept
    {
        return figure_id == o.figure_id && axes_index == o.axes_index
               && series_index == o.series_index;
    }
};

// Named, type-erased data stream. Survives client disconnects: publisher may
// reconnect later and resume pushing to the same topic name.
class TopicRegistry
{
   public:
    struct Info
    {
        std::string    name;
        ipc::TopicKind kind = ipc::TopicKind::Scalar2D;
        std::string    unit;
        double         estimated_hz     = 0.0;
        uint64_t       total_samples    = 0;
        uint64_t       last_publish_ns  = 0;
        uint32_t       subscriber_count = 0;
        bool           publisher_online = false;
    };

    enum class DeclareResult
    {
        Created,
        ReclaimedByOwner,   // existing topic was offline and now reclaimed
        Conflict,           // already owned by a different live publisher
    };

    // ─── Publisher API ────────────────────────────────────────────────────

    DeclareResult declare(const std::string& name,
                          ipc::TopicKind     kind,
                          const std::string& unit,
                          uint32_t           ring_capacity,
                          uint64_t           owner_client_id);

    // Push samples. Returns subscribers to fan out to (copied under the lock).
    // 'samples' uses the same interleaved layout as the IPC payload (2 doubles
    // per Scalar2D sample, 3 doubles per Scalar3D sample). 'kind_out' yields
    // the topic kind so the caller can convert to the figure_model float layout.
    // Returns false if the topic is unknown.
    bool publish(const std::string&              name,
                 const std::vector<double>&      samples,
                 ipc::TopicKind*                 kind_out,
                 std::vector<TopicSubscription>* subs_out);

    // Forget all topics owned by this client (mark offline). Pending
    // subscribers remain so the topic re-attaches on next declare().
    void on_client_disconnect(uint64_t client_id);

    // ─── Subscriber API ───────────────────────────────────────────────────

    bool subscribe(const std::string& name, const TopicSubscription& sub);
    bool subscribe(const std::string&       name,
                   const TopicSubscription& sub,
                   ipc::TopicKind*          kind_out,
                   std::vector<double>*     retained_samples_out);
    void unsubscribe(const TopicSubscription& sub);

    // ─── Discovery ────────────────────────────────────────────────────────

    std::vector<Info> snapshot() const;

    bool                          exists(const std::string& name) const;
    std::optional<ipc::TopicKind> kind_of(const std::string& name) const;

   private:
    struct Topic
    {
        ipc::TopicKind kind = ipc::TopicKind::Scalar2D;
        std::string    unit;
        uint64_t       owner_client_id  = 0;
        bool           publisher_online = false;
        uint32_t       ring_capacity    = 4096;
        // Last N samples (interleaved doubles). Used to prime late subscribers
        // immediately when a topic is dragged onto a plot.
        std::deque<double>             ring;
        uint64_t                       total_samples   = 0;
        uint64_t                       last_publish_ns = 0;
        double                         ewma_dt_ns      = 0.0;
        std::vector<TopicSubscription> subs;
    };

    mutable std::mutex                     mutex_;
    std::unordered_map<std::string, Topic> topics_;
};

}   // namespace spectra::daemon
