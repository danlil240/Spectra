#pragma once

// Px4AppShell — application shell for spectra-px4 standalone executable.
//
// Owns and wires PX4 adapter backends + panels into a dockspace-driven UI.
// Supports both offline ULog analysis and real-time MAVLink inspection.

#include "px4_bridge.hpp"
#include "px4_plot_manager.hpp"
#include "px4_recent_files.hpp"
#include "ulog_reader.hpp"

#include <spectra/figure_registry.hpp>
#include <spectra/fwd.hpp>

#include "ui/live_connection_panel.hpp"
#include "ui/px4_panel_helpers.hpp"
#include "ui/ulog_file_panel.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/app_shell.hpp"
#endif

namespace spectra
{
class CommandRegistry;
class Figure;
class LayoutManager;
class ShortcutManager;
class WindowManager;
}   // namespace spectra

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Px4AppConfig
// ---------------------------------------------------------------------------

struct Px4AppConfig
{
    std::string ulog_file;   // optional: open this ULog on launch
    std::string host{"127.0.0.1"};
    uint16_t    port{14540};
    bool        auto_connect{false};
    double      time_window_s{30.0};
    uint32_t    window_width{1400};
    uint32_t    window_height{800};

    // When true, use ChunkedLineSeries for plotting (efficient for large
    // ULog files and long-running live sessions).  Default: false.
    bool use_chunked{false};

    // Memory budget (bytes) for chunked series.  0 = unlimited.
    size_t chunked_memory_budget{0};

    // Where File > Open Recent is persisted.  Empty keeps the list in memory
    // only; parse_px4_args() fills in Px4RecentFiles::default_path().
    std::string recent_files_path;
};

Px4AppConfig parse_px4_args(int argc, char** argv, std::string& error_out);

// ---------------------------------------------------------------------------
// AutoPlotField — one field within an auto-plot group.
// ---------------------------------------------------------------------------

struct AutoPlotField
{
    std::string topic;
    std::string field;
    int         array_idx{-1};
    uint8_t     multi_id{0};
    std::string label;

    std::vector<float> times;
    std::vector<float> values;
};

// ---------------------------------------------------------------------------
// AutoPlotGroup — a logical group of related fields shown in one subplot.
// ---------------------------------------------------------------------------

struct AutoPlotGroup
{
    std::string                title;
    std::string                ylabel;
    std::vector<AutoPlotField> fields;
};

// ---------------------------------------------------------------------------
// Px4AppShell
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI
class Px4AppShell : public spectra::ui::shell::AppShell
#else
class Px4AppShell
#endif
{
   public:
    explicit Px4AppShell(const Px4AppConfig& cfg);
    ~Px4AppShell();

    Px4AppShell(const Px4AppShell&)            = delete;
    Px4AppShell& operator=(const Px4AppShell&) = delete;
    Px4AppShell(Px4AppShell&&)                 = delete;
    Px4AppShell& operator=(Px4AppShell&&)      = delete;

    // Bind to the Spectra render figure.  It holds the auto-plot and fields
    // until set_figure_source() lets other figures hold their own.
    void set_canvas_figure(spectra::Figure* fig);

    // Multi-figure support (new figures, split panes).  `registry` resolves
    // figure ids (a closed figure drops its plots); `active_figure` names the
    // figure the user is looking at, which receives new fields, Auto Plot and
    // Close All Plots.  Without it every plot goes to the canvas figure.
    void set_figure_source(spectra::FigureRegistry*           registry,
                           std::function<spectra::FigureId()> active_figure);
    // The figure new plots go to (the canvas figure in single-figure mode).
    spectra::Figure* target_figure() const;
    // Which figure is drawn at a screen point (split panes); without it drops
    // go to the active figure.
    void set_figure_hit_test(std::function<spectra::FigureId(float, float)> figure_at);

    // Plot a topic / field dragged from the ULog or Live panel into `figure`
    // (the active figure when null).  `axes_index` is the subplot under the
    // cursor: an auto-plot subplot gets the field added to its group.  A
    // payload with no field adds every field of the topic.  Returns false
    // when nothing could be plotted.
    bool drop_field(const Px4FieldDragPayload& payload,
                    spectra::Figure*           figure     = nullptr,
                    int                        axes_index = -1);

    // Figures with their own px4 plot set (for tests / status display).
    size_t plotted_figure_count() const { return plots_.size(); }

    // Wire WindowManager for OS-level panel tearoff.
    void set_window_manager(spectra::WindowManager* wm);

    void set_layout_manager(spectra::LayoutManager* lm);

    bool panel_visible(const char* id) const;
    void set_panel_visible(const char* id, bool v);

    bool nav_rail_visible() const;
    bool nav_rail_expanded() const;
    void set_nav_rail_visible(bool v);
    void set_nav_rail_expanded(bool v);

    // Initialise components.
    bool init();

    // Shut down all components.
    void shutdown();

    // Per-frame update (call from render loop).
    void poll();

    // Draw ImGui UI (call from ImGui frame).
    void draw();

    // Process deferred panel create/destroy — call AFTER app.step().
    void process_pending_panels();

    // Request graceful shutdown.
    void request_shutdown() { shutdown_requested_.store(true, std::memory_order_relaxed); }
    bool shutdown_requested() const { return shutdown_requested_.load(std::memory_order_relaxed); }

    // Open a ULog file (offline analysis).
    bool open_ulog(const std::string& path);

    // OS drag-and-drop: a dropped .ulg/.ulog file is queued and opened on the
    // next poll() (outside the windowing backend's event callback).  Returns
    // false, and leaves the open log untouched, for any other file type.
    // set_window_manager() routes the window manager's file drops here.
    bool handle_file_drop(const std::string& path);
    // Opens the queued dropped file, if any.  Called by poll().
    bool process_pending_file_drop();
    // True for paths with a ULog extension (.ulg / .ulog, any case).
    static bool is_ulog_path(const std::string& path);

    // Message for the last failed open / rejected drop (empty after a success).
    const std::string& last_open_error() const { return last_open_error_; }

    // Auto-plot interesting fields from the loaded ULog (flight-review style).
    void auto_plot_ulog();

    // Close all auto-generated and manually added plots.
    void close_all_plots();

    // Subplot grid {rows, cols} used for `num_groups` auto-plot groups.  Four
    // or more groups are tiled into columns so every plot fits the canvas.
    static std::pair<int, int> auto_plot_grid(int num_groups);

    // Most-recently-opened ULog files (newest first).
    const Px4RecentFiles& recent_files() const { return recent_; }
    // Open a recent entry; a file that no longer exists is dropped from the list.
    bool open_recent(const std::string& path);
    void clear_recent_files();

    // File picker used by "Open ULog..." (default: native dialog).  Returns the
    // chosen path, or empty when cancelled.
    void set_file_chooser(std::function<std::string()> chooser);

#ifdef SPECTRA_USE_IMGUI
    // Register the px4 commands (open ULog, quit) with the app's command
    // registry and bind their keyboard shortcuts (Ctrl+O, Ctrl+Q).  Both must
    // outlive the shell, or call unregister_commands() before they go away.
    void register_commands(spectra::CommandRegistry& registry, spectra::ShortcutManager& shortcuts);
    void unregister_commands();
    // Run a command from the app's registry (no-op before register_commands()).
    void run_command(const char* id);
#endif

    // True when there is nothing to plot (no ULog open, no MAVLink link, no
    // fields): the canvas shows an "Open ULog" empty state instead of an
    // empty 0-1 axes, and the canvas figure has no axes.
    bool empty_state_visible() const;

    // Start the MAVLink bridge with the configured host/port.
    void connect_live();

    // Accessors.
    ULogReader& reader() { return reader_; }
    Px4Bridge&  bridge() { return bridge_; }
    // Plot manager of the target figure (see set_figure_source()).
    Px4PlotManager&     plot_manager() { return active_plots().mgr; }
    const Px4AppConfig& config() const { return cfg_; }

#ifdef SPECTRA_USE_IMGUI
   protected:
    void on_register_panels() override;
    void on_populate_menus(spectra::ui::shell::MenuBar& bar) override;
    void on_populate_nav_rail(spectra::ui::shell::NavRail& rail) override;
    void on_default_layout(unsigned int dockspace_id) override;
    void on_build_status_bar(spectra::ui::shell::StatusBar& bar) override;
    std::unique_ptr<spectra::ui::shell::CanvasHost> create_canvas_host() override;
#endif

   private:
    // The plots shown in one figure: its fields plus the auto-plot groups.
    struct FigurePlots
    {
        spectra::FigureId          id{spectra::INVALID_FIGURE_ID};
        spectra::Figure*           figure{nullptr};
        bool                       primary{false};   // the canvas figure
        Px4PlotManager             mgr;
        std::vector<AutoPlotGroup> auto_groups;
        bool                       auto_active{false};
        uint64_t                   last_revision{static_cast<uint64_t>(-1)};
    };

    FigurePlots&                 active_plots() { return *plots_[active_plots_]; }
    const FigurePlots&           active_plots() const { return *plots_[active_plots_]; }
    std::unique_ptr<FigurePlots> make_figure_plots(spectra::Figure* fig, spectra::FigureId id);
    // Resolve figure ids, drop closed figures and pick the active plot set.
    void refresh_figure_targets();
    // Plot set of `fig`, created for a figure the registry knows; else null.
    FigurePlots* plots_for(spectra::Figure* fig);
    void         draw_field_drop_target();

    bool        open_ulog_with_dialog();
    void        sync_canvas_figure(bool force = false);
    void        sync_figure(FigurePlots& fp, bool force);
    void        sync_auto_plot_figure(FigurePlots& fp);
    void        sync_manual_plot_figure(FigurePlots& fp, bool force);
    static void rebuild_figure_axes(FigurePlots& fp, int rows, int cols);
    void        refresh_recent_menu();
    void        sync_layout_chrome();

    void draw_ulog_file(bool* p_open = nullptr);
    void draw_live_connection(bool* p_open = nullptr);
    void draw_canvas_empty_state(const spectra::Rect& region);

    friend class Px4CanvasHost;

    Px4AppConfig cfg_;

    ULogReader reader_;
    Px4Bridge  bridge_;

    // One plot set per figure; [0] is the canvas figure's and always exists.
    std::vector<std::unique_ptr<FigurePlots>>      plots_;
    size_t                                         active_plots_{0};
    spectra::FigureRegistry*                       figure_registry_{nullptr};
    std::function<spectra::FigureId()>             active_figure_fn_;
    std::function<spectra::FigureId(float, float)> figure_at_fn_;

    std::unique_ptr<ULogFilePanel>       file_panel_;
    std::unique_ptr<LiveConnectionPanel> live_panel_;

    std::atomic<bool> shutdown_requested_{false};

    std::string last_open_error_;
    std::string pending_drop_path_;

    Px4RecentFiles               recent_;
    bool                         recent_menu_dirty_{true};
    std::function<std::string()> file_chooser_;
    spectra::CommandRegistry*    cmd_registry_{nullptr};
    spectra::ShortcutManager*    shortcut_mgr_{nullptr};

#ifdef SPECTRA_USE_IMGUI
    std::map<std::string, bool> pending_panel_visibility_;
#endif
};

}   // namespace spectra::adapters::px4
