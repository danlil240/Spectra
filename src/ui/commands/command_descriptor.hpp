#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace spectra
{

class CommandRegistry;
class ShortcutManager;

// Declarative description of a command for batch registration.
struct CommandDescriptor
{
    std::string           id;
    std::string           label;
    std::string           shortcut;   // "" = no shortcut
    std::string           category;
    uint16_t              icon = 0;
    std::function<void()> action;
    std::function<bool()> enabled;   // optional availability check (unused for now)
};

// Batch-register a list of descriptors into a CommandRegistry and ShortcutManager.
void register_descriptors(CommandRegistry&                   registry,
                          ShortcutManager&                   shortcuts,
                          std::span<const CommandDescriptor> descriptors);

}   // namespace spectra
