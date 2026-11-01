// udp_json_bridge.hpp — UDP datagrams (JSON or CSV) → Spectra topics.
//
// Binds a UDP port (127.0.0.1:9870 by default) and decodes every datagram
// with decode_text_datagram(): each numeric leaf becomes a Scalar2D topic
// "<prefix>/<field path>", x = the message's "t" / "timestamp" field or the
// receive time in seconds since the bridge started.
//
// Security: the bridge is unauthenticated.  It only listens on loopback unless
// a non-loopback bind address is configured explicitly, and caps the number of
// distinct topics it creates (max_topics) so a sender cannot exhaust memory by
// spraying new field names.  See SECURITY.md.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>

#include "topic_bridge.hpp"

namespace spectra::daemon
{

inline constexpr uint16_t    DEFAULT_UDP_BRIDGE_PORT = 9870;
inline constexpr const char* DEFAULT_UDP_BRIDGE_BIND = "127.0.0.1";

struct UdpBridgeConfig
{
    std::string bind_address  = DEFAULT_UDP_BRIDGE_BIND;   // IPv4 literal or "localhost"
    uint16_t    port          = DEFAULT_UDP_BRIDGE_PORT;   // 0 = ephemeral (tests)
    std::string prefix        = "udp";                     // topic name prefix
    uint32_t    max_topics    = 1024;                      // distinct topics this bridge may create
    uint32_t    ring_capacity = 0;                         // per-topic ring; 0 = registry default
};

// Parse "[host:]port", e.g. "9870", "127.0.0.1:9870", "0.0.0.0:9870".
// Leaves cfg untouched and returns false (filling *error) on bad input.
bool parse_udp_bridge_spec(std::string_view spec, UdpBridgeConfig& cfg, std::string* error);

// True for 127.0.0.0/8 and "localhost".
bool is_loopback_address(std::string_view address);

class UdpJsonBridge final : public TopicBridge
{
   public:
    explicit UdpJsonBridge(UdpBridgeConfig config);
    ~UdpJsonBridge() override;

    std::string      describe() const override;
    bool             start(TopicSink& sink, uint64_t owner_id, std::string* error) override;
    void             stop() override;
    bool             is_running() const override;
    TopicBridgeStats stats() const override;

    // Port actually bound (differs from config().port when it was 0).
    uint16_t               bound_port() const { return bound_port_.load(); }
    const UdpBridgeConfig& config() const { return config_; }

    // Decode one datagram and push the result to the sink.  Called by the
    // bridge thread; public so tests can drive the decoder without a socket.
    void handle_datagram(std::string_view data, uint64_t receive_ns);

   private:
    void run();

    UdpBridgeConfig       config_;
    int                   fd_ = -1;
    std::thread           thread_;
    std::atomic<bool>     running_{false};
    std::atomic<uint16_t> bound_port_{0};
    TopicSink*            sink_     = nullptr;
    uint64_t              owner_id_ = 0;

    std::chrono::steady_clock::time_point start_time_{};
    std::unordered_set<std::string>       topics_;   // bridge thread only

    mutable std::mutex stats_mutex_;
    TopicBridgeStats   stats_;
};

}   // namespace spectra::daemon
