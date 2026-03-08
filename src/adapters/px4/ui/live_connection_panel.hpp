#pragma once

// LiveConnectionPanel — ImGui panel for real-time PX4/MAVLink telemetry.
//
// Provides controls for connecting to a PX4 instance via UDP, displays
// connection status, message rates, and allows selecting channels/fields
// to add to the live plot.

#include "../px4_bridge.hpp"
#include "../px4_plot_manager.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

    // Draw the panel.
    void draw(bool* p_open = nullptr);

    const std::string& title() const { return title_; }
    void set_title(const std::string& t) { title_ = t; }

private:
    void draw_connection_controls();
    void draw_status();
    void draw_channel_list();

    Px4Bridge&      bridge_;
    Px4PlotManager& plot_mgr_;

    std::string title_{"PX4 Live"};

    // Connection config.
    char host_buf_[64]{"127.0.0.1"};
    int  port_{14540};
};

}   // namespace spectra::adapters::px4
