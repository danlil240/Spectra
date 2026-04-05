// commands_data.cpp — Data export and accessibility command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/accessibility/sonification.hpp"
#include "ui/data/clipboard_export.hpp"
#include "ui/data/html_table_export.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
#endif

#include <string>
#include <vector>

namespace spectra
{

std::vector<CommandDescriptor> make_data_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto*& active_figure = *ctx.active_figure;
    auto&  imgui_ui      = ctx.ui_ctx.imgui_ui;

    cmds.push_back({"data.copy_to_clipboard",
                    "Copy Data to Clipboard (TSV)",
                    "Ctrl+Shift+D",
                    "Data",
                    static_cast<uint16_t>(ui::Icon::Copy),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        std::vector<const Series*> to_export;
                        if (imgui_ui)
                        {
                            const auto& sel = imgui_ui->selection_context();
                            if (!sel.selected_series.empty())
                            {
                                for (const auto& e : sel.selected_series)
                                {
                                    if (e.series && e.series->visible())
                                        to_export.push_back(e.series);
                                }
                            }
                        }
                        if (to_export.empty())
                        {
                            for (auto& ax : active_figure->axes_mut())
                            {
                                if (!ax)
                                    continue;
                                for (const auto& sp : ax->series())
                                {
                                    if (sp && sp->visible())
                                        to_export.push_back(sp.get());
                                }
                            }
                        }
                        std::string tsv = series_to_tsv(to_export);
                        if (!tsv.empty())
                        {
    #ifdef SPECTRA_USE_GLFW
                            constexpr size_t kMaxClipboardBytes = 4 * 1024 * 1024;
                            if (tsv.size() <= kMaxClipboardBytes)
                            {
                                glfwSetClipboardString(nullptr, tsv.c_str());
                            }
                            else
                            {
                                SPECTRA_LOG_WARN("clipboard",
                                                 "TSV too large for clipboard ({} bytes, max {})",
                                                 tsv.size(),
                                                 kMaxClipboardBytes);
                            }
    #endif
                        }
                    }});

    cmds.push_back(
        {"data.export_html_table",
         "Export Accessible HTML Table",
         "",
         "Data",
         static_cast<uint16_t>(ui::Icon::Export),
         [&]()
         {
             if (!active_figure)
                 return;
             const std::string path = "spectra_data.html";
             if (figure_to_html_table_file(*active_figure, path))
             {
                 SPECTRA_LOG_INFO("accessibility", "HTML table exported to '{}'", path);
             }
             else
             {
                 SPECTRA_LOG_WARN("accessibility", "Failed to write HTML table to '{}'", path);
             }
         }});

    cmds.push_back(
        {"accessibility.sonify_series",
         "Sonify Active Series (Export WAV)",
         "",
         "Accessibility",
         0,
         [&]()
         {
             if (!active_figure)
                 return;
             for (auto& ax_ptr : active_figure->axes_mut())
             {
                 if (!ax_ptr || ax_ptr->series().empty())
                     continue;
                 const std::string path = "spectra_sonify.wav";
                 if (sonify_axes_to_wav(*ax_ptr, path))
                 {
                     SPECTRA_LOG_INFO("accessibility", "Sonification WAV exported to '{}'", path);
                 }
                 else
                 {
                     SPECTRA_LOG_WARN("accessibility",
                                      "Failed to sonify axes or write WAV to '{}'",
                                      path);
                 }
                 break;
             }
         }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
