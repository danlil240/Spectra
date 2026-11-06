// log_browser_panel.hpp — browse recorded log files (ULog, MCAP, ...) and
// plot their fields.
//
// The panel keeps a list of open log files, each read through a
// logs::LogReader picked by a logs::LogReaderRegistry, and shows their fields
// as a searchable topic → field tree.  A field is plotted by dragging it onto
// any 2D axes (payload LOG_FIELD_DRAG_TYPE; the canvas drop target lives in
// ImGuiIntegration) or by double-clicking it, which plots into the axes
// returned by the AxesProvider (the active figure, created if needed).
//
// Plotted series keep timestamps in double precision (Series::x_offset), so
// boot-time and epoch timestamps both stay exact.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "io/logs/log_reader.hpp"
#include "io/logs/log_reader_registry.hpp"

namespace spectra
{
class Axes;
class LineSeries;
}   // namespace spectra

namespace spectra::ui::logs
{

// ImGui drag-and-drop payload type id used by field rows.
inline constexpr const char* LOG_FIELD_DRAG_TYPE = "SPECTRA_LOG_FIELD";

// Fixed-size payload passed through ImGui drag/drop.
struct LogFieldDragPayload
{
    uint32_t file_id     = 0;
    uint32_t field_index = 0;
};

class LogBrowserPanel
{
   public:
    // Axes to plot into when no target is given (double-click, or a drop on
    // the empty-app welcome screen).  May create a figure; nullptr if none.
    using AxesProvider = std::function<Axes*()>;
    // Asks the host to show a native "open file" dialog (File > Open Log...).
    using OpenDialogFn = std::function<void()>;

    struct OpenFile
    {
        uint32_t                                  id = 0;
        std::string                               path;
        std::string                               name;   // file name without directories
        std::unique_ptr<spectra::logs::LogReader> reader;
        spectra::logs::LogFieldNode               tree;
    };

    LogBrowserPanel();
    ~LogBrowserPanel();
    LogBrowserPanel(const LogBrowserPanel&)            = delete;
    LogBrowserPanel& operator=(const LogBrowserPanel&) = delete;

    // ─── Wiring ──────────────────────────────────────────────────────────

    // Registry used to pick readers (not owned).  nullptr selects a private
    // registry with the built-in readers.
    void                                    set_registry(const spectra::logs::LogReaderRegistry* r);
    const spectra::logs::LogReaderRegistry& registry() const;

    void set_axes_provider(AxesProvider p) { axes_provider_ = std::move(p); }
    bool has_axes_provider() const { return static_cast<bool>(axes_provider_); }
    void set_open_dialog_callback(OpenDialogFn fn) { open_dialog_cb_ = std::move(fn); }

    // ─── Files ───────────────────────────────────────────────────────────

    // True if the registry has a reader for this file (by content or extension).
    bool can_open(const std::string& path) const;

    // Open (or re-open, replacing the old entry) a log file and show the
    // panel.  Returns false and sets status() on failure.
    bool open_file(const std::string& path);
    void close_file(uint32_t file_id);

    const std::vector<OpenFile>& files() const { return files_; }
    const OpenFile*              find_file(uint32_t file_id) const;

    // ─── Plotting ────────────────────────────────────────────────────────

    // Read a field and add it as a line series to `target` (or the
    // AxesProvider's axes when null).  Returns the new series, or nullptr
    // (with status() set) on failure.
    LineSeries* plot_field(uint32_t file_id, size_t field_index, Axes* target = nullptr);
    LineSeries* plot_payload(const LogFieldDragPayload& payload, Axes* target = nullptr)
    {
        return plot_field(payload.file_id, payload.field_index, target);
    }

    // Last error or info line shown at the top of the panel.
    const std::string& status() const { return status_; }
    bool               status_is_error() const { return status_error_; }

    // ─── UI ──────────────────────────────────────────────────────────────

    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }

    // Field search text (case-insensitive substring of "topic/path").
    void        set_filter(const std::string& text);
    const char* filter() const { return filter_buf_; }

    // Draw the panel window.  No-op when SPECTRA_USE_IMGUI is not defined.
    void draw();

   private:
    void set_status(std::string msg, bool error);
    void draw_file(OpenFile& file, bool filtering);
    // Returns true if any leaf under `node` passes the filter.
    bool draw_node(const OpenFile& file, const spectra::logs::LogFieldNode& node, bool filtering);
    bool subtree_matches(const OpenFile& file, const spectra::logs::LogFieldNode& node) const;

    const spectra::logs::LogReaderRegistry*                   registry_ = nullptr;
    mutable std::unique_ptr<spectra::logs::LogReaderRegistry> own_registry_;

    std::vector<OpenFile> files_;
    uint32_t              next_file_id_ = 1;

    AxesProvider axes_provider_;
    OpenDialogFn open_dialog_cb_;

    std::string status_;
    bool        status_error_    = false;
    bool        visible_         = false;
    char        filter_buf_[128] = {};
    uint32_t    pending_close_   = 0;   // file closed after the frame's draw
};

}   // namespace spectra::ui::logs
