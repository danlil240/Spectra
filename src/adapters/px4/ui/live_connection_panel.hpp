#pragma once

// LiveConnectionPanel — ImGui panel for real-time PX4/MAVLink telemetry.
//
// Provides controls for connecting to a PX4 instance via UDP, displays
// connection status, message rates, and allows selecting channels/fields
// to add to the live plot.

#include "../px4_bridge.hpp"
#include "../px4_plot_manager.hpp"

#include <ui/panel/panel_detach_controller.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace spectra
{
class WindowManager;
}

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// LiveConnectionPanel
// ---------------------------------------------------------------------------

class LiveConnectionPanel
{
   public:
    explicit LiveConnectionPanel(Px4Bridge& bridge, Px4PlotManager& plot_mgr);
    ~LiveConnectionPanel();

    LiveConnectionPanel(const LiveConnectionPanel&)            = delete;
    LiveConnectionPanel& operator=(const LiveConnectionPanel&) = delete;

    /// Draw the panel — handles ImGui Begin/End and controller state.
    void draw(bool* p_open = nullptr);

    /// Process deferred OS window create/destroy.
    void process_pending() { detach_ctrl_.process_pending(); }

    /// Wire WindowManager for OS-level panel tearoff.
    void set_window_manager(spectra::WindowManager* wm) { detach_ctrl_.set_window_manager(wm); }

    /// Access the detach controller.
    spectra::PanelDetachController&       detach_controller() { return detach_ctrl_; }
    const spectra::PanelDetachController& detach_controller() const { return detach_ctrl_; }

    // Convenience forwarders.
    const std::string& title() const { return detach_ctrl_.title(); }
    void               set_dock_id(uint32_t id) { detach_ctrl_.set_dock_id(id); }
    bool               is_detached() const { return detach_ctrl_.is_detached(); }
    void               detach() { detach_ctrl_.detach(); }
    void               attach() { detach_ctrl_.attach(); }

   private:
    void draw_content();
    void draw_context_menu();
    void draw_connection_controls();
    void draw_status();
    void draw_channel_list();

    spectra::PanelDetachController detach_ctrl_;

    // Track docked state for undock-transition detection.
    bool was_docked_{true};

    Px4Bridge&      bridge_;
    Px4PlotManager& plot_mgr_;

    // Connection config.
    char host_buf_[64]{"127.0.0.1"};
    int  port_{14540};
};

}   // namespace spectra::adapters::px4
