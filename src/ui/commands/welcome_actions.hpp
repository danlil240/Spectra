#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

class CommandRegistry;
class ShortcutManager;

// Pseudo command ids handled by ImGuiIntegration itself rather than the
// command registry: "Open file..." (native dialog for CSV/TSV, ULog, MCAP and
// layouts, used when no `file.open` command is registered) and "Topics" (shows
// the Topics panel; `panel.toggle_topics` would hide an already open panel).
inline constexpr std::string_view WELCOME_OPEN_FILE_ID = "welcome.open_file";
inline constexpr std::string_view WELCOME_TOPICS_ID    = "welcome.topics";

// One action of the start page's "Start" column.
struct WelcomeAction
{
    std::string id;         // Command registry id or a WELCOME_*_ID pseudo id
    std::string label;      // Row label
    std::string shortcut;   // Human-readable live shortcut ("" if none)
    bool        primary = false;
    uint16_t    icon    = 0;   // ui::Icon codepoint (0 = none)
};

// The shortcut shown for command @p id: with `shortcuts`, the command's declared
// shortcut if it is actually bound to it, else another live binding, else "";
// without it, the declared shortcut as is.  "" for unknown commands.
std::string welcome_command_shortcut(const CommandRegistry* registry,
                                     const ShortcutManager* shortcuts,
                                     std::string_view       id);

// Builds the "Start" column, in order: New figure (`figure.new`, primary),
// Open file... (`file.open`, else the WELCOME_OPEN_FILE_ID pseudo action),
// Open layout... (`file.open_layout`) and Topics (WELCOME_TOPICS_ID, with the
// shortcut of `panel.toggle_topics`).  Registry actions whose command is not
// registered or is disabled are omitted; Open file... and Topics are always
// present.  A null registry yields only those two.
std::vector<WelcomeAction> build_welcome_actions(const CommandRegistry* registry,
                                                 const ShortcutManager* shortcuts = nullptr);

// Keyboard hint, e.g. "Ctrl+T  New Figure  ·  Ctrl+K  Command Palette".
// Empty if no action has a shortcut.
std::string build_welcome_hint(const std::vector<WelcomeAction>& actions);

// ─── "Recent" column ─────────────────────────────────────────────────────────

struct WelcomeRecentItem
{
    std::string path;     // full path (opened on click, shown as tooltip)
    std::string name;     // file name
    std::string folder;   // parent folder name ("" at the root)
    std::string kind;     // "CSV", "ULog", "MCAP", "Layout", ...
    bool        layout = false;
};

// Recent data files first (newest first, at most @p max_files), then recent
// layouts (at most @p max_layouts).  Paths listed in both keep their first entry.
std::vector<WelcomeRecentItem> build_welcome_recent(std::span<const std::string> files,
                                                    std::span<const std::string> layouts,
                                                    size_t                       max_files   = 8,
                                                    size_t                       max_layouts = 5);

}   // namespace spectra
