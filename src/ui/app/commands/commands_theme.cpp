// commands_theme.cpp — Theme and data palette command descriptors.
//
// One "theme.<id>" command per registered theme and one "palette.<id>"
// command per registered data palette, so the command palette lists them all,
// plus "theme.toggle", which switches between the last-used dark and light
// themes.  Theme commands change only the active choice (undoable); the
// startup default stays a Settings decision.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/settings/settings_store.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
#endif

#include <string>

namespace spectra
{

#ifdef SPECTRA_USE_IMGUI
namespace
{

// Readable name of a data palette id for command labels.
std::string palette_label(const std::string& id)
{
    if (id == "default")
        return "Theme Default";
    if (id == "colorblind")
        return "Okabe-Ito (Colorblind)";
    if (id == "ibm")
        return "IBM";
    return ui::theme_display_name(id);
}

uint16_t theme_icon(const ui::ThemeManager& tm, const std::string& id)
{
    switch (tm.theme_kind(id))
    {
        case ui::ThemeKind::Light:
            return static_cast<uint16_t>(ui::Icon::Sun);
        case ui::ThemeKind::HighContrast:
            return static_cast<uint16_t>(ui::Icon::Contrast);
        case ui::ThemeKind::Dark:
            break;
    }
    return static_cast<uint16_t>(ui::Icon::Moon);
}

// Switch to @p theme and push an undo entry restoring the previous theme.
void switch_theme(ui::ThemeManager&  tm,
                  UndoManager&       undo_mgr,
                  const std::string& theme,
                  const std::string& description)
{
    const std::string old_theme = tm.current_theme_name();
    tm.set_theme(theme);
    if (tm.current_theme_name() == old_theme)
        return;
    tm.apply_to_imgui();
    ui::ThemeManager* tm_ptr = &tm;
    undo_mgr.push(UndoAction{description,
                             [old_theme, tm_ptr]()
                             {
                                 tm_ptr->set_theme(old_theme);
                                 tm_ptr->apply_to_imgui();
                             },
                             [theme, tm_ptr]()
                             {
                                 tm_ptr->set_theme(theme);
                                 tm_ptr->apply_to_imgui();
                             }});
}

}   // namespace
#endif

std::vector<CommandDescriptor> make_theme_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& ui_ctx = ctx.ui_ctx;
    auto* tm_ptr = ui_ctx.theme_mgr;
    if (!tm_ptr)
        return cmds;
    auto& tm = *tm_ptr;
    tm.ensure_initialized();
    UndoManager* undo_ptr = &ui_ctx.undo_mgr;
    // WindowUIContext::settings_store may be attached after the commands are
    // built, so look it up when a command runs.
    WindowUIContext* ui_ptr = &ui_ctx;

    for (const std::string& id : tm.available_themes())
    {
        const std::string name = tm.display_name(id);
        cmds.push_back({"theme." + id,
                        "Switch to " + name + " Theme",
                        "",
                        "Theme",
                        theme_icon(tm, id),
                        [tm_ptr, undo_ptr, id, name]()
                        { switch_theme(*tm_ptr, *undo_ptr, id, "Switch to " + name + " theme"); }});
    }

    cmds.push_back({"theme.toggle",
                    "Toggle Dark/Light Theme",
                    "",
                    "Theme",
                    static_cast<uint16_t>(ui::Icon::Contrast),
                    [tm_ptr, undo_ptr, ui_ptr]()
                    {
                        const std::string target = tm_ptr->toggle_theme_target();
                        switch_theme(*tm_ptr, *undo_ptr, target, "Toggle theme");
                        // Remember the dark/light pair across restarts.
                        if (ui_ptr->settings_store)
                            ui::settings::remember_toggle_themes(*tm_ptr, *ui_ptr->settings_store);
                    }});

    for (const std::string& id : tm.available_data_palettes())
    {
        const std::string name = palette_label(id);
        cmds.push_back({"palette." + id,
                        "Use " + name + " Data Palette",
                        "",
                        "Theme",
                        static_cast<uint16_t>(ui::Icon::Palette),
                        [tm_ptr, undo_ptr, id, name]()
                        {
                            const std::string old_palette = tm_ptr->current_data_palette_name();
                            tm_ptr->set_data_palette(id);
                            if (tm_ptr->current_data_palette_name() == old_palette)
                                return;
                            undo_ptr->push(UndoAction{
                                "Use " + name + " data palette",
                                [old_palette, tm_ptr]() { tm_ptr->set_data_palette(old_palette); },
                                [id, tm_ptr]() { tm_ptr->set_data_palette(id); }});
                        }});
    }
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
