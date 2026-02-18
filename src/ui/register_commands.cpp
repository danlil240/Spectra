// register_commands.cpp — Shared command registration for both in-process
// and multi-process (agent) windows.  Extracted from app_inproc.cpp so that
// every Spectra window gets the exact same commands, shortcuts, and UI.

#include "register_commands.hpp"

#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "figure_registry.hpp"
#include "session_runtime.hpp"
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #include "window_manager.hpp"
    #include "../render/vulkan/window_context.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include "icons.hpp"
    #include "theme.hpp"
    #include "undoable_property.hpp"
    #include "workspace.hpp"
#endif

#include <algorithm>
#include <string>

namespace spectra
{

void register_standard_commands(const CommandBindings& b)
{
#ifdef SPECTRA_USE_IMGUI
    if (!b.ui_ctx || !b.registry || !b.active_figure || !b.active_figure_id)
        return;

    auto& ui_ctx          = *b.ui_ctx;
    auto& registry        = *b.registry;
    auto*& active_figure  = *b.active_figure;
    auto& active_figure_id = *b.active_figure_id;

    auto& imgui_ui        = ui_ctx.imgui_ui;
    auto& data_interaction = ui_ctx.data_interaction;
    auto& dock_system     = ui_ctx.dock_system;
    auto& timeline_editor = ui_ctx.timeline_editor;
    auto& mode_transition = ui_ctx.mode_transition;
    auto& is_in_3d_mode   = ui_ctx.is_in_3d_mode;
    auto& saved_3d_camera = ui_ctx.saved_3d_camera;
    auto& home_limits     = ui_ctx.home_limits;
    auto& cmd_registry    = ui_ctx.cmd_registry;
    auto& shortcut_mgr    = ui_ctx.shortcut_mgr;
    auto& undo_mgr        = ui_ctx.undo_mgr;
    auto& cmd_palette     = ui_ctx.cmd_palette;
    auto& fig_mgr         = *ui_ctx.fig_mgr;
    auto& input_handler   = ui_ctx.input_handler;
    auto& anim_controller = ui_ctx.anim_controller;

    SessionRuntime* session = b.session;
#ifdef SPECTRA_USE_GLFW
    WindowManager* window_mgr = b.window_mgr;
#endif

    // ─── View commands ───────────────────────────────────────────────────
    cmd_registry.register_command(
        "view.reset",
        "Reset View",
        [&]()
        {
            auto before = capture_figure_axes(*active_figure);
            for (auto& ax : active_figure->axes_mut())
            {
                if (ax)
                {
                    auto old_xlim = ax->x_limits();
                    auto old_ylim = ax->y_limits();
                    ax->auto_fit();
                    AxisLimits target_x = ax->x_limits();
                    AxisLimits target_y = ax->y_limits();
                    ax->xlim(old_xlim.min, old_xlim.max);
                    ax->ylim(old_ylim.min, old_ylim.max);
                    anim_controller.animate_axis_limits(
                        *ax, target_x, target_y, 0.25f, ease::ease_out);
                }
            }
            auto after = capture_figure_axes(*active_figure);
            undo_mgr.push(UndoAction{"Reset view",
                                     [before]() { restore_figure_axes(before); },
                                     [after]() { restore_figure_axes(after); }});
        },
        "R",
        "View",
        static_cast<uint16_t>(ui::Icon::Home));

    cmd_registry.register_command(
        "view.autofit",
        "Auto-Fit Active Axes",
        [&]()
        {
            if (auto* ax = input_handler.active_axes())
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
        },
        "A",
        "View");

    cmd_registry.register_command(
        "view.toggle_grid",
        "Toggle Grid",
        [&]() { undoable_toggle_grid_all(&undo_mgr, *active_figure); },
        "G",
        "View",
        static_cast<uint16_t>(ui::Icon::Grid));

    cmd_registry.register_command(
        "view.toggle_crosshair",
        "Toggle Crosshair",
        [&]()
        {
            if (data_interaction)
            {
                bool old_val = data_interaction->crosshair_active();
                data_interaction->toggle_crosshair();
                bool new_val = data_interaction->crosshair_active();
                undo_mgr.push(UndoAction{new_val ? "Show crosshair" : "Hide crosshair",
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
        },
        "C",
        "View",
        static_cast<uint16_t>(ui::Icon::Crosshair));

    cmd_registry.register_command(
        "view.toggle_legend",
        "Toggle Legend",
        [&]() { undoable_toggle_legend(&undo_mgr, *active_figure); },
        "L",
        "View",
        static_cast<uint16_t>(ui::Icon::Eye));

    cmd_registry.register_command(
        "view.toggle_border",
        "Toggle Border",
        [&]() { undoable_toggle_border_all(&undo_mgr, *active_figure); },
        "B",
        "View");

    cmd_registry.register_command(
        "view.fullscreen",
        "Toggle Fullscreen Canvas",
        [&]()
        {
            if (imgui_ui)
            {
                auto& lm = imgui_ui->get_layout_manager();
                bool old_inspector = lm.is_inspector_visible();
                bool old_nav = lm.is_nav_rail_expanded();
                bool all_hidden = !old_inspector && !old_nav;
                bool new_inspector = all_hidden;
                bool new_nav = all_hidden;
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
        },
        "F",
        "View",
        static_cast<uint16_t>(ui::Icon::Fullscreen));

    cmd_registry.register_command(
        "view.home",
        "Home (Restore Original View)",
        [&]()
        {
            auto before = capture_figure_axes(*active_figure);
            for (auto& ax : active_figure->axes_mut())
            {
                if (!ax) continue;
                auto it = home_limits.find(ax.get());
                if (it != home_limits.end())
                {
                    ax->xlim(it->second.x.min, it->second.x.max);
                    ax->ylim(it->second.y.min, it->second.y.max);
                }
                else
                {
                    ax->auto_fit();
                }
            }
            auto after = capture_figure_axes(*active_figure);
            undo_mgr.push(UndoAction{"Restore original view",
                                     [before]() { restore_figure_axes(before); },
                                     [after]() { restore_figure_axes(after); }});
        },
        "Home",
        "View",
        static_cast<uint16_t>(ui::Icon::Home));

    cmd_registry.register_command(
        "view.zoom_in",
        "Zoom In",
        [&]()
        {
            if (auto* ax = input_handler.active_axes())
            {
                auto old_x = ax->x_limits();
                auto old_y = ax->y_limits();
                float xc = (old_x.min + old_x.max) * 0.5f,
                      xr = (old_x.max - old_x.min) * 0.375f;
                float yc = (old_y.min + old_y.max) * 0.5f,
                      yr = (old_y.max - old_y.min) * 0.375f;
                AxisLimits new_x{xc - xr, xc + xr};
                AxisLimits new_y{yc - yr, yc + yr};
                undoable_set_limits(&undo_mgr, *ax, new_x, new_y);
            }
        },
        "",
        "View",
        static_cast<uint16_t>(ui::Icon::ZoomIn));

    cmd_registry.register_command(
        "view.zoom_out",
        "Zoom Out",
        [&]()
        {
            if (auto* ax = input_handler.active_axes())
            {
                auto old_x = ax->x_limits();
                auto old_y = ax->y_limits();
                float xc = (old_x.min + old_x.max) * 0.5f,
                      xr = (old_x.max - old_x.min) * 0.625f;
                float yc = (old_y.min + old_y.max) * 0.5f,
                      yr = (old_y.max - old_y.min) * 0.625f;
                AxisLimits new_x{xc - xr, xc + xr};
                AxisLimits new_y{yc - yr, yc + yr};
                undoable_set_limits(&undo_mgr, *ax, new_x, new_y);
            }
        },
        "",
        "View");

    // Toggle 2D/3D view mode
    cmd_registry.register_command(
        "view.toggle_3d",
        "Toggle 2D/3D View",
        [&]()
        {
            Axes3D* ax3d = nullptr;
            for (auto& ax_base : active_figure->all_axes())
            {
                if (ax_base)
                {
                    ax3d = dynamic_cast<Axes3D*>(ax_base.get());
                    if (ax3d)
                        break;
                }
            }
            if (!ax3d || mode_transition.is_active())
                return;

            if (is_in_3d_mode)
            {
                saved_3d_camera = ax3d->camera();

                ModeTransition3DState from;
                from.camera = ax3d->camera();
                from.xlim = ax3d->x_limits();
                from.ylim = ax3d->y_limits();
                from.zlim = ax3d->z_limits();
                from.grid_planes = static_cast<int>(ax3d->grid_planes());

                ModeTransition2DState to;
                to.xlim = ax3d->x_limits();
                to.ylim = ax3d->y_limits();

                mode_transition.begin_to_2d(from, to);
                is_in_3d_mode = false;
                input_handler.set_orbit_locked(true);
            }
            else
            {
                ModeTransition2DState from;
                from.xlim = ax3d->x_limits();
                from.ylim = ax3d->y_limits();

                ModeTransition3DState to;
                to.camera = saved_3d_camera;
                to.xlim = ax3d->x_limits();
                to.ylim = ax3d->y_limits();
                to.zlim = ax3d->z_limits();
                to.grid_planes = static_cast<int>(ax3d->grid_planes());

                mode_transition.begin_to_3d(from, to);
                is_in_3d_mode = true;
                input_handler.set_orbit_locked(false);
            }
        },
        "3",
        "View",
        static_cast<uint16_t>(ui::Icon::Axes));

    // ─── App commands ────────────────────────────────────────────────────
    cmd_registry.register_command(
        "app.command_palette",
        "Command Palette",
        [&]() { cmd_palette.toggle(); },
        "Ctrl+K",
        "App",
        static_cast<uint16_t>(ui::Icon::Search));

    cmd_registry.register_command(
        "app.cancel",
        "Cancel / Close",
        [&]()
        {
            if (cmd_palette.is_open())
            {
                cmd_palette.close();
            }
        },
        "Escape",
        "App");

    // ─── File commands ───────────────────────────────────────────────────
    cmd_registry.register_command(
        "file.export_png",
        "Export PNG",
        [&]() { active_figure->save_png("spectra_export.png"); },
        "Ctrl+S",
        "File",
        static_cast<uint16_t>(ui::Icon::Export));

    cmd_registry.register_command(
        "file.export_svg",
        "Export SVG",
        [&]() { active_figure->save_svg("spectra_export.svg"); },
        "Ctrl+Shift+S",
        "File",
        static_cast<uint16_t>(ui::Icon::Export));

    cmd_registry.register_command(
        "file.save_workspace",
        "Save Workspace",
        [&]()
        {
            std::vector<Figure*> figs;
            for (auto id : fig_mgr.figure_ids())
            {
                Figure* f = registry.get(id);
                if (f) figs.push_back(f);
            }
            auto data =
                Workspace::capture(figs,
                                   fig_mgr.active_index(),
                                   ui::ThemeManager::instance().current_theme_name(),
                                   imgui_ui->get_layout_manager().is_inspector_visible(),
                                   imgui_ui->get_layout_manager().inspector_width(),
                                   imgui_ui->get_layout_manager().is_nav_rail_expanded());
            if (data_interaction)
            {
                data.interaction.crosshair_enabled = data_interaction->crosshair_active();
                data.interaction.tooltip_enabled = data_interaction->tooltip_active();
                for (const auto& m : data_interaction->markers())
                {
                    WorkspaceData::InteractionState::MarkerEntry me;
                    me.data_x = m.data_x;
                    me.data_y = m.data_y;
                    me.series_label = m.series ? m.series->label() : "";
                    me.point_index = m.point_index;
                    data.interaction.markers.push_back(std::move(me));
                }
            }
            for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
            {
                data.figures[i].custom_tab_title = fig_mgr.get_title(i);
                data.figures[i].is_modified = fig_mgr.is_modified(i);
            }
            data.undo_count = undo_mgr.undo_count();
            data.redo_count = undo_mgr.redo_count();
            data.dock_state = dock_system.serialize();
            Workspace::save(Workspace::default_path(), data);
        },
        "",
        "File",
        static_cast<uint16_t>(ui::Icon::Save));

    cmd_registry.register_command(
        "file.load_workspace",
        "Load Workspace",
        [&]()
        {
            WorkspaceData data;
            if (Workspace::load(Workspace::default_path(), data))
            {
                auto before_snap = capture_figure_axes(*active_figure);
                std::vector<Figure*> figs;
                for (auto id : fig_mgr.figure_ids())
                {
                    Figure* f = registry.get(id);
                    if (f) figs.push_back(f);
                }
                Workspace::apply(data, figs);
                auto after_snap = capture_figure_axes(*active_figure);
                undo_mgr.push(UndoAction{"Load workspace",
                                         [before_snap]() { restore_figure_axes(before_snap); },
                                         [after_snap]() { restore_figure_axes(after_snap); }});
                if (data_interaction)
                {
                    data_interaction->set_crosshair(data.interaction.crosshair_enabled);
                    data_interaction->set_tooltip(data.interaction.tooltip_enabled);
                }
                for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
                {
                    if (!data.figures[i].custom_tab_title.empty())
                    {
                        fig_mgr.set_title(i, data.figures[i].custom_tab_title);
                    }
                }
                if (data.active_figure_index < fig_mgr.count())
                {
                    fig_mgr.queue_switch(data.active_figure_index);
                }
                if (!data.theme_name.empty())
                {
                    ui::ThemeManager::instance().set_theme(data.theme_name);
                    ui::ThemeManager::instance().apply_to_imgui();
                }
                if (imgui_ui)
                {
                    auto& lm = imgui_ui->get_layout_manager();
                    lm.set_inspector_visible(data.panels.inspector_visible);
                    lm.set_nav_rail_expanded(data.panels.nav_rail_expanded);
                }
                if (!data.dock_state.empty())
                {
                    dock_system.deserialize(data.dock_state);
                }
            }
        },
        "",
        "File",
        static_cast<uint16_t>(ui::Icon::FolderOpen));

    // ─── Edit commands ───────────────────────────────────────────────────
    cmd_registry.register_command(
        "edit.undo",
        "Undo",
        [&]() { undo_mgr.undo(); },
        "Ctrl+Z",
        "Edit",
        static_cast<uint16_t>(ui::Icon::Undo));

    cmd_registry.register_command(
        "edit.redo",
        "Redo",
        [&]() { undo_mgr.redo(); },
        "Ctrl+Shift+Z",
        "Edit",
        static_cast<uint16_t>(ui::Icon::Redo));

    // ─── Figure management ───────────────────────────────────────────────
    cmd_registry.register_command(
        "figure.new",
        "New Figure",
        [&]() { fig_mgr.queue_create(); },
        "Ctrl+T",
        "Figure",
        static_cast<uint16_t>(ui::Icon::Plus));

    cmd_registry.register_command(
        "figure.close",
        "Close Figure",
        [&]()
        {
            if (fig_mgr.count() > 1)
            {
                fig_mgr.queue_close(fig_mgr.active_index());
            }
        },
        "Ctrl+W",
        "Figure",
        static_cast<uint16_t>(ui::Icon::Close));

    // Tab switching (1-9)
    for (int i = 0; i < 9; ++i)
    {
        cmd_registry.register_command(
            "figure.tab_" + std::to_string(i + 1),
            "Switch to Figure " + std::to_string(i + 1),
            [&fig_mgr, i]() { fig_mgr.queue_switch(static_cast<size_t>(i)); },
            std::to_string(i + 1),
            "Figure");
    }

    cmd_registry.register_command(
        "figure.next_tab",
        "Next Figure Tab",
        [&fig_mgr]() { fig_mgr.switch_to_next(); },
        "Ctrl+Tab",
        "Figure");

    cmd_registry.register_command(
        "figure.prev_tab",
        "Previous Figure Tab",
        [&fig_mgr]() { fig_mgr.switch_to_previous(); },
        "Ctrl+Shift+Tab",
        "Figure");

    // ─── Series commands ─────────────────────────────────────────────────
    cmd_registry.register_command(
        "series.cycle_selection",
        "Cycle Series Selection",
        [&]()
        {
            // Placeholder for series cycling
        },
        "Tab",
        "Series");

    // ─── Animation commands ──────────────────────────────────────────────
    cmd_registry.register_command(
        "anim.toggle_play",
        "Toggle Play/Pause",
        [&]() { timeline_editor.toggle_play(); },
        "Space",
        "Animation",
        static_cast<uint16_t>(ui::Icon::Play));

    cmd_registry.register_command(
        "anim.step_back",
        "Step Frame Back",
        [&]() { timeline_editor.step_backward(); },
        "[",
        "Animation",
        static_cast<uint16_t>(ui::Icon::StepBackward));

    cmd_registry.register_command(
        "anim.step_forward",
        "Step Frame Forward",
        [&]() { timeline_editor.step_forward(); },
        "]",
        "Animation",
        static_cast<uint16_t>(ui::Icon::StepForward));

    cmd_registry.register_command(
        "anim.stop", "Stop Playback", [&]() { timeline_editor.stop(); }, "", "Animation");

    cmd_registry.register_command(
        "anim.go_to_start",
        "Go to Start",
        [&]() { timeline_editor.set_playhead(0.0f); },
        "",
        "Animation");

    cmd_registry.register_command(
        "anim.go_to_end",
        "Go to End",
        [&]() { timeline_editor.set_playhead(timeline_editor.duration()); },
        "",
        "Animation");

    // ─── Panel toggles ───────────────────────────────────────────────────
    cmd_registry.register_command(
        "panel.toggle_timeline",
        "Toggle Timeline Panel",
        [&]()
        {
            if (imgui_ui)
            {
                imgui_ui->set_timeline_visible(!imgui_ui->is_timeline_visible());
            }
        },
        "T",
        "Panel",
        static_cast<uint16_t>(ui::Icon::Play));

    cmd_registry.register_command(
        "panel.toggle_curve_editor",
        "Toggle Curve Editor",
        [&]()
        {
            if (imgui_ui)
            {
                imgui_ui->set_curve_editor_visible(!imgui_ui->is_curve_editor_visible());
            }
        },
        "",
        "Panel");

    // ─── Theme commands ──────────────────────────────────────────────────
    cmd_registry.register_command(
        "theme.dark",
        "Switch to Dark Theme",
        [&]()
        {
            auto& tm = ui::ThemeManager::instance();
            std::string old_theme = tm.current_theme_name();
            tm.set_theme("dark");
            tm.apply_to_imgui();
            undo_mgr.push(UndoAction{"Switch to dark theme",
                                     [old_theme]()
                                     {
                                         auto& t = ui::ThemeManager::instance();
                                         t.set_theme(old_theme);
                                         t.apply_to_imgui();
                                     },
                                     []()
                                     {
                                         auto& t = ui::ThemeManager::instance();
                                         t.set_theme("dark");
                                         t.apply_to_imgui();
                                     }});
        },
        "",
        "Theme",
        static_cast<uint16_t>(ui::Icon::Moon));

    cmd_registry.register_command(
        "theme.light",
        "Switch to Light Theme",
        [&]()
        {
            auto& tm = ui::ThemeManager::instance();
            std::string old_theme = tm.current_theme_name();
            tm.set_theme("light");
            tm.apply_to_imgui();
            undo_mgr.push(UndoAction{"Switch to light theme",
                                     [old_theme]()
                                     {
                                         auto& t = ui::ThemeManager::instance();
                                         t.set_theme(old_theme);
                                         t.apply_to_imgui();
                                     },
                                     []()
                                     {
                                         auto& t = ui::ThemeManager::instance();
                                         t.set_theme("light");
                                         t.apply_to_imgui();
                                     }});
        },
        "",
        "Theme",
        static_cast<uint16_t>(ui::Icon::Sun));

    cmd_registry.register_command(
        "theme.toggle",
        "Toggle Dark/Light Theme",
        [&]()
        {
            auto& tm = ui::ThemeManager::instance();
            std::string old_theme = tm.current_theme_name();
            std::string new_theme = (old_theme == "dark") ? "light" : "dark";
            tm.set_theme(new_theme);
            tm.apply_to_imgui();
            undo_mgr.push(UndoAction{"Toggle theme",
                                     [old_theme]()
                                     {
                                         auto& t = ui::ThemeManager::instance();
                                         t.set_theme(old_theme);
                                         t.apply_to_imgui();
                                     },
                                     [new_theme]()
                                     {
                                         auto& t = ui::ThemeManager::instance();
                                         t.set_theme(new_theme);
                                         t.apply_to_imgui();
                                     }});
        },
        "",
        "Theme",
        static_cast<uint16_t>(ui::Icon::Contrast));

    // ─── Panel commands ──────────────────────────────────────────────────
    cmd_registry.register_command(
        "panel.toggle_inspector",
        "Toggle Inspector Panel",
        [&]()
        {
            if (imgui_ui)
            {
                auto& lm = imgui_ui->get_layout_manager();
                bool old_val = lm.is_inspector_visible();
                lm.set_inspector_visible(!old_val);
                undo_mgr.push(UndoAction{
                    old_val ? "Hide inspector" : "Show inspector",
                    [&imgui_ui, old_val]()
                    {
                        if (imgui_ui)
                            imgui_ui->get_layout_manager().set_inspector_visible(old_val);
                    },
                    [&imgui_ui, old_val]()
                    {
                        if (imgui_ui)
                            imgui_ui->get_layout_manager().set_inspector_visible(!old_val);
                    }});
            }
        },
        "",
        "Panel");

    cmd_registry.register_command(
        "panel.toggle_nav_rail",
        "Toggle Navigation Rail",
        [&]()
        {
            if (imgui_ui)
            {
                auto& lm = imgui_ui->get_layout_manager();
                bool old_val = lm.is_nav_rail_expanded();
                lm.set_nav_rail_expanded(!old_val);
                undo_mgr.push(UndoAction{
                    old_val ? "Collapse nav rail" : "Expand nav rail",
                    [&imgui_ui, old_val]()
                    {
                        if (imgui_ui)
                            imgui_ui->get_layout_manager().set_nav_rail_expanded(old_val);
                    },
                    [&imgui_ui, old_val]()
                    {
                        if (imgui_ui)
                            imgui_ui->get_layout_manager().set_nav_rail_expanded(!old_val);
                    }});
            }
        },
        "",
        "Panel",
        static_cast<uint16_t>(ui::Icon::Menu));

    // ─── Split view commands ─────────────────────────────────────────────
    auto do_split = [&](SplitDirection dir)
    {
        if (dock_system.is_split())
        {
            SplitPane* active_pane = dock_system.split_view().active_pane();
            if (!active_pane || active_pane->figure_count() < 2)
                return;

            size_t active_local = active_pane->active_local_index();
            size_t move_local = (active_local + 1) % active_pane->figure_count();
            size_t move_fig = active_pane->figure_indices()[move_local];

            active_pane->remove_figure(move_fig);

            size_t active_fig = active_pane->figure_index();
            SplitPane* new_pane = nullptr;
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
                SplitPane* root = dock_system.split_view().root();
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

    cmd_registry.register_command(
        "view.split_right",
        "Split Right",
        [&, do_split]() { do_split(SplitDirection::Horizontal); },
        "Ctrl+\\",
        "View");

    cmd_registry.register_command(
        "view.split_down",
        "Split Down",
        [&, do_split]() { do_split(SplitDirection::Vertical); },
        "Ctrl+Shift+\\",
        "View");

    cmd_registry.register_command(
        "view.close_split",
        "Close Split Pane",
        [&]()
        {
            if (dock_system.is_split())
            {
                dock_system.close_split(dock_system.active_figure_index());
            }
        },
        "",
        "View");

    cmd_registry.register_command(
        "view.reset_splits",
        "Reset All Splits",
        [&]() { dock_system.reset_splits(); },
        "",
        "View");

    // ─── Tool mode commands ──────────────────────────────────────────────
    cmd_registry.register_command(
        "tool.pan",
        "Pan Tool",
        [&]() { input_handler.set_tool_mode(ToolMode::Pan); },
        "",
        "Tools",
        static_cast<uint16_t>(ui::Icon::Hand));

    cmd_registry.register_command(
        "tool.box_zoom",
        "Box Zoom Tool",
        [&]() { input_handler.set_tool_mode(ToolMode::BoxZoom); },
        "",
        "Tools",
        static_cast<uint16_t>(ui::Icon::ZoomIn));

    // ─── Window commands ─────────────────────────────────────────────────
#ifdef SPECTRA_USE_GLFW
    cmd_registry.register_command(
        "app.new_window",
        "New Window",
        [&, window_mgr]()
        {
            if (!window_mgr)
                return;
            FigureId dup_id = fig_mgr.duplicate_figure(active_figure_id);
            if (dup_id == INVALID_FIGURE_ID)
                return;
            Figure* dup_fig = registry.get(dup_id);
            uint32_t w = dup_fig ? dup_fig->width() : 800;
            uint32_t h = dup_fig ? dup_fig->height() : 600;
            std::string win_title = fig_mgr.get_title(dup_id);
            window_mgr->create_window_with_ui(w, h, win_title, dup_id);
        },
        "Ctrl+Shift+N",
        "App",
        static_cast<uint16_t>(ui::Icon::Plus));

    cmd_registry.register_command(
        "figure.move_to_window",
        "Move Figure to Window",
        [&, window_mgr]()
        {
            if (!window_mgr)
                return;
            if (!window_mgr || window_mgr->windows().empty())
                return;
            auto* src_wctx = window_mgr->focused_window();
            if (!src_wctx) src_wctx = window_mgr->windows()[0];

            FigureId fig_id = active_figure_id;
            if (fig_id == INVALID_FIGURE_ID)
                return;

            if (fig_mgr.count() <= 1)
            {
                SPECTRA_LOG_WARN("window_manager",
                                 "Cannot move last figure from window");
                return;
            }

            WindowContext* target = nullptr;
            for (auto* wctx : window_mgr->windows())
            {
                if (wctx != src_wctx && wctx->ui_ctx)
                {
                    target = wctx;
                    break;
                }
            }

            if (target)
            {
                window_mgr->move_figure(fig_id, src_wctx->id, target->id);
            }
            else
            {
                Figure* fig = registry.get(fig_id);
                uint32_t w = fig ? fig->width() : 800;
                uint32_t h = fig ? fig->height() : 600;
                std::string title = fig_mgr.get_title(fig_id);

                FigureState state = fig_mgr.remove_figure(fig_id);

                auto& pf = src_wctx->assigned_figures;
                pf.erase(std::remove(pf.begin(), pf.end(), fig_id), pf.end());
                if (src_wctx->active_figure_id == fig_id)
                    src_wctx->active_figure_id = pf.empty() ? INVALID_FIGURE_ID : pf.front();

                auto* new_wctx = window_mgr->create_window_with_ui(w, h, title, fig_id);
                if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                {
                    auto* new_fm = new_wctx->ui_ctx->fig_mgr;
                    new_fm->state(fig_id) = std::move(state);
                    std::string correct_title = new_fm->get_title(fig_id);
                    if (new_fm->tab_bar())
                        new_fm->tab_bar()->set_tab_title(0, correct_title);
                }
            }
        },
        "Ctrl+Shift+M",
        "App",
        static_cast<uint16_t>(ui::Icon::Plus));
#endif

    // Register default shortcut bindings
    shortcut_mgr.register_defaults();

    SPECTRA_LOG_INFO("app",
                    "Registered " + std::to_string(cmd_registry.count()) + " commands, "
                        + std::to_string(shortcut_mgr.count()) + " shortcuts");
#else
    (void)b;
#endif
}

}  // namespace spectra
