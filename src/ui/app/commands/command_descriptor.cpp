// command_descriptor.cpp — Implements batch registration of CommandDescriptors.

#include "command_descriptor.hpp"

#include "ui/commands/command_registry.hpp"

namespace spectra
{

void register_descriptors(CommandRegistry&                      registry,
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
    }
}

}   // namespace spectra
