// register_commands.cpp — Shared command registration for both in-process
// and multi-process (agent) windows.  Delegates to per-category descriptor
// factories under commands/ for a table-driven registration model.

#include "register_commands.hpp"

#include <spectra/logger.hpp>

#include "commands/command_context.hpp"
#include "commands/command_descriptor.hpp"
#include "commands/command_groups.hpp"
#include "window_ui_context.hpp"

namespace spectra
{

void register_standard_commands(const CommandBindings& b)
{
#ifdef SPECTRA_USE_IMGUI
    if (!b.ui_ctx || !b.registry || !b.active_figure || !b.active_figure_id)
        return;

    auto& ui_ctx = *b.ui_ctx;

    CommandContext ctx{
        ui_ctx,
        *b.registry,
        b.active_figure,
        b.active_figure_id,
        b.session,
    #ifdef SPECTRA_USE_GLFW
        b.window_mgr,
    #endif
    };

    auto& reg = ui_ctx.cmd_registry;

    auto& mgr = ui_ctx.shortcut_mgr;

    register_descriptors(reg, mgr, make_view_commands(ctx));
    register_descriptors(reg, mgr, make_edit_commands(ctx));
    register_descriptors(reg, mgr, make_file_commands(ctx));
    register_descriptors(reg, mgr, make_figure_commands(ctx));
    register_descriptors(reg, mgr, make_series_commands(ctx));
    register_descriptors(reg, mgr, make_animation_commands(ctx));
    register_descriptors(reg, mgr, make_panel_commands(ctx));
    register_descriptors(reg, mgr, make_theme_commands(ctx));
    register_descriptors(reg, mgr, make_tools_commands(ctx));
    register_descriptors(reg, mgr, make_plot_commands(ctx));
    register_descriptors(reg, mgr, make_data_commands(ctx));
    register_descriptors(reg, mgr, make_app_commands(ctx));

    ui_ctx.shortcut_mgr.register_defaults();

    SPECTRA_LOG_DEBUG("app",
                      "Registered " + std::to_string(reg.count()) + " commands, "
                          + std::to_string(ui_ctx.shortcut_mgr.count()) + " shortcuts");
#else
    (void)b;
#endif
}

}   // namespace spectra
