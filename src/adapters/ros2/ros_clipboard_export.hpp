#pragma once

// RosClipboardExport — copy ROS2-plotted data to the system clipboard as TSV.
//
// Implements E2: Ctrl+C copies the selected range of one or more series as
// tab-separated values (TSV), compatible with spreadsheet paste.
//
// TSV format (matches Excel/LibreOffice Calc expectations):
//   Header row:  timestamp_sec\ttimestamp_nsec\twall_clock\t<col1>\t<col2>...
//   Data rows:   <sec>\t<nsec>\t<wall>\t<v1>\t<v2>...
//
// For multiple series the union of all X timestamps is used as the row set;
// missing values are written as the configured missing_value string (default "").
//
// SelectionRange controls which portion of a series is copied:
//   Full    — all data currently stored in each LineSeries
//   Range   — only rows whose X value (timestamp_s) falls within [x_min, x_max]
//
// Clipboard back-end:
//   - When compiled with SPECTRA_USE_IMGUI the ImGui clipboard API is used.
//   - Otherwise the text is stored in an internal buffer accessible via
//     last_clipboard_text() — useful for headless tests and non-ImGui paths.
//
// Keyboard integration (optional — call from your keyboard handler):
//   if (key == KEY_C && ctrl_held) exporter.copy_plots(ids);
//
// Thread-safety:
//   All methods must be called from the render thread (same as poll()).
//   LineSeries x_data()/y_data() spans are NOT thread-safe against concurrent
//   poll() — do not call copy while poll() is running.

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
// ClipboardExportConfig — formatting parameters for TSV output.
// ---------------------------------------------------------------------------

struct ClipboardExportConfig
{
    // Decimal precision for value columns.  Default 9.
    int precision = 9;

    // Decimal precision for the wall_clock column.  Default 9.
    int wall_clock_precision = 9;

    // String written when a series has no data at a given timestamp row.
    // Default "" (empty cell — standard spreadsheet convention).
    std::string missing_value;

    // If true, write a header row.  Default true.
    bool write_header = true;
};

// ---------------------------------------------------------------------------
// ClipboardCopyResult — outcome of one copy operation.
// ---------------------------------------------------------------------------

struct ClipboardCopyResult
{
    // True if at least one data row was written and placed on the clipboard.
    bool ok{false};

    // Human-readable error (empty on success).
    std::string error;

    // Number of data rows written (not counting the header).
    size_t row_count{0};

    // Number of columns (including the 3 timestamp columns).
    size_t column_count{0};

    // The full TSV string that was placed on the clipboard.
    std::string tsv_text;
};

// ---------------------------------------------------------------------------
// RosClipboardExport — main class.
// ---------------------------------------------------------------------------

class RosClipboardExport
{
   public:
    // Selection range mode.
    enum class SelectionRange
    {
        Full,    // Copy all data stored in each series
        Range,   // Copy only samples with X in [x_min, x_max]
    };

    explicit RosClipboardExport(RosPlotManager& mgr);

    // Non-copyable.
    RosClipboardExport(const RosClipboardExport&)            = delete;
    RosClipboardExport& operator=(const RosClipboardExport&) = delete;
    RosClipboardExport(RosClipboardExport&&)                 = default;
    RosClipboardExport& operator=(RosClipboardExport&&)      = delete;

    // ---------- configuration --------------------------------------------

    ClipboardExportConfig&       config() { return config_; }
    const ClipboardExportConfig& config() const { return config_; }

    void set_precision(int prec) { config_.precision = prec; }
    void set_missing_value(const std::string& v) { config_.missing_value = v; }
    void set_write_header(bool v) { config_.write_header = v; }

    // ---------- copy operations ------------------------------------------

    // Copy a single plot (full history) to the clipboard.
    ClipboardCopyResult copy_plot(int plot_id);

    // Copy a single plot with range filtering.
    // Only rows where x (= timestamp_s) is in [x_min, x_max] are included.
    ClipboardCopyResult copy_plot(int plot_id, double x_min, double x_max);

    // Copy multiple plots (full history) to the clipboard.
    // Rows are the union of all X timestamps across all requested plots.
    ClipboardCopyResult copy_plots(const std::vector<int>& plot_ids);

    // Copy multiple plots with range filtering.
    ClipboardCopyResult copy_plots(const std::vector<int>& plot_ids,
                                   SelectionRange          mode,
                                   double                  x_min = 0.0,
                                   double                  x_max = 0.0);

    // ---------- keyboard helper ------------------------------------------

    // Returns true if the Ctrl+C keyboard shortcut should trigger a copy.
    // Call on_key_event() from your keyboard handler; this tracks the ctrl
    // and 'C' key state and returns true once on the frame they are both down.
    //
    // This is a lightweight stateless check — it does NOT call any copy method;
    // the caller decides which plot ids to copy.
    static bool is_copy_shortcut(int key, bool ctrl_held);

    // ---------- clipboard access -----------------------------------------

    // Returns the TSV text from the most recent successful copy operation.
    // Useful in headless tests and non-ImGui environments.
    const std::string& last_clipboard_text() const { return last_text_; }

    // ---------- low-level helpers (public for testing) -------------------

    // Build a TSV string from pre-collected series data.
    // Equivalent to CsvExportResult::to_string() but tab-separated.
    // ns_data may be empty (falls back to float decomposition of timestamp_s).
    struct SeriesData
    {
        std::string          column_name;
        std::vector<float>   x;    // timestamp_s (from LineSeries::x_data())
        std::vector<float>   y;    // value (from LineSeries::y_data())
        std::vector<int64_t> ns;   // timestamp_ns per sample (may be empty)
    };

    std::string build_tsv(const std::vector<SeriesData>& series,
                          SelectionRange                 mode,
                          double                         x_min,
                          double                         x_max) const;

    // Format a double value with the given precision.
    static std::string format_value(double v, int precision);

    // Format an int64 as a decimal string.
    static std::string format_int64(int64_t v);

    // Build column name: "<topic>/<field_path>".
    static std::string make_column_name(const std::string& topic, const std::string& field_path);

    // Split a timestamp_s + timestamp_ns into sec + nsec components.
    // When timestamp_ns == 0 falls back to float decomposition of timestamp_s.
    static void split_timestamp(double   timestamp_s,
                                int64_t  timestamp_ns,
                                int64_t& out_sec,
                                int64_t& out_nsec);

   private:
    // Internal: build TSV from manager-owned series identified by ids.
    ClipboardCopyResult build_and_copy(const std::vector<int>& ids,
                                       SelectionRange          mode,
                                       double                  x_min,
                                       double                  x_max);

    // Write text to the system clipboard (or internal buffer when no ImGui).
    void set_clipboard(const std::string& text);

    RosPlotManager&       mgr_;
    ClipboardExportConfig config_;
    std::string           last_text_;   // last text placed on clipboard
};

}   // namespace spectra::adapters::ros2
