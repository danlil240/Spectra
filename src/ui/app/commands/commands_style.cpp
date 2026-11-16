// commands_style.cpp — plot style presets (style.<name>) and the Export
// options dialog (file.export_dialog).

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/figure.hpp>
#include <spectra/style.hpp>

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/commands/style_undo.hpp"
    #include "ui/theme/icons.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_style_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto&  ui_ctx        = ctx.ui_ctx;
    auto*& active_figure = *ctx.active_figure;
    auto&  undo_mgr      = ui_ctx.undo_mgr;

    // style.default, style.paper, style.talk, style.minimal, style.dark-paper
    for (const std::string& name : style::available())
    {
        cmds.push_back({"style." + name,
                        "Apply " + style::display_name(name) + " Style",
                        "",
                        "Style",
                        static_cast<uint16_t>(ui::Icon::Palette),
                        [&, name]()
                        {
                            const StyleSheet* st = style::find(name);
                            if (!active_figure || !st)
                                return;
                            undoable_apply_style(&undo_mgr, *active_figure, *st);
                        }});
    }

    // Standalone so menus (File > Export) and the palette can open it.
    cmds.push_back({"file.export_dialog",
                    "Export...",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Export),
                    [&]()
                    {
                        if (active_figure)
                            ui_ctx.export_dialog.open();
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
