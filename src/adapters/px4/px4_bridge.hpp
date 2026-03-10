#pragma once

// Px4Bridge — real-time telemetry connection for Spectra PX4 adapter.
//
// Connects to a PX4 autopilot via UDP (MAVLink) for real-time data inspection.
// Runs a background receive thread that accumulates telemetry into ring buffers.
// The render thread reads via thread-safe accessors.
//
// Typical usage:
//   Px4Bridge bridge;
//   bridge.init("127.0.0.1", 14540);   // SITL default
//   bridge.start();                      // launches bg receive thread
//   // ... per frame: auto data = bridge.latest("vehicle_attitude");
//   bridge.shutdown();
//
// Thread-safety:
//   The receive thread writes ring buffers; the render thread reads snapshots.
//   Protected by a shared mutex for each topic channel.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// BridgeState
// ---------------------------------------------------------------------------

enum class BridgeState
{
    Disconnected,
    Connecting,
    Connected,
    Receiving,   // actively receiving data
    ShuttingDown,
    Stopped,
};

const char* bridge_state_name(BridgeState s);

// ---------------------------------------------------------------------------
// TelemetryField — one named numeric value from a MAVLink message.
// ---------------------------------------------------------------------------

struct TelemetryField
{
    std::string name;
    double      value{0.0};
};

// ---------------------------------------------------------------------------
// TelemetryMessage — one decoded MAVLink message with timestamp.
// ---------------------------------------------------------------------------

struct TelemetryMessage
{
    uint32_t                    msg_id{0};
    std::string                 name;   // e.g. "ATTITUDE", "GPS_RAW_INT"
    uint64_t                    timestamp_us{0};
    std::vector<TelemetryField> fields;
};

// ---------------------------------------------------------------------------
// TelemetryChannel — ring buffer for one message type.
// ---------------------------------------------------------------------------

struct TelemetryChannel
{
    std::string                   name;
    std::vector<TelemetryMessage> buffer;   // circular buffer
    size_t                        write_pos{0};
    size_t                        capacity{1000};
    size_t                        count{0};
    mutable std::mutex            mutex;

    void                          push(TelemetryMessage msg);
    std::vector<TelemetryMessage> snapshot() const;
    TelemetryMessage              latest() const;
};

// ---------------------------------------------------------------------------
// Px4Bridge
// ---------------------------------------------------------------------------

class Px4Bridge
{
   public:
    Px4Bridge();
    ~Px4Bridge();

    // Non-copyable, non-movable.
    Px4Bridge(const Px4Bridge&)            = delete;
    Px4Bridge& operator=(const Px4Bridge&) = delete;
    Px4Bridge(Px4Bridge&&)                 = delete;
    Px4Bridge& operator=(Px4Bridge&&)      = delete;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    // Configure the UDP endpoint for MAVLink communication.
    // Default port 14540 = PX4 SITL; 14550 = QGroundControl.
    bool init(const std::string& host = "127.0.0.1", uint16_t port = 14540);

    // Start the background receive thread.
    bool start();

    // Stop the receive thread and close the socket.
    void shutdown();

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    BridgeState state() const { return state_.load(std::memory_order_acquire); }
    bool        is_receiving() const { return state() == BridgeState::Receiving; }
    bool        is_connected() const
    {
        auto s = state();
        return s == BridgeState::Connected || s == BridgeState::Receiving;
    }

    const std::string& host() const { return host_; }
    uint16_t           port() const { return port_; }

    // ------------------------------------------------------------------
    // Data access (render thread)
    // ------------------------------------------------------------------

    // List all active telemetry channels (message types seen so far).
    std::vector<std::string> channel_names() const;

    // Get a snapshot of all messages for a channel.
    std::vector<TelemetryMessage> channel_snapshot(const std::string& name) const;

    // Get the most recent message for a channel.
    std::optional<TelemetryMessage> channel_latest(const std::string& name) const;

    // Total messages received since start.
    uint64_t total_messages() const { return total_msg_count_.load(std::memory_order_relaxed); }

    // Messages per second (computed from last 1-second window).
    double message_rate() const { return msg_rate_.load(std::memory_order_relaxed); }

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    using StateCallback = std::function<void(BridgeState)>;
    void set_state_callback(StateCallback cb) { state_cb_ = std::move(cb); }

    // ------------------------------------------------------------------
    // Error handling
    // ------------------------------------------------------------------

    const std::string& last_error() const noexcept { return last_error_; }

   private:
    void receive_thread_func();
    void set_state(BridgeState s);

    // Decode a raw MAVLink v1/v2 frame into a TelemetryMessage.
    bool decode_mavlink_message(const uint8_t* data, size_t len, TelemetryMessage& out);

    // Get or create a channel for a message name.
    TelemetryChannel& get_channel(const std::string& name);

    std::string host_{"127.0.0.1"};
    uint16_t    port_{14540};
    int         socket_fd_{-1};

    std::thread              recv_thread_;
    std::atomic<BridgeState> state_{BridgeState::Disconnected};
    std::atomic<bool>        stop_requested_{false};

    mutable std::mutex                                                 channels_mutex_;
    std::unordered_map<std::string, std::unique_ptr<TelemetryChannel>> channels_;

    std::atomic<uint64_t> total_msg_count_{0};
    std::atomic<double>   msg_rate_{0.0};

    StateCallback state_cb_;
    std::string   last_error_;
};

}   // namespace spectra::adapters::px4
