#include "live_connection_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

#include <cstdio>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LiveConnectionPanel::LiveConnectionPanel(Px4Bridge& bridge, Px4PlotManager& plot_mgr)
    : DetachablePanel("PX4 Live")
    , bridge_(bridge)
    , plot_mgr_(plot_mgr)
{
    set_detached_size(450.0f, 350.0f);
}

LiveConnectionPanel::~LiveConnectionPanel() = default;

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_content()
{
#ifdef SPECTRA_USE_IMGUI
    draw_connection_controls();
    ImGui::Separator();
    draw_status();
    ImGui::Separator();
    draw_channel_list();
#endif
}

// ---------------------------------------------------------------------------
// draw_connection_controls
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_connection_controls()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::Text("MAVLink UDP Connection");

    ImGui::InputText("Host", host_buf_, sizeof(host_buf_));
    ImGui::InputInt("Port", &port_);

    if (port_ < 1)
        port_ = 1;
    if (port_ > 65535)
        port_ = 65535;

    if (!bridge_.is_connected())
    {
        if (ImGui::Button("Connect"))
        {
            bridge_.init(host_buf_, static_cast<uint16_t>(port_));
            bridge_.start();
        }
    }
    else
    {
        if (ImGui::Button("Disconnect"))
        {
            bridge_.shutdown();
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_status
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_status()
{
#ifdef SPECTRA_USE_IMGUI
    auto state = bridge_.state();
    ImGui::Text("State: %s", bridge_state_name(state));
    ImGui::Text("Messages: %lu", static_cast<unsigned long>(bridge_.total_messages()));
    ImGui::Text("Rate: %.1f msg/s", bridge_.message_rate());

    if (!bridge_.last_error().empty())
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s",
                           bridge_.last_error().c_str());
#endif
}

// ---------------------------------------------------------------------------
// draw_channel_list
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_channel_list()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::Text("Active Channels");
    ImGui::BeginChild("Channels", ImVec2(0, 0), true);

    auto channels = bridge_.channel_names();
    for (auto& ch_name : channels)
    {
        auto latest = bridge_.channel_latest(ch_name);
        if (!latest)
            continue;

        char label[128];
        std::snprintf(label, sizeof(label), "%s (%zu fields)",
                      ch_name.c_str(), latest->fields.size());

        if (ImGui::TreeNode(label))
        {
            for (auto& field : latest->fields)
            {
                char field_label[256];
                std::snprintf(field_label, sizeof(field_label),
                              "%s = %.4f", field.name.c_str(), field.value);

                if (ImGui::Selectable(field_label, false,
                                       ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        plot_mgr_.add_live_field(ch_name, field.name);
                    }
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
#endif
}

}   // namespace spectra::adapters::px4
