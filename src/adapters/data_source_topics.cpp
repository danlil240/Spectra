#include "adapters/data_source_topics.hpp"

#include <chrono>
#include <cmath>

#include "adapters/data_source_registry.hpp"
#include "daemon/bridges/field_path.hpp"

namespace spectra
{

std::string data_source_topic_name(std::string_view source, std::string_view label)
{
    return daemon::join_topic_path(source, label);
}

size_t pump_data_sources_to_topics(DataSourceRegistry& registry,
                                   daemon::TopicSink&  sink,
                                   uint64_t            owner_id)
{
    auto batches = registry.poll_all_by_source();
    if (batches.empty())
        return 0;

    const auto receive_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());

    size_t pushed = 0;
    for (const auto& b : batches)
    {
        daemon::TopicIngestBatch batch;
        batch.owner_id   = owner_id;
        batch.receive_ns = receive_ns;
        batch.fields.reserve(b.points.size());
        for (const auto& p : b.points)
        {
            if (!std::isfinite(p.timestamp) || !std::isfinite(p.value))
                continue;
            std::string name = data_source_topic_name(b.source, p.series_label);
            if (name.empty())
                continue;
            batch.fields.push_back(daemon::TopicFieldSample{std::move(name),
                                                            p.timestamp,
                                                            static_cast<double>(p.value)});
        }
        const size_t n = batch.fields.size();
        if (n != 0 && sink.push(std::move(batch)))
            pushed += n;
    }
    return pushed;
}

}   // namespace spectra
