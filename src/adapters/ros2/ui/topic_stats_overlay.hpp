#pragma once

// TopicStatsOverlay — per-topic statistics panel for the ROS2 adapter.
//
// Displays detailed statistics for a single selected topic:
//   - Hz: avg / min / max (rolling window)
//   - Message count (total and within window)
//   - Byte count with auto-scaled display (B / KB / MB)
//   - Latency stats (avg / min / max) for topics with std_msgs/Header
//   - Drop detection: warns when inter-message gap > 3× expected period
//   - Bandwidth: auto-scaled display (B/s / KB/s / MB/s)
//
// Integrates as a standalone ImGui window or can be embedded inline.
// Wires naturally to TopicListPanel via set_select_callback.
//
// Thread-safety:
//   notify_message() is safe to call from the ROS2 executor thread.
//   draw() must be called from the ImGui render thread only.
//   All shared state is protected by an internal mutex.
//
// Typical usage:
//   TopicStatsOverlay overlay;
//   panel.set_select_callback([&](const std::string& t) {
//       overlay.set_topic(t);
//   });
//   panel.notify_message(topic, bytes);   // also forward to overlay
//   overlay.notify_message(topic, bytes, latency_us);  // from subscriber
//
//   // In ImGui render loop:
//   overlay.draw();

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// MessageSample — one sample used for Hz / latency statistics.
// ---------------------------------------------------------------------------

struct MessageSample
{
    int64_t  arrival_ns{0};    // wall-clock arrival time (nanoseconds)
    size_t   bytes{0};         // serialised message size
    int64_t  latency_us{-1};   // latency in microseconds, -1 = unknown
};

// ---------------------------------------------------------------------------
// TopicDetailStats — rolling statistics for a single topic.
//
// Maintains a sliding window of MessageSamples.  All computation is done in
// compute() which is called from the render thread.
// ---------------------------------------------------------------------------

struct TopicDetailStats
{
    // Rolling sample window.
    std::deque<MessageSample> samples;

    // --- computed values (updated by compute()) ---

    double hz_avg{0.0};      // average Hz within window
    double hz_min{0.0};      // minimum instantaneous Hz
    double hz_max{0.0};      // maximum instantaneous Hz

    double bw_bps{0.0};      // bandwidth in bytes/sec (window average)

    double latency_avg_us{-1.0};  // average latency (µs); -1 = no data
    double latency_min_us{-1.0};
    double latency_max_us{-1.0};

    // Cumulative counters (never reset).
    uint64_t total_messages{0};
    uint64_t total_bytes{0};

    // Drop detection.
    bool     drop_detected{false};   // true if last gap > 3× expected period
    int64_t  last_gap_ns{0};         // last inter-message gap

    // Timestamp of the most recently received message.
    int64_t last_msg_ns{0};

    // Push a new sample (executor thread; caller holds lock).
    void push(int64_t arrival_ns, size_t bytes, int64_t latency_us = -1);

    // Prune entries older than window_ns and recompute all stats.
    // Called from render thread (caller holds lock).
    void compute(int64_t now_ns, int64_t window_ns = 1'000'000'000LL);

    // Reset all statistics (keeps cumulative counters).
    void reset_window();
};

// ---------------------------------------------------------------------------
// TopicStatsOverlay
// ---------------------------------------------------------------------------

class TopicStatsOverlay
{
public:
    TopicStatsOverlay();
    ~TopicStatsOverlay() = default;

    TopicStatsOverlay(const TopicStatsOverlay&)            = delete;
    TopicStatsOverlay& operator=(const TopicStatsOverlay&) = delete;
    TopicStatsOverlay(TopicStatsOverlay&&)                 = delete;
    TopicStatsOverlay& operator=(TopicStatsOverlay&&)      = delete;

    // ---------- wiring -------------------------------------------------------

    // Set the topic to display statistics for.  Thread-safe.
    // Pass an empty string to show the "no topic selected" placeholder.
    void set_topic(const std::string& topic_name);
    const std::string& topic() const;

    // ---------- statistics injection -----------------------------------------

    // Notify that a message arrived on `topic_name` with the given byte count
    // and optional latency (µs, -1 = unknown).
    // Thread-safe — safe to call from the ROS2 executor thread.
    // Only samples for the currently selected topic are recorded.
    void notify_message(const std::string& topic_name,
                        size_t bytes,
                        int64_t latency_us = -1);

    // Reset the rolling window for the current topic (clears history).
    // Cumulative counters are preserved.
    void reset_stats();

    // ---------- ImGui rendering ----------------------------------------------

    // Render the overlay as a standalone ImGui window.
    // `p_open` — if non-null, a close button is shown.
    void draw(bool* p_open = nullptr);

    // Render statistics inline (no Begin/End window wrapper).
    // Caller must have an active ImGui window.
    void draw_inline();

    // ---------- configuration ------------------------------------------------

    // Window title (default: "Topic Statistics").
    void set_title(const std::string& title) { title_ = title; }
    const std::string& title() const { return title_; }

    // Rolling window length in milliseconds (default 1000 ms).
    void set_window_ms(int ms);
    int  window_ms() const { return window_ms_; }

    // Drop threshold: a drop warning is raised when the inter-message gap
    // exceeds `drop_factor` × expected period.  Default: 3.0.
    void set_drop_factor(double factor);
    double drop_factor() const { return drop_factor_; }

    // ---------- testing helpers (no ImGui dependency) ------------------------

    // Snapshot of computed stats for the current topic (render-thread safe
    // when called from the same thread as draw()).
    struct StatsSnapshot
    {
        std::string topic;
        double hz_avg{0.0};
        double hz_min{0.0};
        double hz_max{0.0};
        double bw_bps{0.0};
        double latency_avg_us{-1.0};
        double latency_min_us{-1.0};
        double latency_max_us{-1.0};
        uint64_t total_messages{0};
        uint64_t total_bytes{0};
        bool     drop_detected{false};
        int64_t  last_gap_ns{0};
    };

    // Compute and return a fresh snapshot (acquires lock).
    StatsSnapshot snapshot();

    // Force-compute stats using an explicit now_ns (for testing).
    void compute_now(int64_t now_ns);

    // Direct access to raw stats object (render-thread only, no lock).
    const TopicDetailStats& stats() const { return stats_; }

private:
    // ---- formatting helpers -------------------------------------------------

    // Format bandwidth in B/s, KB/s, or MB/s.
    static std::string format_bw(double bps);

    // Format bytes as B, KB, or MB.
    static std::string format_bytes(uint64_t bytes);

    // Format latency in µs or ms.
    static std::string format_latency(double us);

    // Format Hz with 1 decimal place, "—" if zero.
    static std::string format_hz(double hz);

    // Returns now in nanoseconds (wall clock).
    static int64_t now_ns();

    // ---- draw helpers -------------------------------------------------------

    // Draw one labelled stat row: Label | Value (uses ImGui columns).
    static void draw_stat_row(const char* label, const std::string& value,
                               bool highlight = false);

    // ---- data ---------------------------------------------------------------

    mutable std::mutex mutex_;

    std::string       current_topic_;   // topic we are tracking
    TopicDetailStats  stats_;           // rolling stats for current topic

    std::string title_{"Topic Statistics"};
    int         window_ms_{1000};
    double      drop_factor_{3.0};
};

}   // namespace spectra::adapters::ros2
