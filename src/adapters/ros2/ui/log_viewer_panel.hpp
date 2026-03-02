#pragma once

// LogViewerPanel — ImGui /rosout log viewer panel (F5).
//
// Renders a scrolling, filterable table of ROS2 log messages from a
// RosLogViewer backend.  Designed as a dockable ImGui window.
//
// Features:
//   - Colour-coded severity rows: DEBUG=gray, INFO=white, WARN=yellow,
//     ERROR=red, FATAL=bright-red/magenta
//   - Per-severity badge counts in the toolbar
//   - Filter toolbar: severity dropdown, node filter text input, message
//     regex input (with error indicator), AND logic
//   - Auto-scroll to newest entry (paused while user scrolls up)
//   - Pause / Resume / Clear buttons
//   - Copy selected row (or all visible rows) to clipboard as plain text
//   - Expandable detail pane for selected row (file, function, line, stamp)
//   - "Follow" toggle — re-enables auto-scroll
//   - 10 K-entry ring buffer indicator in status bar
//
// Thread-safety:
//   All draw() calls must come from the render thread.
//   RosLogViewer is internally thread-safe; LogViewerPanel takes a filtered
//   snapshot each frame (capped at display_hz_).
//
// Typical usage:
//   RosLogViewer viewer(node);
//   viewer.subscribe();
//   LogViewerPanel panel(viewer);
//
//   // In ImGui render loop:
//   panel.draw();

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ros_log_viewer.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// LogViewerPanel
// ---------------------------------------------------------------------------

class LogViewerPanel
{
public:
    // viewer — must outlive this panel (non-owning reference).
    explicit LogViewerPanel(RosLogViewer& viewer);
    ~LogViewerPanel() = default;

    LogViewerPanel(const LogViewerPanel&)            = delete;
    LogViewerPanel& operator=(const LogViewerPanel&) = delete;
    LogViewerPanel(LogViewerPanel&&)                 = delete;
    LogViewerPanel& operator=(LogViewerPanel&&)      = delete;

    // -----------------------------------------------------------------------
    // ImGui rendering
    // -----------------------------------------------------------------------

    // Render the panel as a standalone dockable window.
    // p_open — if non-null, a close button is shown.
    void draw(bool* p_open = nullptr);

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    void set_title(const std::string& t) { title_ = t; }
    const std::string& title() const { return title_; }

    // Maximum display refresh rate (default 20 Hz).
    void set_display_hz(double hz) { display_interval_s_ = (hz > 0.0) ? 1.0 / hz : 0.0; }
    double display_hz() const { return (display_interval_s_ > 0.0) ? 1.0 / display_interval_s_ : 0.0; }

    // Maximum rows to render in the table (older ones hidden for performance).
    // Default 2000.
    void set_max_display_rows(size_t n) { max_display_rows_ = (n > 0) ? n : 1; }
    size_t max_display_rows() const { return max_display_rows_; }

    // -----------------------------------------------------------------------
    // State accessors (testing / inspection — no ImGui dependency)
    // -----------------------------------------------------------------------

    bool is_paused()      const { return viewer_.is_paused(); }
    bool auto_scroll()    const { return auto_scroll_; }
    int  selected_row()   const { return selected_row_; }

    // Last snapshot size (entries after filter).
    size_t visible_count() const { return visible_count_; }

    // Copy text of all visible rows to a string (clipboard content).
    // Public so tests can verify without ImGui.
    std::string build_copy_text(const std::vector<LogEntry>& entries) const;

    // Format a single row as a tab-separated line.
    static std::string format_row(const LogEntry& e);

private:
    // -----------------------------------------------------------------------
    // ImGui sub-components (SPECTRA_USE_IMGUI guard in .cpp)
    // -----------------------------------------------------------------------

    void draw_toolbar();
    void draw_filter_bar();
    void draw_table(const std::vector<LogEntry>& entries);
    void draw_detail_pane(const LogEntry& e);
    void draw_status_bar(const std::vector<LogEntry>& entries);

    // Refresh display snapshot if enough time has passed.
    void maybe_refresh();

    // Copy text to clipboard (ImGui or fallback store).
    void set_clipboard(const std::string& text);

    // Wall-clock helper.
    static double wall_time_now();

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    RosLogViewer&  viewer_;
    std::string    title_{"ROS2 Log"};

    // Display rate throttle (render thread only).
    double display_interval_s_{1.0 / 20.0};
    double last_refresh_time_s_{0.0};

    // Cached snapshot from last refresh.
    std::vector<LogEntry> cached_entries_;
    size_t                visible_count_{0};

    // Auto-scroll state.
    bool auto_scroll_{true};
    bool scroll_to_bottom_{false};

    // Selected row index into cached_entries_ (-1 = none).
    int selected_row_{-1};

    // Whether the detail pane is shown.
    bool show_detail_{true};

    // Max rows rendered in the table.
    size_t max_display_rows_{2000};

    // Filter UI state (mirrored from/to viewer filter).
    // We store local copies so we can edit them in text inputs.
    char   node_filter_buf_[256]{};
    char   regex_filter_buf_[256]{};
    int    severity_combo_idx_{0};   // index into severity_levels_[]

    // Last clipboard text (headless fallback for tests).
    std::string last_clipboard_text_;

    // Static list of severity combo labels (maps to LogSeverity values).
    static const char* const severity_labels_[];
    static const LogSeverity severity_values_[];
    static constexpr int     severity_count_ = 6;
};

}   // namespace spectra::adapters::ros2
