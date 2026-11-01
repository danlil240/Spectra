// commands_file.cpp — File command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <optional>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/commands/undoable_property.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/export_dialog.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/workspace/plugin_api.hpp"
    #include "ui/workspace/workspace.hpp"
    #include "ui/workspace/figure_serializer.hpp"
    #include "ui/workspace/layout.hpp"
    #include "ui/workspace/layout_session.hpp"

    #include <algorithm>
    #include <cstdlib>
    #include <filesystem>

    #include "../../../../third_party/tinyfiledialogs.h"
    #include "ui/dialog_env_guard.hpp"
    #include "ui/native_dialog_policy.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_file_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto&  ui_ctx           = ctx.ui_ctx;
    auto&  registry         = ctx.registry;
    auto*& active_figure    = *ctx.active_figure;
    auto&  imgui_ui         = ui_ctx.imgui_ui;
    auto&  data_interaction = ui_ctx.data_interaction;
    auto&  dock_system      = ui_ctx.dock_system;
    auto&  undo_mgr         = ui_ctx.undo_mgr;
    auto&  fig_mgr          = *ui_ctx.fig_mgr;
    auto&  tm               = *ui_ctx.theme_mgr;

    cmds.push_back({"file.export_png",
                    "Export PNG",
                    "Ctrl+S",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Export),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        const char* patterns[] = {"*.png"};
                        auto        path       = spectra::ask_export_path("Export PNG",
                                                             "spectra_export.png",
                                                             1,
                                                             patterns,
                                                             "PNG Image",
                                                             ui_ctx.last_export_dir);
                        if (!path)
                            return;
                        active_figure->save_png(*path);
                    }});

    cmds.push_back({"file.export_svg",
                    "Export SVG",
                    "Ctrl+Shift+S",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Export),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        const char* patterns[] = {"*.svg"};
                        auto        path       = spectra::ask_export_path("Export SVG",
                                                             "spectra_export.svg",
                                                             1,
                                                             patterns,
                                                             "SVG Image",
                                                             ui_ctx.last_export_dir);
                        if (!path)
                            return;
                        active_figure->save_svg(*path);
                    }});

    cmds.push_back({"file.copy_to_clipboard",
                    "Copy Figure to Clipboard",
                    "Ctrl+Shift+C",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Copy),
                    [&]()
                    {
                        if (!active_figure || !registry.get(registry.find_id(active_figure)))
                            return;
                        active_figure->copy_to_clipboard();
                    }});

    auto& layout_session = ui_ctx.layout_session;
    layout_session.set_figure_registry(&registry);

    // This window's workspace state (Save Workspace and Save Layout).
    auto capture_workspace = [&]() -> WorkspaceData
    {
        WorkspaceData::PanelState panels;
        if (imgui_ui)
        {
            auto& lm                 = imgui_ui->get_layout_manager();
            panels.inspector_visible = lm.is_inspector_visible();
            panels.inspector_width   = lm.inspector_width();
            panels.nav_rail_expanded = lm.is_nav_rail_expanded();
        }

        std::vector<Figure*> figs;
        for (auto id : fig_mgr.figure_ids())
        {
            Figure* f = registry.get(id);
            if (f)
                figs.push_back(f);
        }
        auto data = Workspace::capture(figs,
                                       fig_mgr.active_index(),
                                       tm.current_theme_name(),
                                       panels.inspector_visible,
                                       panels.inspector_width,
                                       panels.nav_rail_expanded);
        if (data_interaction)
        {
            data.interaction.crosshair_enabled = data_interaction->crosshair_active();
            data.interaction.tooltip_enabled   = data_interaction->tooltip_active();
            for (const auto& m : data_interaction->markers())
            {
                if (m.is_3d)
                    continue;   // workspace markers are 2D only
                OverlaySnapshot::MarkerEntry me;
                me.data_x       = m.data_x;
                me.data_y       = m.data_y;
                me.series_label = m.series_label;
                me.point_index  = m.point_index;
                for (auto* fig : figs)
                {
                    if (!fig || !m.axes)
                        continue;
                    for (size_t ai = 0; ai < fig->axes().size(); ++ai)
                    {
                        if (fig->axes()[ai].get() == m.axes)
                            me.axes_index = ai;
                    }
                }
                data.interaction.markers.push_back(std::move(me));
            }

            for (const auto& ann : data_interaction->annotations().annotations())
            {
                if (ann.editing)
                    continue;
                if (ann.text.empty())
                    continue;
                OverlaySnapshot::AnnotationEntry ae;
                ae.data_x     = ann.data_x;
                ae.data_y     = ann.data_y;
                ae.text       = ann.text;
                ae.color      = ann.color;
                ae.offset_x   = ann.offset_x;
                ae.offset_y   = ann.offset_y;
                ae.axes_index = 0;
                if (ann.axes && !figs.empty())
                {
                    for (auto* fig : figs)
                    {
                        if (!fig)
                            continue;
                        size_t ai = 0;
                        for (const auto& ax : fig->axes())
                        {
                            if (ax.get() == ann.axes)
                            {
                                ae.axes_index = ai;
                                break;
                            }
                            ++ai;
                        }
                    }
                }
                data.interaction.annotations.push_back(std::move(ae));
            }
        }
        for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
        {
            // Positional: figure i is the i-th tab (ids are not indices).
            const FigureId fid               = fig_mgr.figure_ids()[i];
            data.figures[i].custom_tab_title = fig_mgr.get_title(fid);
            data.figures[i].is_modified      = fig_mgr.is_modified(fid);
        }
        data.undo_count      = undo_mgr.undo_count();
        data.redo_count      = undo_mgr.redo_count();
        data.dock_state      = dock_system.serialize();
        data.last_export_dir = ui_ctx.last_export_dir;
        if (ui_ctx.plugin_manager)
            data.plugin_state = ui_ctx.plugin_manager->serialize_state();
        return data;
    };

    cmds.push_back({"file.save_workspace",
                    "Save Workspace",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Save),
                    [capture_workspace]()
                    { Workspace::save(Workspace::default_path(), capture_workspace()); }});

    // Save the window as a shareable layout: data bindings, no data.
    auto save_layout_to = [&, capture_workspace](const std::string& path) -> bool
    {
        WorkspaceData data;
        // Unresolved series show "[unresolved: ...]" in the legend; save their real labels.
        layout_session.with_saved_labels([&]() { data = capture_workspace(); });
        Workspace::make_layout(data);
        data.data_palette_name = tm.current_data_palette_name();

        // Store figure positions, not this session's figure ids.
        const auto& ids         = fig_mgr.figure_ids();
        auto        position_of = [&ids](FigureId id) -> std::optional<FigureId>
        {
            auto it = std::find(ids.begin(), ids.end(), id);
            if (it == ids.end())
                return std::nullopt;
            return static_cast<FigureId>(it - ids.begin());
        };
        data.active_figure_index = position_of(fig_mgr.active_index()).value_or(0);
        data.dock_state          = remap_dock_figures(data.dock_state, position_of);

        if (!Workspace::save(path, data))
        {
            SPECTRA_LOG_WARN("layout", "Could not write layout {}", path);
            return false;
        }
        size_t bindings = 0;
        for (const auto& f : data.figures)
        {
            for (const auto& ss : f.series)
                bindings += ss.binding.empty() ? 0 : 1;
        }
        SPECTRA_LOG_INFO("layout",
                         "Saved layout {} ({} figure(s), {} data binding(s))",
                         path,
                         data.figures.size(),
                         bindings);
        layout_session.recent().push(path);
        return true;
    };

    cmds.push_back({"file.save_layout",
                    "Save Layout...",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Save),
                    [&, save_layout_to]()
                    {
                        const char* patterns[] = {"*.spectra"};
                        auto        path       = spectra::ask_export_path("Save Layout",
                                                             "layout.spectra",
                                                             1,
                                                             patterns,
                                                             "Spectra layout",
                                                             layout_session.last_dir);
                        if (path)
                            save_layout_to(*path);
                    }});

    // Open a layout: new figures (tabs) whose series re-resolve their bindings.
    layout_session.set_open_handler(
        [&](const std::string& path) -> bool
        {
            WorkspaceData data;
            if (!Workspace::load(path, data))
            {
                SPECTRA_LOG_WARN("layout", "Could not open layout {}", path);
                return false;
            }
            validate_workspace_data(data);
            if (!data.layout_only)
                SPECTRA_LOG_INFO("layout",
                                 "{} is a workspace, not a layout: opening its figures without "
                                 "series",
                                 path);

            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path  abs_path = fs::absolute(path, ec);
            // CSV files are looked up next to the layout, then in the working directory.
            layout_session.csv_provider().add_search_dir(abs_path.parent_path().string());
            layout_session.csv_provider().add_search_dir(fs::current_path(ec).string());
            layout_session.last_dir = abs_path.parent_path().string();

            const bool fresh  = fig_mgr.count() == 0;
            auto       result = instantiate_layout(data, registry, layout_session.resolver());
            for (size_t i = 0; i < result.figure_ids.size(); ++i)
            {
                const FigureId id = result.figure_ids[i];
                fig_mgr.add_figure(id, FigureState(id, registry.get(id)));
                if (!data.figures[i].custom_tab_title.empty())
                    fig_mgr.set_title(id, data.figures[i].custom_tab_title);
            }

            // Window-level state only when the layout is the whole window;
            // opened next to existing figures it just adds tabs.
            if (fresh)
            {
                if (!data.theme_name.empty())
                {
                    tm.set_theme(data.theme_name);
                    tm.apply_to_imgui();
                }
                if (!data.data_palette_name.empty())
                    tm.set_data_palette(data.data_palette_name);
                if (imgui_ui)
                {
                    auto& lm = imgui_ui->get_layout_manager();
                    lm.set_inspector_visible(data.panels.inspector_visible);
                    lm.set_nav_rail_expanded(data.panels.nav_rail_expanded);
                }
                if (data.layout_only && !data.dock_state.empty())
                {
                    const auto& ids = result.figure_ids;
                    dock_system.deserialize(
                        remap_dock_figures(data.dock_state,
                                           [&ids](FigureId pos) -> std::optional<FigureId>
                                           {
                                               if (pos < ids.size())
                                                   return ids[static_cast<size_t>(pos)];
                                               return std::nullopt;
                                           }));
                }
                if (data_interaction)
                {
                    data_interaction->set_crosshair(data.interaction.crosshair_enabled);
                    data_interaction->set_tooltip(data.interaction.tooltip_enabled);
                }
            }
            if (!result.figure_ids.empty())
            {
                size_t active = 0;
                if (data.layout_only && data.active_figure_index < result.figure_ids.size())
                    active = static_cast<size_t>(data.active_figure_index);
                fig_mgr.queue_switch(result.figure_ids[active]);
            }

            SPECTRA_LOG_INFO("layout",
                             "Opened layout {}: {} bound, {} waiting for topics, {} unresolved",
                             path,
                             result.count(BindingStatus::Bound),
                             result.count(BindingStatus::Waiting),
                             result.count(BindingStatus::Unresolved));
            layout_session.adopt(std::move(result));
            layout_session.recent().push(path);
            return true;
        });

    cmds.push_back({"file.open_layout",
                    "Open Layout...",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::FolderOpen),
                    [&]()
                    {
                        if (!native_dialogs_enabled())
                            return;
                        DialogEnvGuard env_guard;
                        char const*    filters[1] = {"*.spectra"};
                        std::string    start      = layout_session.last_dir;
                        if (start.empty())
                        {
                            const char* home = std::getenv("HOME");
                            start            = home ? home : "/";
                        }
                        if (start.back() != '/')
                            start += '/';
                        const char* result = tinyfd_openFileDialog("Open Layout",
                                                                   start.c_str(),
                                                                   1,
                                                                   filters,
                                                                   "Spectra layout (*.spectra)",
                                                                   0);
                        if (result)
                            layout_session.open(result);
                    }});

    cmds.push_back({"file.data_bindings",
                    "Data Bindings",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Link),
                    [&]() { layout_session.set_visible(!layout_session.is_visible()); }});

    cmds.push_back(
        {"file.load_workspace",
         "Load Workspace",
         "",
         "File",
         static_cast<uint16_t>(ui::Icon::FolderOpen),
         [&]()
         {
             WorkspaceData data;
             if (Workspace::load(Workspace::default_path(), data))
             {
                 // With no figure open (welcome screen) the workspace brings its
                 // figures back: same size and subplot layout, empty axes (series
                 // data is not stored in workspaces). Loading into open figures
                 // keeps them and is undoable as before.
                 const bool had_figures = active_figure != nullptr && fig_mgr.count() > 0;
                 if (fig_mgr.count() == 0)
                 {
                     for (const auto& fs : data.figures)
                     {
                         FigureId id = registry.register_figure(Workspace::build_figure(fs));
                         fig_mgr.add_figure(id, FigureState(id, registry.get(id)));
                     }
                 }
                 Figure* target = active_figure;
                 if (!target && fig_mgr.count() > 0)
                     target = registry.get(fig_mgr.active_index());
                 if (!target)
                     return;
                 std::optional<FigureAxisSnapshot> before_snap;
                 if (had_figures)
                     before_snap = capture_figure_axes(*target);
                 std::vector<Figure*> figs;
                 for (auto id : fig_mgr.figure_ids())
                 {
                     Figure* f = registry.get(id);
                     if (f)
                         figs.push_back(f);
                 }
                 Workspace::apply(data, figs);
                 if (before_snap)
                 {
                     auto after_snap = capture_figure_axes(*target);
                     undo_mgr.push(UndoAction{"Load workspace",
                                              [before = *before_snap]()
                                              { restore_figure_axes(before); },
                                              [after_snap]() { restore_figure_axes(after_snap); }});
                 }
                 if (data_interaction)
                 {
                     data_interaction->set_crosshair(data.interaction.crosshair_enabled);
                     data_interaction->set_tooltip(data.interaction.tooltip_enabled);

                     // Markers: find the series by label in the saved axes
                     // (first figure that has it), keeping absolute x/y.
                     data_interaction->clear_markers();
                     for (const auto& me : data.interaction.markers)
                     {
                         const Axes*   axes_ptr   = nullptr;
                         const Series* series_ptr = nullptr;
                         for (auto* fig : figs)
                         {
                             if (!fig || me.axes_index >= fig->axes().size()
                                 || !fig->axes()[me.axes_index])
                                 continue;
                             const Axes* ax = fig->axes()[me.axes_index].get();
                             for (const auto& sp : ax->series())
                             {
                                 if (sp && sp->label() == me.series_label)
                                 {
                                     series_ptr = sp.get();
                                     axes_ptr   = ax;
                                     break;
                                 }
                             }
                             if (series_ptr)
                                 break;
                         }
                         data_interaction->add_marker(me.data_x,
                                                      me.data_y,
                                                      series_ptr,
                                                      me.point_index,
                                                      axes_ptr);
                     }

                     data_interaction->annotations().clear();
                     for (const auto& ae : data.interaction.annotations)
                     {
                         if (ae.text.empty())
                             continue;
                         const Axes* axes_ptr = nullptr;
                         for (auto* fig : figs)
                         {
                             if (!fig)
                                 continue;
                             if (ae.axes_index < fig->axes().size() && fig->axes()[ae.axes_index])
                             {
                                 axes_ptr = fig->axes()[ae.axes_index].get();
                                 break;
                             }
                         }
                         size_t idx =
                             data_interaction->annotations().add(ae.data_x, ae.data_y, axes_ptr);
                         auto& ann    = data_interaction->annotations().annotations_mut()[idx];
                         ann.text     = ae.text;
                         ann.color    = ae.color;
                         ann.offset_x = ae.offset_x;
                         ann.offset_y = ae.offset_y;
                         ann.editing  = false;
                     }
                 }
                 for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
                 {
                     if (!data.figures[i].custom_tab_title.empty())
                     {
                         fig_mgr.set_title(fig_mgr.figure_ids()[i],
                                           data.figures[i].custom_tab_title);
                     }
                 }
                 if (data.active_figure_index < fig_mgr.count())
                 {
                     fig_mgr.queue_switch(data.active_figure_index);
                 }
                 if (!data.theme_name.empty())
                 {
                     tm.set_theme(data.theme_name);
                     tm.apply_to_imgui();
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
                 if (ui_ctx.plugin_manager && !data.plugin_state.empty())
                     ui_ctx.plugin_manager->deserialize_state(data.plugin_state);
                 if (!data.last_export_dir.empty())
                     ui_ctx.last_export_dir = data.last_export_dir;
             }
         }});

    cmds.push_back({"file.save_figure",
                    "Save Figure",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Save),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        if (data_interaction)
                        {
                            auto snap = data_interaction->capture_overlay_snapshot(*active_figure);
                            FigureSerializer::save_with_dialog(*active_figure, &snap);
                        }
                        else
                        {
                            FigureSerializer::save_with_dialog(*active_figure);
                        }
                    }});

    cmds.push_back({"file.load_figure",
                    "Load Figure",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::FolderOpen),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        if (data_interaction)
                        {
                            OverlaySnapshot snap;
                            FigureSerializer::load_with_dialog(*active_figure, &snap);
                            data_interaction->restore_overlay_snapshot(snap, *active_figure);
                        }
                        else
                        {
                            FigureSerializer::load_with_dialog(*active_figure);
                        }
                        for (auto& ax : active_figure->all_axes_mut())
                        {
                            if (!ax)
                                continue;
                            for (auto& s : ax->series_mut())
                            {
                                if (s)
                                    s->mark_dirty();
                            }
                        }
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
