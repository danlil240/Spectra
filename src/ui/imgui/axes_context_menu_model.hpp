#pragma once

// axes_context_menu_model.hpp — ImGui-free description of the items shown in
// the right-click context menu of a plot axes.  The ImGui layer renders the
// list returned by build_axes_context_menu(); keeping the model separate lets
// unit tests check which actions are offered without a GPU or ImGui context.

#include <cstddef>
#include <string>
#include <vector>

namespace spectra
{

class CommandRegistry;

enum class AxesMenuItemKind
{
    Separator,
    Command,          // Run command_id from the CommandRegistry
    AxesProperties,   // Select this axes and show the inspector's Axes section
    LinkX,
    LinkY,
    LinkBoth,
    Link3D,
    LinkGroupsInfo,   // Read-only list of link groups this axes belongs to
    UnlinkThis,
    UnlinkAll,
    CopySeries,
    CutSeries,
    DeleteSeries,
    PasteSeries,
};

struct AxesMenuItem
{
    AxesMenuItemKind kind = AxesMenuItemKind::Separator;
    std::string      label;
    std::string      shortcut;
    std::string      command_id;       // Only for AxesMenuItemKind::Command
    bool             danger = false;   // Destructive action (drawn in red)
};

struct AxesContextMenuState
{
    bool   is_3d                = false;
    bool   has_multiple_axes    = false;   // More than one axes (2D or 3D) in the figure
    bool   is_linked            = false;   // This (2D) axes is in at least one link group
    size_t selected_series      = 0;       // Number of selected series
    size_t clipboard_series     = 0;       // Number of series in the series clipboard
    bool   has_series_clipboard = false;   // A SeriesClipboard is wired
};

// Builds the ordered item list for the axes context menu.  Command items are
// only added when `registry` has the command; their label and shortcut come
// from the registry so the menu matches the command palette and keymap.
// Separators are never leading, trailing or doubled.
std::vector<AxesMenuItem> build_axes_context_menu(const AxesContextMenuState& state,
                                                  const CommandRegistry*      registry);

}   // namespace spectra
