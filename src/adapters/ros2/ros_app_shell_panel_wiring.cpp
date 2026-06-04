#include "ros_app_shell.hpp"

#include "ui/node_graph_panel.hpp"

namespace spectra::adapters::ros2
{

void RosAppShell::wire_panel_callbacks()
{
    if (topic_list_)
    {
        topic_list_->set_select_callback([this](const std::string& topic)
                                         { on_topic_selected(topic); });

        topic_list_->set_plot_callback([this](const std::string& topic) { on_topic_plot(topic); });
    }

    drag_drop_ = std::make_unique<FieldDragDrop>();
    drag_drop_->set_plot_request_callback([this](const FieldDragPayload& payload, PlotTarget target)
                                          { handle_plot_request(payload, target); });

    if (topic_list_)
        topic_list_->set_drag_drop(drag_drop_.get());
    if (topic_echo_)
        topic_echo_->set_drag_drop(drag_drop_.get());

    // Wire echo panel into topic list so the monitor can show inline echo.
    if (topic_list_ && topic_echo_)
        topic_list_->set_echo_panel(topic_echo_.get());

    // Forward echo panel message arrivals to topic stats and topic list so
    // that statistics update for the selected topic even when it isn't plotted.
    if (topic_echo_)
    {
        topic_echo_->set_message_callback(
            [this](const std::string& topic, size_t bytes)
            {
                // Only forward to topic_list / topic_stats if there is no
                // active monitor subscription for this topic — the monitor
                // sub already calls notify_message, so forwarding here too
                // would double-count and inflate the displayed Hz.
                bool has_monitor = false;
                {
                    std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                    has_monitor = monitor_subs_.count(topic) > 0;
                }
                if (!has_monitor)
                {
                    if (topic_stats_)
                        topic_stats_->notify_message(topic, bytes, -1);
                    if (topic_list_)
                        topic_list_->notify_message(topic, bytes);
                }
            });
    }

    if (node_graph_panel_)
    {
        node_graph_panel_->set_select_callback(
            [this](const GraphNode& n)
            {
                if (n.kind == GraphNodeKind::Topic)
                {
                    on_topic_selected(n.id);
                }
                else if (param_editor_)
                {
                    param_editor_->set_target_node(n.id);
                    show_param_editor_ = true;
                }
            });

        node_graph_panel_->set_activate_callback(
            [this](const GraphNode& n)
            {
                if (n.kind == GraphNodeKind::Topic)
                    add_topic_plot(n.id);
            });
    }
}

void RosAppShell::handle_plot_request(const FieldDragPayload& payload, PlotTarget target)
{
    if (!payload.valid())
        return;

    std::string topic_field = payload.topic_name;
    if (!payload.field_path.empty())
        topic_field += ':' + payload.field_path;

    // If workspace has an active subplot selection, target that slot.
    if (target == PlotTarget::CurrentAxes && workspace_state_.active_subplot_idx > 0
        && subplot_mgr_)
    {
        const int   slot  = workspace_state_.active_subplot_idx;
        std::string field = payload.field_path;
        if (field.empty())
            field = default_numeric_field(payload.topic_name, payload.type_name);
        if (!field.empty())
        {
            subplot_mgr_->add_plot(slot, payload.topic_name, field, payload.type_name);
            show_plot_area_ = true;
            return;
        }
    }

    add_topic_plot(topic_field);
}

}   // namespace spectra::adapters::ros2
