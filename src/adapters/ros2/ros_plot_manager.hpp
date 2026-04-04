#pragma once

// RosPlotManager — bridge between ROS2 field subscriptions and Spectra Figure/LineSeries.
//
// Manages a collection of active plots, each binding a (topic, field_path) pair to a
// Spectra LineSeries inside a Figure.  Each frame the caller invokes poll() to drain the
// SPSC ring buffers and append new (timestamp, value) points to the series.
//
// Typical usage:
//   Ros2Bridge bridge;
//   bridge.init("spectra_ros", argc, argv);
//   bridge.start_spin();
//
//   MessageIntrospector intr;
//   RosPlotManager mgr(bridge, intr);
//
//   auto handle = mgr.add_plot("/chatter_float", "data");
//   // handle.figure → spectra::Figure&
//   // handle.series → spectra::LineSeries&
//
//   // In render loop:
//   mgr.poll();    // drains ring buffers, appends to series
//
// Thread-safety:
//   - add_plot() / remove_plot() must NOT be called from the executor thread.
//     They acquire an internal mutex and are safe to call from the render thread.
//   - poll() must be called from a single consumer thread (render thread).
//     It does NOT hold the internal mutex during hot-path ring-buffer draining.
//   - The underlying GenericSubscriber ring buffers are SPSC: executor = producer,
//     render thread (poll) = consumer.
//
// Auto-fit behaviour:
//   Y-axis is auto-fitted once after the first AUTO_FIT_SAMPLES samples are
//   received on a given plot.  After that the user can zoom/pan freely.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <spectra/chunked_series.hpp>

#include "generic_subscriber.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// DirectWriteContext — shared state for direct-write callbacks.
//
// Captures the time origin (set on first sample) and a sample counter.
// Thread-safe: origin is set exactly once via std::call_once; counter is atomic.
// ---------------------------------------------------------------------------

struct DirectWriteContext
{
    std::once_flag        origin_once;
    double                origin{0.0};
    std::atomic<uint64_t> samples_written{0};
};

// ---------------------------------------------------------------------------
// PlotHandle — returned by add_plot(); allows the caller to access the
// underlying Figure and LineSeries directly.
// ---------------------------------------------------------------------------

struct PlotHandle
{
    // Unique identifier (>= 1).  Used for remove_plot().
    int id{-1};

    // The topic + field that this plot tracks.
    std::string topic;
    std::string field_path;

    // Pointers into RosPlotManager-owned storage; valid until remove_plot(id).
    // The Figure and Series are owned by the PlotEntry, not the caller.
    spectra::Figure* figure{nullptr};
    spectra::Axes*   axes{nullptr};

    // Always valid — points to the underlying Series (LineSeries or ChunkedLineSeries).
    spectra::Series* base_series{nullptr};

    // Non-null only when the plot uses LineSeries (use_chunked == false).
    spectra::LineSeries* series{nullptr};

    // Non-null only when the plot uses ChunkedLineSeries (use_chunked == true).
    spectra::ChunkedLineSeries* chunked_series{nullptr};

    bool valid() const { return id >= 1 && figure != nullptr && base_series != nullptr; }
};

class TopicDiscovery;

// ---------------------------------------------------------------------------
// RosPlotManager — main class.
// ---------------------------------------------------------------------------

class RosPlotManager
{
   public:
    // Default auto-scroll time window (seconds).
    static constexpr double DEFAULT_SCROLL_WINDOW_S = 30.0;
    static constexpr double MIN_WINDOW_S            = 1.0;
    static constexpr double MAX_WINDOW_S            = 3600.0;
    static constexpr double DEFAULT_PRUNE_BUFFER_S  = 20.0;

    // Number of samples after which the first Y auto-fit is applied.
    static constexpr size_t AUTO_FIT_SAMPLES = 100;

    // Maximum samples drained from a ring buffer per poll() call per plot.
    // Prevents a burst from blocking the render thread for too long.
    static constexpr size_t MAX_DRAIN_PER_POLL = 4096;

    // Construct.
    // bridge  — must outlive this object and be in Spinning state before add_plot() is called.
    // intr    — must outlive this object.
    RosPlotManager(Ros2Bridge& bridge, MessageIntrospector& intr);

    ~RosPlotManager();

    // Non-copyable, non-movable.
    RosPlotManager(const RosPlotManager&)            = delete;
    RosPlotManager& operator=(const RosPlotManager&) = delete;
    RosPlotManager(RosPlotManager&&)                 = delete;
    RosPlotManager& operator=(RosPlotManager&&)      = delete;

    // ---------- plot management ------------------------------------------

    // Subscribe to (topic, field_path) and create a new Figure + LineSeries.
    // type_name  — ROS2 message type, e.g. "std_msgs/msg/Float64".
    //              If empty, attempts auto-discovery via the bridge node's topic graph.
    // buffer_depth — ring buffer capacity forwarded to GenericSubscriber.
    // Returns a PlotHandle with id >= 1 on success; id == -1 on failure
    // (e.g. field not found, type unknown, bridge not spinning).
    PlotHandle add_plot(const std::string& topic,
                        const std::string& field_path,
                        const std::string& type_name    = "",
                        size_t             buffer_depth = 10000);

    // Remove a plot by its handle id.  Destroys the subscriber + Figure.
    // Returns false if the id is not found.
    bool remove_plot(int id);

    // Remove all plots.
    void clear();

    // Number of active plots.
    size_t plot_count() const;

    // Retrieve a handle by id.  Returns invalid handle if not found.
    PlotHandle handle(int id) const;

    // All active handles (snapshot; safe to iterate after the call returns).
    std::vector<PlotHandle> handles() const;

    // ---------- frame update (render thread) -----------------------------

    // Drain ring buffers and append new points to the Spectra series.
    // Call once per frame from the render thread.
    // No heap allocations in the hot path once draining has started.
    void poll();

    // ---------- configuration --------------------------------------------

    // Override the figure size used when creating new plots.
    void set_figure_size(uint32_t w, uint32_t h);

    // Override the buffer_depth used for each new add_plot() call.
    // (per-plot override can be passed directly to add_plot()).
    void set_default_buffer_depth(size_t depth);

    // Override how many samples to collect before the first Y auto-fit.
    // Default: AUTO_FIT_SAMPLES (100).
    void set_auto_fit_samples(size_t n);

    // Set the TopicDiscovery instance for safe (non-blocking) type resolution.
    void set_topic_discovery(TopicDiscovery* disc) { discovery_ = disc; }

    // Configure pruning of samples older than the current visible left edge.
    void   set_prune_buffer(double seconds);
    double prune_buffer() const;
    void   set_pruning_enabled(bool enabled);
    bool   pruning_enabled() const;

    // ---------- chunked series (LT-8) ------------------------------------

    // Enable chunked series mode for new plots.  When enabled, new plots
    // use ChunkedLineSeries instead of LineSeries — storing data in fixed-size
    // chunks for efficient streaming and optional level-of-detail rendering.
    // Existing plots are not affected.  Default: false.
    void set_use_chunked(bool enabled) { use_chunked_ = enabled; }
    bool use_chunked() const { return use_chunked_; }

    // Set the memory budget (bytes) for chunked series.  When the total data
    // in a chunked series exceeds this budget, the oldest chunks are dropped.
    // 0 = unlimited.  Default: 0.  Only applies when use_chunked is true.
    void   set_chunked_memory_budget(size_t bytes) { chunked_memory_budget_ = bytes; }
    size_t chunked_memory_budget() const { return chunked_memory_budget_; }

    // Enable/disable level-of-detail cache for chunked series.
    // When enabled, a decimation pyramid is built so the renderer can use
    // coarser data when zoomed out.  Default: true (when chunked mode active).
    void set_chunked_lod(bool enabled) { chunked_lod_ = enabled; }
    bool chunked_lod() const { return chunked_lod_; }

    // ---------- auto-scroll (C2) -----------------------------------------

    // Set the sliding time window width (seconds) applied to all plots.
    // Clamped to [MIN_WINDOW_S, MAX_WINDOW_S].
    void   set_time_window(double seconds);
    double time_window() const;

    // Pause / resume auto-scroll following for a specific plot.
    void pause_scroll(int plot_id);
    void resume_scroll(int plot_id);
    void toggle_scroll_paused(int plot_id);
    bool is_scroll_paused(int plot_id) const;

    // Pause / resume all plots at once.
    void pause_all_scroll();
    void resume_all_scroll();

    // Estimated memory used by a plot's LineSeries data.
    // Returns 0 if id is not found.
    size_t memory_bytes(int plot_id) const;

    // Total estimated memory across all plots.
    size_t total_memory_bytes() const;

    // ---------- callbacks ------------------------------------------------

    // Called (from render thread, inside poll()) whenever a new point is appended.
    using OnDataCallback = std::function<void(int plot_id, double t_sec, double value)>;
    void set_on_data(OnDataCallback cb) { on_data_cb_ = std::move(cb); }

   private:
    // ---------------------------------------------------------------------------
    // PlotEntry — internal per-plot state.
    // ---------------------------------------------------------------------------
    struct PlotEntry
    {
        int         id{-1};
        std::string topic;
        std::string field_path;
        std::string type_name;

        // Spectra objects (owned here).
        std::unique_ptr<spectra::Figure> figure;
        spectra::Axes*                   axes{nullptr};   // non-owning, into figure->axes()

        // Base series pointer (always valid, points to either series or chunked).
        spectra::Series* base_series{nullptr};

        // Non-null when using LineSeries (use_chunked == false).
        spectra::LineSeries* series{nullptr};

        // Non-null when using ChunkedLineSeries (use_chunked == true).
        spectra::ChunkedLineSeries* chunked{nullptr};

        // ROS2 subscription.
        std::unique_ptr<GenericSubscriber> subscriber;
        int                                extractor_id{-1};

        // Auto-fit state.
        size_t samples_received{0};
        bool   auto_fitted{false};

        // Color index in the palette (assigned at creation).
        size_t color_index{0};

        // Scratch buffer reused by poll() — avoids per-frame heap alloc.
        std::vector<FieldSample> drain_buf;

        // Time origin for relative timestamps (seconds since epoch).
        double time_origin{0.0};
        bool   has_time_origin{false};

        // Direct-write context (thread-safe series mode).
        // When non-null, the subscriber invokes a callback directly from the
        // executor thread and bypasses the ring buffer drain in poll().
        std::unique_ptr<DirectWriteContext> direct_ctx;
    };

    // Find entry by id; returns nullptr if not found.
    PlotEntry*       find_entry(int id);
    const PlotEntry* find_entry(int id) const;

    // Auto-detect type name from ROS2 graph for a topic.
    // Returns empty string if not found.
    std::string detect_type(const std::string& topic) const;

    // Assign the next palette color (cycles through palette::default_cycle).
    spectra::Color next_color();

    // ---------- members ---------------------------------------------------

    Ros2Bridge&          bridge_;
    MessageIntrospector& intr_;

    mutable std::mutex                      mutex_;
    std::vector<std::unique_ptr<PlotEntry>> entries_;
    int                                     next_id_{1};

    // Color assignment.
    size_t color_cursor_{0};

    // Configuration.
    uint32_t figure_width_         = 1280;
    uint32_t figure_height_        = 720;
    size_t   default_buffer_depth_ = 10000;
    size_t   auto_fit_samples_     = AUTO_FIT_SAMPLES;
    double   scroll_window_s_      = DEFAULT_SCROLL_WINDOW_S;
    double   prune_buffer_s_       = DEFAULT_PRUNE_BUFFER_S;
    bool     pruning_enabled_      = true;

    // Chunked series configuration (LT-8).
    bool   use_chunked_           = false;
    size_t chunked_memory_budget_ = 0;   // 0 = unlimited
    bool   chunked_lod_           = true;

    OnDataCallback on_data_cb_;

    TopicDiscovery* discovery_{nullptr};
};

}   // namespace spectra::adapters::ros2
