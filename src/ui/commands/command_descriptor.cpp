#include "command_descriptor.hpp"

#include "command_registry.hpp"
#include "shortcut_manager.hpp"

namespace spectra
{

void register_descriptors(CommandRegistry&                   registry,
                          ShortcutManager&                   shortcuts,
                          std::span<const CommandDescriptor> descriptors)
{
    for (const auto& desc : descriptors)
    {
        registry.register_command(desc.id,
                                  desc.label,
                                  desc.action,
                                  desc.shortcut,
                                  desc.category,
                                  desc.icon);
    }
}

}   // namespace spectra
