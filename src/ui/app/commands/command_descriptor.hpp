// command_descriptor.hpp — Declarative command descriptors for table-driven
// command registration.  Each feature area provides a vector of descriptors
// that are batch-registered into the CommandRegistry.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace spectra
{

class CommandRegistry;
class ShortcutManager;

// Declarative description of a single command.
// Groups command metadata with its action so feature areas can define
// commands as data rather than imperative registration calls.
struct CommandDescriptor
{
    std::string           id;         // e.g. "view.reset"
    std::string           label;      // e.g. "Reset View"
    std::string           shortcut;   // e.g. "R", "" = no shortcut
    std::string           category;   // e.g. "View"
    uint16_t              icon = 0;   // ui::Icon enum value, 0 = none
    std::function<void()> action;     // Command callback
};

// Batch-register a list of descriptors into the given CommandRegistry.
// Shortcuts are auto-registered via the ShortcutManager.
void register_descriptors(CommandRegistry&                      registry,
                          const std::vector<CommandDescriptor>& descriptors);

}   // namespace spectra
