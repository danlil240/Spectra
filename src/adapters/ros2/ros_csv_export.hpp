#pragma once

// RosCsvExport — export ROS2-plotted data to CSV with dual ROS timestamps.
//
// Exports data from one or more active plots managed by RosPlotManager.
// Each exported row contains:
//   timestamp_sec  — integer seconds component of the ROS2 message stamp
//   timestamp_nsec — nanoseconds remainder (0–999999999)
//   wall_clock     — wall-clock timestamp in seconds (float, may differ from stamp)
//   <field columns> — one column per exported plot, named "<topic>/<field_path>"
//
// Export modes:
//   Full  — all data currently stored in each LineSeries
//   Range — only rows whose X value (timestamp_s) falls within [x_min, x_max]
//
// Alignment policy (multi-series):
//   When exporting multiple plots, the union of all distinct X timestamps is
//   used as the row set.  Missing values for a given series at a timestamp are
//   written as the configured missing_value string (default "").
//
// Usage (single series, full history):
//   RosCsvExport exporter(mgr);
//   exporter.config().separator = ',';
//   exporter.config().precision = 9;
//   auto result = exporter.export_plot(handle.id);
//   result.save_to_file("/tmp/imu_x.csv");
//
// Usage (multiple series, visible range):
//   RosCsvExport exporter(mgr);
//   auto result = exporter.export_plots({id1, id2, id3},
//                                       RosCsvExport::RangeMode::Visible,
//                                       x_min, x_max);
//   std::string csv = result.to_string();
//
// Thread-safety:
//   All export methods read LineSeries data that is owned by RosPlotManager.
//   They must be called from the render thread (same thread as poll()).
//   The underlying LineSeries x_data()/y_data() spans are NOT thread-safe
//   against concurrent poll() — do not call export while poll() is running.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{
class LineSeries;
}

namespace spectra::adapters::ros2
{

class RosPlotManager;

// ---------------------------------------------------------------------------
// CsvExportConfig — controls formatting of all CSV output.
// ---------------------------------------------------------------------------

struct CsvExportConfig
{
    // Column separator character.  Default ','.
    char separator = ',';

    // Decimal precision for value columns.  Default 9.
    int precision = 9;

    // Decimal precision for the wall_clock column.  Default 9.
    int wall_clock_precision = 9;

    // String written when a series has no data point at a given timestamp row.
    // Default "".
    std::string missing_value;

    // If true, write a header row.  Default true.
    bool write_header = true;

    // Line ending.  Default "\n".
    std::string line_ending = "\n";
};

// ---------------------------------------------------------------------------
// CsvExportResult — the result of one export operation.
// ---------------------------------------------------------------------------

struct CsvExportResult
{
    // True if export succeeded and at least one row was written.
    bool ok{false};

    // Human-readable error message (empty on success).
    std::string error;

    // Number of data rows written (not counting the header).
    size_t row_count{0};

    // Number of columns (including timestamp columns).
    size_t column_count{0};

    // The column headers (in order).
    std::vector<std::string> headers;

    // All rows: each row is a vector of string-encoded values aligned to headers.
    // row_data[i][j] is row i, column j.
    std::vector<std::vector<std::string>> row_data;

    // Serialize to a CSV string using the separator/line_ending that produced this result.
    std::string to_string() const;

    // Write to a file.  Returns true on success.
    bool save_to_file(const std::string& path) const;

    // Internal fields set by the exporter to support to_string() / save_to_file().
    char        separator_{','};
    std::string line_ending_{"\n"};
    bool        write_header_{true};
};

// ---------------------------------------------------------------------------
// RosCsvExport — main export class.
// ---------------------------------------------------------------------------

class RosCsvExport
{
public:
    // Export mode.
    enum class RangeMode
    {
        Full,     // Export all data in the series
        Visible,  // Export only samples in [x_min, x_max]
    };

    explicit RosCsvExport(RosPlotManager& mgr);

    // Non-copyable.
    RosCsvExport(const RosCsvExport&)            = delete;
    RosCsvExport& operator=(const RosCsvExport&) = delete;
    RosCsvExport(RosCsvExport&&)                 = default;
    RosCsvExport& operator=(RosCsvExport&&)      = delete;

    // ---------- configuration --------------------------------------------

    CsvExportConfig&       config() { return config_; }
    const CsvExportConfig& config() const { return config_; }

    // Convenience setters.
    void set_separator(char sep) { config_.separator = sep; }
    void set_precision(int prec) { config_.precision = prec; }
    void set_missing_value(const std::string& v) { config_.missing_value = v; }

    // ---------- export ---------------------------------------------------

    // Export a single plot (full history).
    CsvExportResult export_plot(int plot_id) const;

    // Export a single plot with range filtering.
    // Only rows where x (= timestamp_s) is in [x_min, x_max] are included.
    CsvExportResult export_plot(int    plot_id,
                                double x_min,
                                double x_max) const;

    // Export multiple plots (full history).
    // Rows are the union of all X timestamps across all plots.
    CsvExportResult export_plots(const std::vector<int>& plot_ids) const;

    // Export multiple plots with range filtering.
    CsvExportResult export_plots(const std::vector<int>& plot_ids,
                                 RangeMode               mode,
                                 double                  x_min = 0.0,
                                 double                  x_max = 0.0) const;

    // ---------- low-level helpers (public for testing) -------------------

    // Convert a timestamp_s (seconds, as stored in LineSeries X) and the
    // corresponding timestamp_ns (nanoseconds, as stored in FieldSample) to
    // separate sec / nsec columns.
    //
    // When timestamp_ns == 0 the function falls back to deriving sec/nsec
    // from timestamp_s directly (treating it as a raw float seconds value).
    static void split_timestamp(double    timestamp_s,
                                int64_t   timestamp_ns,
                                int64_t&  out_sec,
                                int64_t&  out_nsec);

    // Format a double value with the given precision into a string.
    static std::string format_value(double v, int precision);

    // Format an int64 as a decimal string.
    static std::string format_int64(int64_t v);

    // Build a column header name from topic + field_path.
    // e.g. "/imu" + "linear_acceleration.x" → "/imu/linear_acceleration.x"
    static std::string make_column_name(const std::string& topic,
                                        const std::string& field_path);

    // Build the fixed timestamp columns header: timestamp_sec, timestamp_nsec,
    // wall_clock (always three columns regardless of value columns).
    static std::vector<std::string> timestamp_headers();

    // Populate timestamp string cells from a timestamp_s value.
    // timestamp_s is the X value from LineSeries (seconds as float).
    // Returns {sec_str, nsec_str, wall_clock_str}.
    std::vector<std::string> format_timestamp_cells(double timestamp_s,
                                                    int64_t timestamp_ns) const;

private:
    // Internal helper: build result from a set of (label, x[], y[], ns[]) tuples.
    // ns_data may be empty (timestamp_ns = 0 used as fallback).
    struct SeriesData
    {
        std::string            column_name;
        std::vector<float>     x;   // timestamp_s (from LineSeries::x_data())
        std::vector<float>     y;   // value (from LineSeries::y_data())
        std::vector<int64_t>   ns;  // timestamp_ns per sample (may be empty)
    };

    CsvExportResult build_result(const std::vector<SeriesData>& series,
                                 RangeMode                      mode,
                                 double                         x_min,
                                 double                         x_max) const;

    RosPlotManager& mgr_;
    CsvExportConfig config_;
};

}   // namespace spectra::adapters::ros2
