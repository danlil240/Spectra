// commands_series.cpp — Series command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#include <string>

namespace spectra
{

std::vector<CommandDescriptor> make_series_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto&  ui_ctx        = ctx.ui_ctx;
    auto*& active_figure = *ctx.active_figure;
    auto&  imgui_ui      = ui_ctx.imgui_ui;
    auto&  undo_mgr      = ui_ctx.undo_mgr;
    auto&  fig_mgr       = *ui_ctx.fig_mgr;

    cmds.push_back({"series.cycle_selection",
                    "Cycle Series Selection",
                    "Tab",
                    "Series",
                    0,
                    [&]()
                    {
                        Figure* current_fig = fig_mgr.active_figure();
                        if (!current_fig)
                            current_fig = active_figure;
                        if (!current_fig)
                            return;
                        if (!imgui_ui || !imgui_ui->is_initialized())
                            return;
                        Axes* target_ax  = nullptr;
                        int   target_idx = -1;
                        for (size_t i = 0; i < current_fig->axes().size(); ++i)
                        {
                            auto* ax = current_fig->axes_mut()[i].get();
                            if (ax && !ax->series().empty())
                            {
                                target_ax  = ax;
                                target_idx = static_cast<int>(i);
                                break;
                            }
                        }
                        if (!target_ax || target_ax->series().empty())
                            return;

                        auto& sel        = imgui_ui->selection_context();
                        int   next_s_idx = 0;
                        if (sel.type == ui::SelectionType::Series && sel.axes == target_ax
                            && sel.series_index >= 0)
                        {
                            next_s_idx = (sel.series_index + 1)
                                         % static_cast<int>(target_ax->series().size());
                        }

                        auto* s = target_ax->series()[next_s_idx].get();
                        imgui_ui->select_series(current_fig, target_ax, target_idx, s, next_s_idx);
                        imgui_ui->set_inspector_section_series();
                    }});

    cmds.push_back({"series.copy",
                    "Copy Series",
                    "Ctrl+C",
                    "Series",
                    static_cast<uint16_t>(ui::Icon::Copy),
                    [&]()
                    {
                        if (!imgui_ui || !imgui_ui->is_initialized())
                            return;
                        auto& sel = imgui_ui->selection_context();
                        if (sel.type != ui::SelectionType::Series || !imgui_ui->series_clipboard())
                        {
                            SPECTRA_LOG_DEBUG("clipboard",
                                              "series.copy: no series selected or no clipboard");
                            return;
                        }
                        SPECTRA_LOG_INFO("clipboard",
                                         "series.copy: copying "
                                             + std::to_string(sel.selected_count()) + " series");
                        if (sel.has_multi_selection())
                        {
                            std::vector<const Series*> list;
                            for (const auto& e : sel.selected_series)
                                if (e.series)
                                    list.push_back(e.series);
                            imgui_ui->series_clipboard()->copy_multi(list);
                        }
                        else if (sel.series)
                        {
                            imgui_ui->series_clipboard()->copy(*sel.series);
                        }
                    }});

    cmds.push_back(
        {"series.cut",
         "Cut Series",
         "Ctrl+X",
         "Series",
         static_cast<uint16_t>(ui::Icon::Edit),
         [&]()
         {
             if (!imgui_ui || !imgui_ui->is_initialized())
                 return;
             auto& sel = imgui_ui->selection_context();
             if (sel.type != ui::SelectionType::Series || !imgui_ui->series_clipboard())
                 return;
             if (sel.has_multi_selection())
             {
                 std::vector<const Series*> list;
                 for (const auto& e : sel.selected_series)
                     if (e.series)
                         list.push_back(e.series);
                 imgui_ui->series_clipboard()->cut_multi(list);
             }
             else if (sel.series)
             {
                 imgui_ui->series_clipboard()->cut(*sel.series);
             }
             for (const auto& e : sel.selected_series)
             {
                 AxesBase* owner = e.axes_base ? e.axes_base : static_cast<AxesBase*>(e.axes);
                 if (owner && e.series)
                     imgui_ui->defer_series_removal(owner, const_cast<Series*>(e.series));
             }
             sel.clear();
         }});

    cmds.push_back(
        {"series.paste",
         "Paste Series",
         "Ctrl+V",
         "Series",
         static_cast<uint16_t>(ui::Icon::Duplicate),
         [&]()
         {
             if (!imgui_ui || !imgui_ui->is_initialized())
                 return;
             if (!imgui_ui->series_clipboard() || !imgui_ui->series_clipboard()->has_data())
             {
                 SPECTRA_LOG_DEBUG("clipboard", "series.paste: no clipboard or no data");
                 return;
             }
             Figure* current_fig = fig_mgr.active_figure();
             if (!current_fig)
                 current_fig = active_figure;
             if (!current_fig)
             {
                 SPECTRA_LOG_DEBUG("clipboard", "series.paste: no active figure");
                 return;
             }
             SPECTRA_LOG_INFO("clipboard",
                              "series.paste: pasting "
                                  + std::to_string(imgui_ui->series_clipboard()->count())
                                  + " series");
             auto&     sel    = imgui_ui->selection_context();
             AxesBase* target = nullptr;
             if (sel.type == ui::SelectionType::Series || sel.type == ui::SelectionType::Axes)
             {
                 if (sel.figure == current_fig)
                     target = sel.axes_base ? sel.axes_base : static_cast<AxesBase*>(sel.axes);
             }
             if (!target)
             {
                 if (!current_fig->all_axes().empty())
                     target = current_fig->all_axes_mut()[0].get();
                 else if (!current_fig->axes().empty())
                     target = current_fig->axes_mut()[0].get();
             }
             if (target)
             {
                 constexpr size_t kMaxSeriesPerAxes = 200;
                 if (target->series().size() >= kMaxSeriesPerAxes)
                 {
                     SPECTRA_LOG_WARN("clipboard",
                                      "series.paste: axes already has {} series (max {})",
                                      target->series().size(),
                                      kMaxSeriesPerAxes);
                     return;
                 }
                 imgui_ui->series_clipboard()->paste_all(*target);
             }
         }});

    cmds.push_back(
        {"series.delete",
         "Delete Series",
         "Delete",
         "Series",
         static_cast<uint16_t>(ui::Icon::Trash),
         [&]()
         {
             if (!imgui_ui || !imgui_ui->is_initialized())
                 return;
             auto& sel = imgui_ui->selection_context();
             if (sel.type != ui::SelectionType::Series)
                 return;

             struct DeleteEntry
             {
                 AxesBase*      owner;
                 SeriesSnapshot snap;
             };
             std::vector<DeleteEntry> entries;
             entries.reserve(sel.selected_series.size());

             for (const auto& e : sel.selected_series)
             {
                 AxesBase* owner = e.axes_base ? e.axes_base : static_cast<AxesBase*>(e.axes);
                 if (owner && e.series)
                 {
                     entries.push_back({owner, SeriesClipboard::snapshot(*e.series)});
                     imgui_ui->defer_series_removal(owner, const_cast<Series*>(e.series));
                 }
             }
             sel.clear();

             if (!entries.empty())
             {
                 undo_mgr.push(UndoAction{"Delete series",
                                          [entries]()
                                          {
                                              for (const auto& de : entries)
                                                  SeriesClipboard::paste_to(*de.owner, de.snap);
                                          },
                                          nullptr});
             }
         }});

    cmds.push_back({"series.deselect",
                    "Deselect Series",
                    "Escape",
                    "Series",
                    0,
                    [&]()
                    {
                        if (!imgui_ui || !imgui_ui->is_initialized())
                            return;
                        imgui_ui->deselect_series();
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
