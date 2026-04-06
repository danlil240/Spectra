#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{

/// Parsed plugin manifest (plugin.json next to the plugin shared library).
///
/// Manifest format (JSON):
/// {
///   "name": "My Plugin",
///   "version": "1.2.0",
///   "api_version": "2.0",
///   "author": "Developer Name",
///   "description": "A data source for serial ports",
///   "capabilities": ["data_source", "overlay", "command"],
///   "dependencies": [],
///   "min_spectra_version": "0.9.0"
/// }
struct PluginManifest
{
    std::string              name;
    std::string              version;
    std::string              api_version;          // e.g. "2.0"
    std::string              author;
    std::string              description;
    std::vector<std::string> capabilities;         // e.g. ["data_source", "overlay"]
    std::vector<std::string> dependencies;         // plugin names this plugin depends on
    std::string              min_spectra_version;  // minimum Spectra version required

    /// Is this manifest valid (has required fields)?
    [[nodiscard]] bool is_valid() const;

    /// Parse api_version string "MAJOR.MINOR" into component integers.
    /// Returns {0, 0} on parse failure.
    [[nodiscard]] std::pair<uint32_t, uint32_t> parsed_api_version() const;

    /// Check if this plugin is compatible with the host API version.
    [[nodiscard]] bool is_api_compatible(uint32_t host_major, uint32_t host_minor) const;
};

/// Load and parse a plugin manifest from a JSON file.
/// Returns empty PluginManifest (with empty name) on failure.
PluginManifest load_plugin_manifest(const std::string& json_path);

/// Find the manifest path for a plugin library file.
/// Looks for "plugin.json" in the same directory as the library.
/// Returns empty string if not found.
std::string find_plugin_manifest_path(const std::string& library_path);

}   // namespace spectra
