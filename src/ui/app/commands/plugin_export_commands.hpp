// plugin_export_commands.hpp — Mirrors plugin export formats as File commands.
//
// Every format in an ExportFormatRegistry gets a "file.export_plugin.<name>"
// command (the id both File menus execute).  The command serializes the active
// figure to the JSON documented on ExportContext::figure_json, asks for an
// output path and calls ExportFormatRegistry::export_figure().  Commands follow
// the registry: they appear when a format is registered and disappear when it
// is unregistered (for example when its plugin unloads).

#pragma once

#include <spectra/fwd.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "io/export_registry.hpp"

namespace spectra
{

class CommandRegistry;

// Command id for the plugin export format called format_name.
std::string plugin_export_command_id(std::string_view format_name);

// Figure data handed to plugin exporters:
//   {"axes":[{"index":0,"title":"...","xlabel":"...","ylabel":"...",
//             "series":[{"label":"...","type":"line","x":[...],"y":[...]}]}]}
// Covers 2D line and scatter series; x/y include the series' x/y offsets and
// non-finite values are written as null.
std::string figure_to_export_json(const Figure& figure);

// Returns the destination path for an export, or nullopt to cancel.
using PluginExportPathProvider =
    std::function<std::optional<std::string>(const ExportFormatInfo& format)>;

class PluginExportCommands
{
   public:
    struct Options
    {
        ExportFormatRegistry* formats       = nullptr;
        CommandRegistry*      commands      = nullptr;
        Figure**              active_figure = nullptr;   // read when a command runs
        // Directory remembered by the save dialog (may be null).
        std::string* last_export_dir = nullptr;
        // Overrides the native save dialog (tests, automation).  When empty the
        // native dialog is used; it returns nothing when dialogs are disabled,
        // in which case the export is skipped.
        PluginExportPathProvider ask_path;
    };

    PluginExportCommands() = default;
    ~PluginExportCommands();

    PluginExportCommands(const PluginExportCommands&)            = delete;
    PluginExportCommands& operator=(const PluginExportCommands&) = delete;

    // Registers a command for every current format and follows later changes.
    // Re-attaching first detaches from the previous registries.
    void attach(Options options);

    // Stops following the registry and removes the commands this object added.
    void detach();

    bool attached() const { return options_.formats != nullptr; }

   private:
    void add_command(const ExportFormatInfo& format);
    void remove_command(const std::string& format_name);

    Options                          options_;
    ExportFormatRegistry::ListenerId listener_id_ = 0;
    std::vector<std::string>         registered_ids_;
};

}   // namespace spectra
