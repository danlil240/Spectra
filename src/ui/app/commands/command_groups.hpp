// command_groups.hpp — Forward declarations for per-category command factories.

#pragma once

#include <spectra/fwd.hpp>

#include <vector>

namespace spectra
{

struct CommandContext;
struct CommandDescriptor;
class FigureManager;
class DocumentSession;

// (ImGui builds only.)  Closes figure @p id the way Ctrl+W does: asks Save / Don't Save / Cancel
// when it has unsaved changes, and closing the last figure leaves the start
// page instead of the window.  Used by figure.close and the pane tab close
// button, middle click and "Close" context menu item.
void request_close_figure(FigureManager& fig_mgr, DocumentSession& document, FigureId id);

// Each function returns a list of descriptors for its category.
std::vector<CommandDescriptor> make_view_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_edit_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_file_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_figure_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_series_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_animation_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_panel_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_theme_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_tools_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_data_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_plot_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_app_commands(CommandContext& ctx);
std::vector<CommandDescriptor> make_style_commands(CommandContext& ctx);

}   // namespace spectra
