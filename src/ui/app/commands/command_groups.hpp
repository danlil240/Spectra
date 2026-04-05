// command_groups.hpp — Forward declarations for per-category command factories.

#pragma once

#include <vector>

namespace spectra
{

struct CommandContext;
struct CommandDescriptor;

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
std::vector<CommandDescriptor> make_app_commands(CommandContext& ctx);

}   // namespace spectra
