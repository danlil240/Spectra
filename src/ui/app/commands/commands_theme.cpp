// commands_theme.cpp — Theme command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
#endif

#include <string>

namespace spectra
{

std::vector<CommandDescriptor> make_theme_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& ui_ctx   = ctx.ui_ctx;
    auto& undo_mgr = ui_ctx.undo_mgr;
    auto& tm       = *ui_ctx.theme_mgr;
    auto* tm_ptr   = ui_ctx.theme_mgr;

    cmds.push_back({"theme.night",
                    "Switch to Night Theme",
                    "",
                    "Theme",
                    static_cast<uint16_t>(ui::Icon::Moon),
                    [&, tm_ptr]()
                    {
                        std::string old_theme = tm.current_theme_name();
                        tm.set_theme("night");
                        tm.apply_to_imgui();
                        undo_mgr.push(UndoAction{"Switch to night theme",
                                                 [old_theme, tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme(old_theme);
                                                     tm_ptr->apply_to_imgui();
                                                 },
                                                 [tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme("night");
                                                     tm_ptr->apply_to_imgui();
                                                 }});
                    }});

    cmds.push_back({"theme.dark",
                    "Switch to Dark Theme",
                    "",
                    "Theme",
                    static_cast<uint16_t>(ui::Icon::Moon),
                    [&, tm_ptr]()
                    {
                        std::string old_theme = tm.current_theme_name();
                        tm.set_theme("dark");
                        tm.apply_to_imgui();
                        undo_mgr.push(UndoAction{"Switch to dark theme",
                                                 [old_theme, tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme(old_theme);
                                                     tm_ptr->apply_to_imgui();
                                                 },
                                                 [tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme("dark");
                                                     tm_ptr->apply_to_imgui();
                                                 }});
                    }});

    cmds.push_back({"theme.light",
                    "Switch to Light Theme",
                    "",
                    "Theme",
                    static_cast<uint16_t>(ui::Icon::Sun),
                    [&, tm_ptr]()
                    {
                        std::string old_theme = tm.current_theme_name();
                        tm.set_theme("light");
                        tm.apply_to_imgui();
                        undo_mgr.push(UndoAction{"Switch to light theme",
                                                 [old_theme, tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme(old_theme);
                                                     tm_ptr->apply_to_imgui();
                                                 },
                                                 [tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme("light");
                                                     tm_ptr->apply_to_imgui();
                                                 }});
                    }});

    cmds.push_back({"theme.toggle",
                    "Toggle Dark/Light Theme",
                    "",
                    "Theme",
                    static_cast<uint16_t>(ui::Icon::Contrast),
                    [&, tm_ptr]()
                    {
                        std::string old_theme = tm.current_theme_name();
                        std::string new_theme = (old_theme == "dark") ? "light" : "dark";
                        tm.set_theme(new_theme);
                        tm.apply_to_imgui();
                        undo_mgr.push(UndoAction{"Toggle theme",
                                                 [old_theme, tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme(old_theme);
                                                     tm_ptr->apply_to_imgui();
                                                 },
                                                 [new_theme, tm_ptr]()
                                                 {
                                                     tm_ptr->set_theme(new_theme);
                                                     tm_ptr->apply_to_imgui();
                                                 }});
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
