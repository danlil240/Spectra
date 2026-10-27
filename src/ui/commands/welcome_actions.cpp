// welcome_actions.cpp — Action model for the empty-app welcome screen.

#include "welcome_actions.hpp"

#include "command_registry.hpp"
#include "shortcut_manager.hpp"

namespace spectra
{

namespace
{

void add_registry_action(std::vector<WelcomeAction>& out,
                         const CommandRegistry*      registry,
                         const ShortcutManager*      shortcuts,
                         const char*                 id,
                         bool                        primary)
{
    if (!registry)
        return;
    const Command* cmd = registry->find(id);
    if (!cmd || !cmd->enabled)
        return;
    std::string shortcut = cmd->shortcut;
    if (shortcuts)
    {
        // A command can have several bindings (its declared one plus defaults).
        // Show the declared shortcut when it is really bound to this command,
        // otherwise any live binding, otherwise nothing.
        const Shortcut declared = Shortcut::from_string(cmd->shortcut);
        if (!declared.valid() || shortcuts->command_for_shortcut(declared) != cmd->id)
        {
            const Shortcut bound = shortcuts->shortcut_for_command(cmd->id);
            shortcut             = bound.valid() ? bound.to_string() : std::string();
        }
    }
    out.push_back({cmd->id, cmd->label, std::move(shortcut), primary});
}

}   // namespace

std::vector<WelcomeAction> build_welcome_actions(const CommandRegistry* registry,
                                                 const ShortcutManager* shortcuts)
{
    std::vector<WelcomeAction> actions;
    add_registry_action(actions, registry, shortcuts, "figure.new", true);
    actions.push_back({std::string(WELCOME_OPEN_CSV_ID), "Open CSV...", "", false});
    return actions;
}

std::string build_welcome_hint(const std::vector<WelcomeAction>& actions)
{
    std::string hint;
    for (const WelcomeAction& a : actions)
    {
        if (a.shortcut.empty())
            continue;
        if (!hint.empty())
            hint += "  \xC2\xB7  ";   // U+00B7 middle dot
        hint += a.shortcut;
        hint += "  ";
        hint += a.label;
    }
    return hint;
}

}   // namespace spectra
