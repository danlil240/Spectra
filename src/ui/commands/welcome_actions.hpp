#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

class CommandRegistry;
class ShortcutManager;

// Pseudo command id for the welcome screen's "Open CSV" action. CSV loading is
// owned by ImGuiIntegration (native dialog + column picker), not the command
// registry, so the welcome screen handles this id itself.
inline constexpr std::string_view WELCOME_OPEN_CSV_ID = "welcome.open_csv";

// One action button on the empty-app welcome screen.
struct WelcomeAction
{
    std::string id;         // Command registry id (or WELCOME_OPEN_CSV_ID)
    std::string label;      // Button label
    std::string shortcut;   // Human-readable shortcut from the registry ("" if none)
    bool        primary = false;
};

// Builds the welcome screen actions. Registry-backed actions take their label
// from the registry so they stay in sync with menus and the command palette. With
// `shortcuts`, the shown shortcut is the command's declared one if it is actually
// bound to it, else another live binding, else none; without it, the declared
// shortcut is shown as is. Actions whose command is not registered or is
// disabled are omitted. "Open CSV" is always present. A null registry yields
// only "Open CSV".
std::vector<WelcomeAction> build_welcome_actions(const CommandRegistry* registry,
                                                 const ShortcutManager* shortcuts = nullptr);

// Keyboard hint shown under the actions, e.g.
// "Ctrl+T  New Figure  ·  Ctrl+K  Command Palette". Empty if no action has a shortcut.
std::string build_welcome_hint(const std::vector<WelcomeAction>& actions);

}   // namespace spectra
