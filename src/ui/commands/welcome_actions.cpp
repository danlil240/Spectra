// welcome_actions.cpp — Action model for the empty-app start page.

#include "welcome_actions.hpp"

#include <algorithm>
#include <filesystem>

#include "command_registry.hpp"
#include "shortcut_manager.hpp"
#include "ui/app/launch_files.hpp"
#include "ui/theme/icons.hpp"

namespace spectra
{

namespace
{

uint16_t icon_id(ui::Icon icon)
{
    return static_cast<uint16_t>(icon);
}

// Appends registry command @p id; returns false when it is missing or disabled.
bool add_registry_action(std::vector<WelcomeAction>& out,
                         const CommandRegistry*      registry,
                         const ShortcutManager*      shortcuts,
                         const char*                 id,
                         const char*                 label,
                         ui::Icon                    icon,
                         bool                        primary)
{
    if (!registry)
        return false;
    const Command* cmd = registry->find(id);
    if (!cmd || !cmd->enabled)
        return false;
    out.push_back({cmd->id,
                   label ? std::string(label) : cmd->label,
                   welcome_command_shortcut(registry, shortcuts, id),
                   primary,
                   icon_id(icon)});
    return true;
}

}   // namespace

std::string welcome_command_shortcut(const CommandRegistry* registry,
                                     const ShortcutManager* shortcuts,
                                     std::string_view       id)
{
    if (!registry)
        return {};
    const Command* cmd = registry->find(std::string(id));
    if (!cmd)
        return {};
    if (!shortcuts)
        return cmd->shortcut;
    // A command can have several bindings (its declared one plus defaults).
    // Show the declared shortcut when it is really bound to this command,
    // otherwise any live binding, otherwise nothing.
    const Shortcut declared = Shortcut::from_string(cmd->shortcut);
    if (declared.valid() && shortcuts->command_for_shortcut(declared) == cmd->id)
        return cmd->shortcut;
    const Shortcut bound = shortcuts->shortcut_for_command(cmd->id);
    return bound.valid() ? bound.to_string() : std::string();
}

std::vector<WelcomeAction> build_welcome_actions(const CommandRegistry* registry,
                                                 const ShortcutManager* shortcuts)
{
    std::vector<WelcomeAction> actions;
    add_registry_action(actions,
                        registry,
                        shortcuts,
                        "figure.new",
                        "New figure",
                        ui::Icon::Plus,
                        true);
    if (!add_registry_action(actions,
                             registry,
                             shortcuts,
                             "file.open",
                             "Open file\xE2\x80\xA6",
                             ui::Icon::FolderOpen,
                             false))
    {
        actions.push_back({std::string(WELCOME_OPEN_FILE_ID),
                           "Open file\xE2\x80\xA6",
                           "",
                           false,
                           icon_id(ui::Icon::FolderOpen)});
    }
    add_registry_action(actions,
                        registry,
                        shortcuts,
                        "file.open_layout",
                        "Open layout\xE2\x80\xA6",
                        ui::Icon::Layout,
                        false);
    actions.push_back({std::string(WELCOME_TOPICS_ID),
                       "Topics / connect\xE2\x80\xA6",
                       welcome_command_shortcut(registry, shortcuts, "panel.toggle_topics"),
                       false,
                       icon_id(ui::Icon::Broadcast)});
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

std::vector<WelcomeRecentItem> build_welcome_recent(std::span<const std::string> files,
                                                    std::span<const std::string> layouts,
                                                    size_t                       max_files,
                                                    size_t                       max_layouts)
{
    std::vector<WelcomeRecentItem> out;
    auto add = [&out](const std::string& path, bool layout, size_t& budget)
    {
        if (budget == 0 || path.empty())
            return;
        if (std::any_of(out.begin(),
                        out.end(),
                        [&](const WelcomeRecentItem& it) { return it.path == path; }))
            return;
        const std::filesystem::path p(path);
        WelcomeRecentItem           item;
        item.path   = path;
        item.name   = p.filename().string();
        item.folder = p.parent_path().filename().string();
        item.kind   = layout ? std::string("Layout") : open_file_kind_label(path);
        item.layout = layout;
        out.push_back(std::move(item));
        --budget;
    };
    size_t file_budget = max_files;
    for (const std::string& f : files)
        add(f, false, file_budget);
    size_t layout_budget = max_layouts;
    for (const std::string& l : layouts)
        add(l, true, layout_budget);
    return out;
}

}   // namespace spectra
