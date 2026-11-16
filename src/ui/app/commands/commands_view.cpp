// commands_view.cpp — View command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "core/axes_data_extent.hpp"
#include "core/axis_scale.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/camera/view_presets.hpp"
#include "ui/commands/undoable_property.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #include "ui/input/input.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace spectra
{

std::vector<CommandDescriptor> make_view_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto&  ui_ctx           = ctx.ui_ctx;
    auto*& active_figure    = *ctx.active_figure;
    auto&  imgui_ui         = ui_ctx.imgui_ui;
    auto&  data_interaction = ui_ctx.data_interaction;
    auto&  dock_system      = ui_ctx.dock_system;
    auto&  undo_mgr         = ui_ctx.undo_mgr;
    auto&  fig_mgr          = *ui_ctx.fig_mgr;
    auto&  input_handler    = ui_ctx.input_handler;
    auto&  anim_controller  = ui_ctx.anim_controller;

    // ─── Reset View ──────────────────────────────────────────────────────
    cmds.push_back({"view.reset",
                    "Reset View",
                    "R",
                    "View",
                    static_cast<uint16_t>(ui::Icon::Home),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        auto before = capture_figure_axes(*active_figure);
                        for (auto& ax : active_figure->axes_mut())
                        {
                            if (!ax)
                                continue;
                            auto old_xlim = ax->x_limits();
                            auto old_ylim = ax->y_limits();
                            ax->auto_fit();
                            AxisLimits target_x = ax->x_limits();
                            AxisLimits target_y = ax->y_limits();
                            ax->xlim(old_xlim.min, old_xlim.max);
                            ax->ylim(old_ylim.min, old_ylim.max);
                            anim_controller.animate_axis_limits(*ax,
                                                                target_x,
                                                                target_y,
                                                                0.25f,
                                                                ease::ease_out);
                        }
                        for (auto& ax_base : active_figure->all_axes_mut())
                        {
                            if (!ax_base)
                                continue;
                            if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                                ax3d->auto_fit();
                        }
                        auto after = capture_figure_axes(*active_figure);
                        undo_mgr.push(UndoAction{"Reset view",
                                                 [before]() { restore_figure_axes(before); },
                                                 [after]() { restore_figure_axes(after); }});
                    }});

    // ─── Auto-Fit ────────────────────────────────────────────────────────
    cmds.push_back({"view.autofit",
                    "Auto-Fit Active Axes",
                    "A",
                    "View",
                    0,
                    [&]()
                    {
                        if (auto* ax3d = dynamic_cast<Axes3D*>(input_handler.active_axes_base()))
                        {
                            ax3d->auto_fit();
                        }
                        else if (auto* ax = input_handler.active_axes())
                        {
                            auto old_x = ax->x_limits();
                            auto old_y = ax->y_limits();
                            ax->auto_fit();
                            auto new_x = ax->x_limits();
                            auto new_y = ax->y_limits();
                            undo_mgr.push(UndoAction{"Auto-fit axes",
                                                     [ax, old_x, old_y]()
                                                     {
                                                         ax->xlim(old_x.min, old_x.max);
                                                         ax->ylim(old_y.min, old_y.max);
                                                     },
                                                     [ax, new_x, new_y]()
                                                     {
                                                         ax->xlim(new_x.min, new_x.max);
                                                         ax->ylim(new_y.min, new_y.max);
                                                     }});
                        }
                    }});

    // ─── Toggle Grid ─────────────────────────────────────────────────────
    cmds.push_back(
        {"view.toggle_grid",
         "Toggle Grid",
         "G",
         "View",
         static_cast<uint16_t>(ui::Icon::Grid),
         [&]()
         {
             if (!active_figure)
                 return;
             undoable_toggle_grid_all(&undo_mgr, *active_figure);
             undo_mgr.begin_group("Toggle 3D grid");
             for (auto& ax_base : active_figure->all_axes_mut())
             {
                 if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                 {
                     auto old_planes = ax3d->grid_planes();
                     bool was_on     = (old_planes != Axes3D::GridPlane::None);
                     auto new_planes = was_on ? Axes3D::GridPlane::None : Axes3D::GridPlane::All;
                     ax3d->grid_planes(new_planes);
                     Axes3D* ax = ax3d;
                     undo_mgr.push(UndoAction{was_on ? "Hide 3D grid" : "Show 3D grid",
                                              [ax, old_planes]() { ax->grid_planes(old_planes); },
                                              [ax, new_planes]() { ax->grid_planes(new_planes); }});
                 }
             }
             undo_mgr.end_group();
         }});

    // ─── Toggle Crosshair ────────────────────────────────────────────────
    cmds.push_back({"view.toggle_crosshair",
                    "Toggle Crosshair",
                    "C",
                    "View",
                    static_cast<uint16_t>(ui::Icon::Crosshair),
                    [&]()
                    {
                        if (data_interaction)
                        {
                            bool old_val = data_interaction->crosshair_active();
                            data_interaction->toggle_crosshair();
                            bool new_val = data_interaction->crosshair_active();
                            undo_mgr.push(
                                UndoAction{new_val ? "Show crosshair" : "Hide crosshair",
                                           [&data_interaction, old_val]()
                                           {
                                               if (data_interaction)
                                                   data_interaction->set_crosshair(old_val);
                                           },
                                           [&data_interaction, new_val]()
                                           {
                                               if (data_interaction)
                                                   data_interaction->set_crosshair(new_val);
                                           }});
                        }
                    }});

    // ─── Toggle Legend ───────────────────────────────────────────────────
    cmds.push_back({"view.toggle_legend",
                    "Toggle Legend",
                    "L",
                    "View",
                    static_cast<uint16_t>(ui::Icon::Eye),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        undoable_toggle_legend(&undo_mgr, *active_figure);
                    }});

    // ─── Toggle Border ───────────────────────────────────────────────────
    cmds.push_back({"view.toggle_border",
                    "Toggle Border",
                    "B",
                    "View",
                    0,
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        undoable_toggle_border_all(&undo_mgr, *active_figure);
                        undo_mgr.begin_group("Toggle 3D border");
                        for (auto& ax_base : active_figure->all_axes_mut())
                        {
                            if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                            {
                                bool old_val = ax3d->show_bounding_box();
                                bool new_val = !old_val;
                                ax3d->show_bounding_box(new_val);
                                Axes3D* ax = ax3d;
                                undo_mgr.push(UndoAction{
                                    new_val ? "Show 3D bounding box" : "Hide 3D bounding box",
                                    [ax, old_val]() { ax->show_bounding_box(old_val); },
                                    [ax, new_val]() { ax->show_bounding_box(new_val); }});
                            }
                        }
                        undo_mgr.end_group();
                    }});

    // ─── Toggle Fullscreen ───────────────────────────────────────────────
    cmds.push_back(
        {"view.fullscreen",
         "Toggle Fullscreen Canvas",
         "F",
         "View",
         static_cast<uint16_t>(ui::Icon::Fullscreen),
         [&]()
         {
             if (imgui_ui)
             {
                 auto& lm            = imgui_ui->get_layout_manager();
                 bool  old_inspector = lm.is_inspector_visible();
                 bool  old_nav       = lm.is_nav_rail_expanded();
                 bool  all_hidden    = !old_inspector && !old_nav;
                 bool  new_inspector = all_hidden;
                 bool  new_nav       = all_hidden;
                 lm.set_inspector_visible(new_inspector);
                 lm.set_nav_rail_expanded(new_nav);
                 undo_mgr.push(UndoAction{
                     "Toggle fullscreen",
                     [&imgui_ui, old_inspector, old_nav]()
                     {
                         if (imgui_ui)
                         {
                             imgui_ui->get_layout_manager().set_inspector_visible(old_inspector);
                             imgui_ui->get_layout_manager().set_nav_rail_expanded(old_nav);
                         }
                     },
                     [&imgui_ui, new_inspector, new_nav]()
                     {
                         if (imgui_ui)
                         {
                             imgui_ui->get_layout_manager().set_inspector_visible(new_inspector);
                             imgui_ui->get_layout_manager().set_nav_rail_expanded(new_nav);
                         }
                     }});
             }
         }});

    // ─── Home ────────────────────────────────────────────────────────────
    cmds.push_back({"view.home",
                    "Home (Restore Original View)",
                    "Home",
                    "View",
                    static_cast<uint16_t>(ui::Icon::Home),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        auto  before    = capture_figure_axes(*active_figure);
                        auto& active_vm = fig_mgr.active_state();
                        for (auto& ax : active_figure->axes_mut())
                        {
                            if (!ax)
                                continue;
                            auto it = active_vm.home_limits().find(ax.get());
                            if (it != active_vm.home_limits().end())
                            {
                                ax->xlim(it->second.x.min, it->second.x.max);
                                ax->ylim(it->second.y.min, it->second.y.max);
                            }
                            else
                            {
                                ax->auto_fit();
                            }
                        }
                        for (auto& ax_base : active_figure->all_axes_mut())
                        {
                            if (ax_base)
                            {
                                if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                                    ax3d->auto_fit();
                            }
                        }
                        auto after = capture_figure_axes(*active_figure);
                        undo_mgr.push(UndoAction{"Restore original view",
                                                 [before]() { restore_figure_axes(before); },
                                                 [after]() { restore_figure_axes(after); }});
                    }});

    // ─── Zoom helper ─────────────────────────────────────────────────────
    // Zoom by `factor` (new span / old span) about the centre of the visible
    // data of every 2D series type (detail::data_center), falling back to the
    // view centre.  Works in axis space (log axes zoom by ratios) and in
    // double precision (epoch-scale limits).
    auto zoom_about_data_center = [](Axes& ax, double factor) -> std::pair<AxisLimits, AxisLimits>
    {
        // No data: NaN centre keeps the current view centre.
        double cx = std::numeric_limits<double>::quiet_NaN();
        double cy = cx;
        detail::data_center(ax, cx, cy);
        return {zoom_limits_centered(ax.xscale(), ax.x_limits(), cx, factor),
                zoom_limits_centered(ax.yscale(), ax.y_limits(), cy, factor)};
    };

    // ─── Keyboard view change ────────────────────────────────────────────
    // Set ax's limits, move every axes linked to it the way a mouse drag does
    // (AxisLinkManager), and push one undo entry restoring all of them.
    auto& axis_link_mgr = ui_ctx.axis_link_mgr;
    auto  set_view_limits =
        [&undo_mgr, &axis_link_mgr](Axes& ax, AxisLimits new_x, AxisLimits new_y, const char* label)
    {
        struct Snapshot
        {
            Axes*      axes;
            AxisLimits x;
            AxisLimits y;
        };
        std::vector<Axes*> touched{&ax};
        for (Axes* peer : axis_link_mgr.linked_peers(&ax))
            if (peer && peer != &ax)
                touched.push_back(peer);

        auto capture = [&touched]()
        {
            std::vector<Snapshot> snap;
            snap.reserve(touched.size());
            for (Axes* a : touched)
                snap.push_back({a, a->x_limits(), a->y_limits()});
            return snap;
        };
        auto restore = [](const std::vector<Snapshot>& snap)
        {
            for (const auto& s : snap)
            {
                s.axes->xlim(s.x.min, s.x.max);
                s.axes->ylim(s.y.min, s.y.max);
            }
        };

        auto before = capture();
        ax.xlim(new_x.min, new_x.max);
        ax.ylim(new_y.min, new_y.max);
        axis_link_mgr.propagate_limits(&ax, ax.x_limits(), ax.y_limits());
        auto after = capture();
        undo_mgr.push(UndoAction{label,
                                 [before, restore]() { restore(before); },
                                 [after, restore]() { restore(after); }});
    };

    // Pan by `fx` / `fy` of the visible span, in axis space: a log axis moves
    // by a ratio (never to zero or below), like a mouse drag.
    auto pan_view = [&input_handler, set_view_limits](double fx, double fy)
    {
        auto* ax = input_handler.active_axes();
        if (!ax)
            return;
        set_view_limits(*ax,
                        pan_limits(ax->xscale(), ax->x_limits(), fx),
                        pan_limits(ax->yscale(), ax->y_limits(), fy),
                        "Pan view");
    };

    // ─── Zoom In ─────────────────────────────────────────────────────────
    cmds.push_back({"view.zoom_in",
                    "Zoom In",
                    "",
                    "View",
                    static_cast<uint16_t>(ui::Icon::ZoomIn),
                    [&, zoom_about_data_center, set_view_limits]()
                    {
                        if (auto* ax3d = dynamic_cast<Axes3D*>(input_handler.active_axes_base()))
                        {
                            ax3d->zoom_limits(0.75f);
                        }
                        else if (auto* ax = input_handler.active_axes())
                        {
                            const auto [new_x, new_y] = zoom_about_data_center(*ax, 0.75);
                            set_view_limits(*ax, new_x, new_y, "Zoom in");
                        }
                    }});

    // ─── Zoom Out ────────────────────────────────────────────────────────
    cmds.push_back({"view.zoom_out",
                    "Zoom Out",
                    "",
                    "View",
                    0,
                    [&, zoom_about_data_center, set_view_limits]()
                    {
                        if (auto* ax3d = dynamic_cast<Axes3D*>(input_handler.active_axes_base()))
                        {
                            ax3d->zoom_limits(1.25f);
                        }
                        else if (auto* ax = input_handler.active_axes())
                        {
                            const auto [new_x, new_y] = zoom_about_data_center(*ax, 1.25);
                            set_view_limits(*ax, new_x, new_y, "Zoom out");
                        }
                    }});

    // ─── Pan commands ────────────────────────────────────────────────────
    cmds.push_back(
        {"view.pan_left", "Pan Left", "Left", "View", 0, [pan_view]() { pan_view(-0.1, 0.0); }});

    cmds.push_back(
        {"view.pan_right", "Pan Right", "Right", "View", 0, [pan_view]() { pan_view(0.1, 0.0); }});

    cmds.push_back(
        {"view.pan_up", "Pan Up", "Up", "View", 0, [pan_view]() { pan_view(0.0, 0.1); }});

    cmds.push_back(
        {"view.pan_down", "Pan Down", "Down", "View", 0, [pan_view]() { pan_view(0.0, -0.1); }});

    // ─── 3D camera views ─────────────────────────────────────────────────
    // Apply `fn` to the 3D axes under the cursor, or else to every 3D axes
    // of the active figure, as one undo step.
    auto for_target_axes3d = [&](const char* group, auto&& fn)
    {
        if (auto* ax3d = dynamic_cast<Axes3D*>(input_handler.active_axes_base()))
        {
            fn(ax3d);
            return;
        }
        if (!active_figure)
            return;
        undo_mgr.begin_group(group);
        for (auto& ax_base : active_figure->all_axes_mut())
        {
            if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                fn(ax3d);
        }
        undo_mgr.end_group();
    };

    for (const ViewPresetInfo& info : VIEW_PRESETS)
    {
        const Camera::ViewPreset preset = info.preset;
        cmds.push_back({info.command_id,
                        info.label,
                        info.shortcut,
                        "View",
                        0,
                        [&, for_target_axes3d, preset]()
                        {
                            for_target_axes3d(
                                "3D view preset",
                                [&](Axes3D* ax)
                                { input_handler.apply_axes3d_view_preset(ax, preset); });
                        }});
    }

    cmds.push_back({"view.3d_toggle_projection",
                    "3D View: Toggle Perspective / Orthographic",
                    "Alt+0",
                    "View",
                    0,
                    [&, for_target_axes3d]()
                    {
                        for_target_axes3d("Toggle 3D projection",
                                          [&](Axes3D* ax)
                                          { input_handler.toggle_axes3d_projection(ax); });
                    }});

    cmds.push_back({"view.3d_equal_aspect",
                    "3D View: Toggle Equal Aspect (Axis Equal)",
                    "",
                    "View",
                    0,
                    [&, for_target_axes3d]()
                    {
                        for_target_axes3d("Toggle 3D equal aspect",
                                          [&](Axes3D* ax)
                                          {
                                              const bool old_val = ax->equal_aspect();
                                              const bool new_val = !old_val;
                                              ax->equal_aspect(new_val);
                                              undo_mgr.push(UndoAction{
                                                  new_val ? "Equal 3D aspect" : "Free 3D aspect",
                                                  [ax, old_val]() { ax->equal_aspect(old_val); },
                                                  [ax, new_val]() { ax->equal_aspect(new_val); }});
                                          });
                    }});

    // ─── Split commands ──────────────────────────────────────────────────
    auto do_split = [&](SplitDirection dir)
    {
        if (dock_system.is_split())
        {
            SplitPane* active_pane = dock_system.split_view().active_pane();
            if (!active_pane || active_pane->figure_count() < 2)
                return;

            size_t active_local = active_pane->active_local_index();
            size_t move_local   = (active_local + 1) % active_pane->figure_count();
            size_t move_fig     = active_pane->figure_indices()[move_local];

            active_pane->remove_figure(move_fig);

            size_t     active_fig = active_pane->figure_index();
            SplitPane* new_pane   = nullptr;
            if (dir == SplitDirection::Horizontal)
                new_pane = dock_system.split_figure_right(active_fig, move_fig);
            else
                new_pane = dock_system.split_figure_down(active_fig, move_fig);

            (void)new_pane;
        }
        else
        {
            if (fig_mgr.count() < 2)
                return;

            FigureId orig_active = fig_mgr.active_index();

            FigureId move_fig = INVALID_FIGURE_ID;
            for (auto id : fig_mgr.figure_ids())
            {
                if (id != orig_active)
                {
                    move_fig = id;
                    break;
                }
            }
            if (move_fig == INVALID_FIGURE_ID)
                return;

            SplitPane* new_pane = nullptr;
            if (dir == SplitDirection::Horizontal)
                new_pane = dock_system.split_figure_right(orig_active, move_fig);
            else
                new_pane = dock_system.split_figure_down(orig_active, move_fig);

            if (new_pane)
            {
                SplitPane* root       = dock_system.split_view().root();
                SplitPane* first_pane = root ? root->first() : nullptr;
                if (first_pane && first_pane->is_leaf())
                {
                    if (first_pane->has_figure(move_fig))
                    {
                        first_pane->remove_figure(move_fig);
                    }
                    for (auto id : fig_mgr.figure_ids())
                    {
                        if (id == move_fig)
                            continue;
                        if (!first_pane->has_figure(id))
                        {
                            first_pane->add_figure(id);
                        }
                    }
                    for (size_t li = 0; li < first_pane->figure_indices().size(); ++li)
                    {
                        if (first_pane->figure_indices()[li] == orig_active)
                        {
                            first_pane->set_active_local_index(li);
                            break;
                        }
                    }
                }
            }

            dock_system.set_active_figure_index(orig_active);
        }
    };

    cmds.push_back({"view.split_right", "Split Right", "Ctrl+\\", "View", 0, [&, do_split]() {
                        do_split(SplitDirection::Horizontal);
                    }});

    cmds.push_back({"view.split_down", "Split Down", "Ctrl+Shift+\\", "View", 0, [&, do_split]() {
                        do_split(SplitDirection::Vertical);
                    }});

    cmds.push_back({"view.close_split",
                    "Close Split Pane",
                    "",
                    "View",
                    0,
                    [&]()
                    {
                        if (dock_system.is_split())
                        {
                            dock_system.close_split(dock_system.active_figure_index());
                        }
                    }});

    cmds.push_back({"view.reset_splits", "Reset All Splits", "", "View", 0, [&]() {
                        dock_system.reset_splits();
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
