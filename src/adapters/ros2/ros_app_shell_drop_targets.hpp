#pragma once

// ImGui drag-drop targets for the ROS app shell Plot Area (per-slot + global).

#include <functional>
#include <string>

#include "subplot_manager.hpp"
#include "ui/field_drag_drop.hpp"

namespace spectra::adapters::ros2
{

constexpr float kPlotAreaGlobalDropHeight = 44.0f;

struct RosPlotDropTargetContext
{
    FieldDragDrop*  drag_drop   = nullptr;
    SubplotManager* subplot_mgr = nullptr;

    std::function<std::string(const std::string& topic, const std::string& type)>
        default_numeric_field;
};

// Invisible drop region for one subplot slot (expands to fill remaining slot height).
void draw_subplot_slot_drop_target(const RosPlotDropTargetContext& ctx,
                                   int                             slot,
                                   float                           drop_width,
                                   float                           remaining_height);

// Global drop zone at the bottom of the Plot Area (new subplot row).
void draw_global_plot_drop_target(const RosPlotDropTargetContext& ctx, float width, float height);

}   // namespace spectra::adapters::ros2
