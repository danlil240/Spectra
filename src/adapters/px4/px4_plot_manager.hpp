#pragma once

// Px4PlotManager — maps ULog topics + MAVLink telemetry to Spectra plot series.
//
// Owns LineSeries instances for each subscribed field, feeds them with data
// from either a ULogReader (offline log analysis) or Px4Bridge (real-time).
//
// Usage:
//   Px4PlotManager mgr;
//   mgr.load_ulog(reader);       // offline mode
//   mgr.add_field("vehicle_attitude", "roll");
//
//   // or real-time:
//   mgr.set_bridge(&bridge);
//   mgr.add_live_field("ATTITUDE", "roll");
//   mgr.poll();                  // per-frame update

#include "px4_bridge.hpp"
#include "ulog_reader.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{
class LineSeries;
class Axes;
}   // namespace spectra

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// PlotField — one plotted field.
// ---------------------------------------------------------------------------

struct PlotField
{
    std::string topic;      // ULog message name or MAVLink channel name
    std::string field;      // field name within the message
    int         array_idx{-1};   // >=0 for array element, -1 for scalar
    uint8_t     multi_id{0};

    // Display config.
    std::string label;      // series label (auto-generated if empty)
    bool        visible{true};

    // Data (filled by manager).
    std::vector<float> times;
    std::vector<float> values;
};

// ---------------------------------------------------------------------------
// Px4PlotManager
// ---------------------------------------------------------------------------

class Px4PlotManager
{
public:
    Px4PlotManager();
    ~Px4PlotManager();

    // ------------------------------------------------------------------
    // Offline mode: load from a ULogReader
    // ------------------------------------------------------------------

    // Load all topic data from a parsed ULog file.
    // After loading, use add_field() to select which fields to plot.
    void load_ulog(const ULogReader& reader);

    // Clear all loaded data and fields.
    void clear();

    // ------------------------------------------------------------------
    // Real-time mode: set a Px4Bridge
    // ------------------------------------------------------------------

    void set_bridge(Px4Bridge* bridge) { bridge_ = bridge; }

    // Poll the bridge for new data (call once per frame).
    void poll();

    // Set the time window for real-time display (seconds).
    void set_time_window(double seconds) { time_window_s_ = seconds; }
    double time_window() const { return time_window_s_; }

    // ------------------------------------------------------------------
    // Field management
    // ------------------------------------------------------------------

    // Add a field to plot.  For array fields, use array_idx >= 0.
    // Returns the index of the added field.
    size_t add_field(const std::string& topic, const std::string& field,
                     int array_idx = -1, uint8_t multi_id = 0);

    // Add a live field from MAVLink telemetry.
    size_t add_live_field(const std::string& channel, const std::string& field);

    // Remove a field by index.
    void remove_field(size_t index);

    // Remove all fields for a given topic.
    void remove_topic(const std::string& topic);

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    const std::vector<PlotField>& fields() const { return fields_; }
    size_t field_count() const { return fields_.size(); }

    // Get all available topics from the loaded ULog or bridge.
    std::vector<std::string> available_topics() const;

    // Get all field names for a topic.
    std::vector<std::string> topic_fields(const std::string& topic) const;

    bool is_live_mode() const { return bridge_ != nullptr; }
    bool has_ulog_data() const { return ulog_ != nullptr; }

private:
    void refresh_ulog_field(PlotField& f);
    void refresh_live_field(PlotField& f);

    const ULogReader*     ulog_{nullptr};
    Px4Bridge*            bridge_{nullptr};

    std::vector<PlotField> fields_;
    double                 time_window_s_{30.0};

    // Live mode: track last-seen timestamps per channel.
    std::unordered_map<std::string, uint64_t> last_seen_ts_;
};

}   // namespace spectra::adapters::px4
