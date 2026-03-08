#pragma once

// Px4AppShell — application shell for spectra-px4 standalone executable.
//
// Owns and wires PX4 adapter backends + panels into a dockspace-driven UI.
// Supports both offline ULog analysis and real-time MAVLink inspection.

#include "px4_bridge.hpp"
#include "px4_plot_manager.hpp"
#include "ulog_reader.hpp"

#include "ui/live_connection_panel.hpp"
#include "ui/ulog_file_panel.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spectra
{
class Figure;
}

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Px4AppConfig
// ---------------------------------------------------------------------------

struct Px4AppConfig
{
    std::string ulog_file;       // optional: open this ULog on launch
    std::string host{"127.0.0.1"};
    uint16_t    port{14540};
    bool        auto_connect{false};
    double      time_window_s{30.0};
    uint32_t    window_width{1400};
    uint32_t    window_height{800};
};

Px4AppConfig parse_px4_args(int argc, char** argv, std::string& error_out);

// ---------------------------------------------------------------------------
// Px4AppShell
// ---------------------------------------------------------------------------

class Px4AppShell
{
public:
    explicit Px4AppShell(const Px4AppConfig& cfg);
    ~Px4AppShell();

    Px4AppShell(const Px4AppShell&)            = delete;
    Px4AppShell& operator=(const Px4AppShell&) = delete;
    Px4AppShell(Px4AppShell&&)                 = delete;
    Px4AppShell& operator=(Px4AppShell&&)      = delete;

    // Bind to the Spectra render figure.
    void set_canvas_figure(spectra::Figure* fig) { canvas_figure_ = fig; }

    // Initialise components.
    bool init();

    // Shut down all components.
    void shutdown();

    // Per-frame update (call from render loop).
    void poll();

    // Draw ImGui UI (call from ImGui frame).
    void draw();

    // Request graceful shutdown.
    void request_shutdown() { shutdown_requested_.store(true, std::memory_order_relaxed); }
    bool shutdown_requested() const
    {
        return shutdown_requested_.load(std::memory_order_relaxed);
    }

    // Open a ULog file (offline analysis).
    bool open_ulog(const std::string& path);

    // Accessors.
    ULogReader&         reader()      { return reader_; }
    Px4Bridge&          bridge()      { return bridge_; }
    Px4PlotManager&     plot_manager() { return plot_mgr_; }
    const Px4AppConfig& config() const { return cfg_; }

private:
    bool open_ulog_with_dialog();
    void draw_menu_bar();
    void draw_status_bar();
    void sync_canvas_figure(bool force = false);

    Px4AppConfig cfg_;

    ULogReader      reader_;
    Px4Bridge       bridge_;
    Px4PlotManager  plot_mgr_;

    std::unique_ptr<ULogFilePanel>       file_panel_;
    std::unique_ptr<LiveConnectionPanel> live_panel_;

    spectra::Figure* canvas_figure_{nullptr};
    uint64_t         last_canvas_revision_{static_cast<uint64_t>(-1)};

    std::atomic<bool> shutdown_requested_{false};

    bool show_file_panel_{true};
    bool show_live_panel_{false};
    std::string last_open_error_;
};

}   // namespace spectra::adapters::px4
