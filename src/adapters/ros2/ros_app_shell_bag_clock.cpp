#include "ros_app_shell.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

void RosAppShell::wire_bag_time_clock()
{
    if (!bag_player_)
        return;

    bag_player_->set_on_playhead(
        [this](double playhead_sec)
        {
            if (!bag_player_ || !bag_player_->is_open())
                return;
            workspace_state_.clock.update_bag_transport(playhead_sec,
                                                        bag_player_->is_playing(),
                                                        bag_player_->rate());
            sync_playhead_to_panels(playhead_sec);
        });

    bag_player_->set_on_state_change(
        [this](PlayerState state)
        { workspace_state_.clock.is_playing = (state == PlayerState::Playing); });

#ifdef SPECTRA_ROS2_BAG
    bag_player_->set_on_raw_message(
        [this](const BagMessage& msg)
        {
            if (!topic_echo_ || !bag_player_ || !bag_player_->is_open())
                return;
            topic_echo_->ingest_bag_message(msg, *intr_, bag_player_->metadata().start_time_ns);
        });
#endif
}

void RosAppShell::on_bag_opened(const std::string& path)
{
    if (!bag_player_ || !bag_player_->is_open())
        return;

    workspace_state_.clock.enter_bag_mode(bag_player_->duration_sec(),
                                          bag_player_->metadata().start_time_ns);

    if (subplot_mgr_)
    {
        subplot_mgr_->set_shared_time_origin(0.0);
        subplot_mgr_->resume_all_scroll();
        subplot_mgr_->set_now(0.0);
    }

    if (topic_echo_)
    {
        topic_echo_->set_bag_echo_mode(true);
        topic_echo_->set_bag_playhead(0.0);
    }

    if (bag_player_)
    {
        bag_player_->set_rate(cfg_.bag_rate);
        bag_player_->set_loop(cfg_.bag_loop);
    }

    if (bag_display_sync_)
        bag_display_sync_->open(path);

    show_bag_playback_ = true;
}

void RosAppShell::on_bag_closed()
{
    workspace_state_.clock.enter_live_mode();

    if (topic_echo_)
        topic_echo_->set_bag_echo_mode(false);

    if (bag_display_sync_)
        bag_display_sync_->close();
}

void RosAppShell::sync_playhead_to_panels(double playhead_sec)
{
    if (subplot_mgr_ && workspace_state_.clock.is_bag_mode())
        subplot_mgr_->set_now(playhead_sec);

    if (topic_echo_ && workspace_state_.clock.is_bag_mode())
        topic_echo_->set_bag_playhead(playhead_sec);
}

void RosAppShell::draw_unified_transport_bar()
{
#ifdef SPECTRA_USE_IMGUI
    if (!bag_playback_panel_ || !bag_player_ || !bag_player_->is_open())
        return;

    const ImGuiViewport* vp    = ImGui::GetMainViewport();
    const float          bar_h = 64.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - bar_h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, bar_h));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus
                                   | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::Begin("##spectra_ros_transport_bar", nullptr, flags))
        bag_playback_panel_->draw_inline();
    ImGui::End();
    ImGui::PopStyleVar(2);
#else
    (void)0;
#endif
}

}   // namespace spectra::adapters::ros2
