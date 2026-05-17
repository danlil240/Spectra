// command_descriptor.cpp — Implements batch registration of CommandDescriptors.

#include "command_descriptor.hpp"

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"

namespace spectra
{

void register_descriptors(CommandRegistry&                      registry,
                          ShortcutManager&                      shortcuts,
                          const std::vector<CommandDescriptor>& descriptors)
{
    for (const auto& desc : descriptors)
    {
        registry.register_command(desc.id,
                                  desc.label,
                                  desc.action,
                                  desc.shortcut,
                                  desc.category,
                                  desc.icon);

        if (!desc.shortcut.empty())
        {
            Shortcut sc = Shortcut::from_string(desc.shortcut);
            if (sc.valid())
                shortcuts.bind(sc, desc.id);
        }
    }
}

}   // namespace spectra
