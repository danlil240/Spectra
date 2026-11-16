// commands_figure.cpp — Figure management command descriptors.

#include "command_context.hpp"
#include "command_groups.hpp"
#include "command_descriptor.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#include <string>

namespace spectra
{

#ifdef SPECTRA_USE_IMGUI
void request_close_figure(FigureManager& fig_mgr, DocumentSession& document, FigureId id)
{
    if (id == INVALID_FIGURE_ID || !fig_mgr.get_figure(id))
        return;
    document.prompt().request(
        UnsavedChangesPrompt::Reason::CloseFigure,
        document.is_figure_modified(id),
        [&fig_mgr, id]() { fig_mgr.queue_close_keep_window(id); },
        "\"" + fig_mgr.get_title(id) + "\"");
}

#endif

std::vector<CommandDescriptor> make_figure_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& fig_mgr  = *ctx.ui_ctx.fig_mgr;
    auto& document = ctx.ui_ctx.document;

    cmds.push_back({"figure.new",
                    "New Figure",
                    "Ctrl+T",
                    "Figure",
                    static_cast<uint16_t>(ui::Icon::Plus),
                    [&]() { fig_mgr.queue_create(); }});

    cmds.push_back({"figure.close",
                    "Close Figure",
                    "Ctrl+W",
                    "Figure",
                    static_cast<uint16_t>(ui::Icon::Close),
                    [&fig_mgr, &document]()
                    {
                        // Ctrl+W / Q close the figure, never the app: the last
                        // one leaves the start page.  Unsaved changes ask first.
                        request_close_figure(fig_mgr, document, fig_mgr.active_index());
                    }});

    // Tab switching (1-9)
    for (int i = 0; i < 9; ++i)
    {
        cmds.push_back({"figure.tab_" + std::to_string(i + 1),
                        "Switch to Figure " + std::to_string(i + 1),
                        std::to_string(i + 1),
                        "Figure",
                        0,
                        [&fig_mgr, i]()
                        {
                            const auto& ids = fig_mgr.figure_ids();
                            if (static_cast<size_t>(i) < ids.size())
                                fig_mgr.queue_switch(ids[static_cast<size_t>(i)]);
                        }});
    }

    cmds.push_back({"figure.next_tab", "Next Figure Tab", "Ctrl+Tab", "Figure", 0, [&fig_mgr]() {
                        fig_mgr.switch_to_next();
                    }});

    cmds.push_back(
        {"figure.prev_tab", "Previous Figure Tab", "Ctrl+Shift+Tab", "Figure", 0, [&fig_mgr]() {
             fig_mgr.switch_to_previous();
         }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
