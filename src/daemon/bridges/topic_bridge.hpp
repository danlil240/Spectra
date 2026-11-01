// topic_bridge.hpp — Ingest for publishers that cannot use spectra::Publisher.
//
// A TopicBridge turns external messages (UDP datagrams today; WebSocket, MQTT,
// serial later) into Spectra topic samples.  Each bridge runs its own thread
// and hands every decoded message to a TopicSink as a TopicIngestBatch: one
// message fans out to many topics by field path ("imu/accel/x"), all sharing
// the message's timestamp.  Topics are declared on first sight.
//
// The sink decides how the samples reach the topic store:
//   - spectra-backend: TopicIngestQueue (topic_ingest.hpp), drained by the
//     daemon's event loop, which owns the FigureModel used for fan-out.
//   - in-process apps: InprocTopicServer, whose registry is mutex-guarded and
//     whose subscriber fan-out is already queued for the main thread.
//
// Bridges never touch figures, series or IPC connections.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::daemon
{

// Client ids at or above this value are reserved for in-process producers
// (bridges, plugin data sources).  Socket clients are numbered from 1 up, so
// the two ranges never collide in TopicRegistry ownership checks.
inline constexpr uint64_t BRIDGE_OWNER_ID_BASE = 1ull << 62;

// One field of one message: a Scalar2D sample (x = message time, y = value).
struct TopicFieldSample
{
    std::string name;   // full topic name, e.g. "udp/imu/accel/x"
    double      x = 0.0;
    double      y = 0.0;
};

// Every field decoded from one external message.
struct TopicIngestBatch
{
    uint64_t                      owner_id      = 0;   // >= BRIDGE_OWNER_ID_BASE
    uint64_t                      receive_ns    = 0;   // steady_clock arrival time (Hz estimate)
    uint32_t                      ring_capacity = 0;   // 0 = registry default
    std::vector<TopicFieldSample> fields;
};

// Destination for decoded messages.  push() must be thread-safe: bridges call
// it from their own threads.
class TopicSink
{
   public:
    virtual ~TopicSink() = default;

    // Returns false if the batch was dropped (sink full or shut down).
    virtual bool push(TopicIngestBatch&& batch) = 0;
};

struct TopicBridgeStats
{
    uint64_t messages_received = 0;   // datagrams / frames read from the transport
    uint64_t messages_parsed   = 0;   // messages that produced at least one sample
    uint64_t parse_errors      = 0;
    uint64_t samples_published = 0;
    uint64_t samples_dropped   = 0;   // sink full or topic limit reached
    uint32_t topics_seen       = 0;
};

class TopicBridge
{
   public:
    virtual ~TopicBridge() = default;

    TopicBridge()                              = default;
    TopicBridge(const TopicBridge&)            = delete;
    TopicBridge& operator=(const TopicBridge&) = delete;

    // Short description, e.g. "udp://127.0.0.1:9870".
    virtual std::string describe() const = 0;

    // Open the transport and start the bridge thread.  Samples are published
    // with `owner_id` as the topic owner.  Returns false (and fills *error)
    // when the transport cannot be opened.  The sink must outlive stop().
    virtual bool start(TopicSink& sink, uint64_t owner_id, std::string* error) = 0;

    // Stop the thread and close the transport.  Idempotent.
    virtual void stop() = 0;

    virtual bool is_running() const = 0;

    virtual TopicBridgeStats stats() const = 0;
};

}   // namespace spectra::daemon
