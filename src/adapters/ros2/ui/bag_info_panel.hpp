#pragma once

// BagInfoPanel — ImGui panel showing rosbag metadata, topic table, and
// drag-and-drop bag file opening (D4).
//
// Features:
//   - Bag metadata section: path, storage format, duration, start/end time,
//     total message count, compressed file size.
//   - Topic table: scrollable table with columns (Topic, Type, Messages).
//     Single-click fires an optional TopicSelectCallback.
//     Double-click fires an optional TopicPlotCallback.
//   - Drag-and-drop: accepts ImGui drag payloads carrying a file path ending
//     in .db3 or .mcap.  Also accepts OS drop events forwarded by the caller
//     via try_open_file().
//   - Open / close buttons in the panel header.
//   - "No bag open" placeholder when no bag is loaded.
//
// All ImGui rendering paths are gated on SPECTRA_USE_IMGUI — the pure-logic
// API (open_bag, metadata, topics, etc.) is always available.
//
// Thread-safety:
//   Not thread-safe.  All methods must be called from the render thread.
//   BagReader itself is also not thread-safe; the panel owns the reader.
//
// Dependencies:
//   - BagReader (D1) — gated behind SPECTRA_ROS2_BAG the same way.
//   - No ROS2 node needed for metadata display.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bag_reader.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// BagTopicRow — one row in the panel's topic table (plain struct, no ImGui).
// ---------------------------------------------------------------------------

struct BagTopicRow
{
    std::string name;           // fully qualified topic name
    std::string type;           // message type string
    uint64_t    message_count;  // total messages for this topic in the bag
};

// ---------------------------------------------------------------------------
// BagSummary — flattened metadata snapshot (plain struct, no ImGui).
// Populated by open_bag() / refresh_summary() from BagMetadata.
// Always valid to read (empty/zero fields when no bag is open).
// ---------------------------------------------------------------------------

struct BagSummary
{
    bool        is_open{false};
    std::string path;
    std::string storage_id;       // "sqlite3" or "mcap"
    double      duration_sec{0.0};
    double      start_time_sec{0.0};
    double      end_time_sec{0.0};
    uint64_t    message_count{0};
    uint64_t    compressed_size{0};  // bytes; 0 if not available
    std::string last_error;

    std::vector<BagTopicRow> topics;

    // Helpers —————————————————————————————————————————————————————————————
    bool        valid()     const noexcept { return is_open; }
    std::size_t topic_count() const noexcept { return topics.size(); }

    // Human-readable duration string, e.g. "1h 23m 04s" / "45.3 s"
    std::string duration_string() const;

    // Human-readable size string, e.g. "1.24 GB" / "512 KB"
    static std::string format_size(uint64_t bytes);

    // Human-readable absolute time (seconds since epoch → "HH:MM:SS.sss")
    static std::string format_time(double seconds_epoch);
};

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

// Called when the user single-clicks a row in the topic table.
// Arguments: topic_name, type_string
using BagTopicSelectCallback = std::function<void(const std::string&, const std::string&)>;

// Called when the user double-clicks (or explicitly "plots") a topic row.
// Arguments: topic_name, type_string
using BagTopicPlotCallback   = std::function<void(const std::string&, const std::string&)>;

// Called when a new bag is successfully opened (after open_bag / drag-drop).
// Argument: bag path
using BagOpenedCallback = std::function<void(const std::string&)>;

// ---------------------------------------------------------------------------
// BagInfoPanel
// ---------------------------------------------------------------------------

class BagInfoPanel
{
public:
    BagInfoPanel();
    ~BagInfoPanel() = default;

    // Non-copyable, non-movable (owns unique BagReader).
    BagInfoPanel(const BagInfoPanel&)            = delete;
    BagInfoPanel& operator=(const BagInfoPanel&) = delete;
    BagInfoPanel(BagInfoPanel&&)                 = delete;
    BagInfoPanel& operator=(BagInfoPanel&&)      = delete;

    // ------------------------------------------------------------------
    // Bag lifecycle (call from render thread)
    // ------------------------------------------------------------------

    // Open the bag at `path`.  Returns true on success.
    // On failure summary().last_error contains a human-readable message.
    bool open_bag(const std::string& path);

    // Close the current bag and reset state.
    void close_bag();

    // True if a bag is currently open.
    bool is_open() const noexcept { return reader_.is_open(); }

    // Try to open `path` if it ends with ".db3" or ".mcap".
    // Returns true if the path looks like a bag and open succeeded.
    // Use this to forward OS file-drop events.
    bool try_open_file(const std::string& path);

    // Validate that a path looks like a rosbag file (by extension).
    static bool is_bag_path(const std::string& path);

    // ------------------------------------------------------------------
    // Data access
    // ------------------------------------------------------------------

    // Latest metadata snapshot (always valid; empty when no bag is open).
    const BagSummary& summary() const noexcept { return summary_; }

    // Convenience: topic rows from summary.
    const std::vector<BagTopicRow>& topics() const noexcept { return summary_.topics; }

    // Index of the currently selected row (-1 = none).
    int selected_index() const noexcept { return selected_index_; }

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    void set_topic_select_callback(BagTopicSelectCallback cb) { select_cb_ = std::move(cb); }
    void set_topic_plot_callback(BagTopicPlotCallback cb)     { plot_cb_   = std::move(cb); }
    void set_bag_opened_callback(BagOpenedCallback cb)        { opened_cb_ = std::move(cb); }

    const BagTopicSelectCallback& topic_select_callback() const { return select_cb_; }
    const BagTopicPlotCallback&   topic_plot_callback()   const { return plot_cb_; }
    const BagOpenedCallback&      bag_opened_callback()   const { return opened_cb_; }

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    void set_title(const std::string& title) { title_ = title; }
    const std::string& title() const noexcept { return title_; }

    // ------------------------------------------------------------------
    // ImGui draw (SPECTRA_USE_IMGUI only — no-ops otherwise)
    // ------------------------------------------------------------------

    // Draw a standalone ImGui window.
    // p_open — pointer to bool controlling window visibility (may be nullptr).
    void draw(bool* p_open = nullptr);

    // Draw just the panel content inline inside an already-begun ImGui window.
    void draw_inline();

    // ------------------------------------------------------------------
    // Internal helpers (public for unit-testing)
    // ------------------------------------------------------------------

    // Build a BagSummary from the currently open BagReader.
    // Called automatically by open_bag(); exposed for testing.
    void refresh_summary();

    // Fire the select callback for the row at `index` (bounds-checked).
    void select_row(int index);

    // Fire the plot callback for the row at `index` (bounds-checked).
    void plot_row(int index);

private:
    // ------------------------------------------------------------------
    // ImGui sub-sections (always declared, guarded internally)
    // ------------------------------------------------------------------

    void draw_metadata_section();
    void draw_topic_table();
    void draw_drop_zone_overlay();
    void draw_no_bag_placeholder();
    void handle_imgui_drag_drop();

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    BagReader    reader_;
    BagSummary   summary_;

    std::string  title_{"Bag Info"};
    int          selected_index_{-1};

    BagTopicSelectCallback select_cb_;
    BagTopicPlotCallback   plot_cb_;
    BagOpenedCallback      opened_cb_;
};

} // namespace spectra::adapters::ros2
