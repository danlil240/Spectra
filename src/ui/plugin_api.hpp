#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace spectra
{

class CommandRegistry;
class ShortcutManager;
class UndoManager;

// ─── Stable C ABI for plugins ────────────────────────────────────────────────
//
// Plugins are shared libraries (.so / .dll / .dylib) that export a single
// entry point: spectra_plugin_init(). The host calls this with a PluginContext
// that provides access to command registration, shortcuts, and undo.
//
// The C ABI ensures binary compatibility across compiler versions.

extern "C"
{
// Plugin API version — bump major on breaking changes.
#define SPECTRA_PLUGIN_API_VERSION_MAJOR 1
#define SPECTRA_PLUGIN_API_VERSION_MINOR 0

    // Opaque handles (pointers cast to void* for C ABI stability)
    typedef void* SpectraCommandRegistry;
    typedef void* SpectraShortcutManager;
    typedef void* SpectraUndoManager;

    // Plugin context passed to plugin_init.
    struct SpectraPluginContext
    {
        uint32_t               api_version_major;
        uint32_t               api_version_minor;
        SpectraCommandRegistry command_registry;
        SpectraShortcutManager shortcut_manager;
        SpectraUndoManager     undo_manager;
    };

    // Plugin info returned by plugin_init.
    struct SpectraPluginInfo
    {
        const char* name;                // Human-readable plugin name
        const char* version;             // Plugin version string
        const char* author;              // Author name
        const char* description;         // Short description
        uint32_t    api_version_major;   // API version the plugin was built against
        uint32_t    api_version_minor;
    };

    // C ABI functions for command registration
    typedef void (*SpectraCommandCallback)(void* user_data);

    struct SpectraCommandDesc
    {
        const char*            id;
        const char*            label;
        const char*            category;
        const char*            shortcut_hint;
        SpectraCommandCallback callback;
        void*                  user_data;
    };

    // Plugin entry point signature.
    // Returns 0 on success, non-zero on failure.
    typedef int (*SpectraPluginInitFn)(const SpectraPluginContext* ctx,
                                       SpectraPluginInfo*          info_out);

    // Plugin cleanup signature (optional).
    typedef void (*SpectraPluginShutdownFn)(void);

    // ─── C ABI host functions (called by plugins) ────────────────────────────────

    // Register a command via C ABI.
    int spectra_register_command(SpectraCommandRegistry registry, const SpectraCommandDesc* desc);

    // Unregister a command via C ABI.
    int spectra_unregister_command(SpectraCommandRegistry registry, const char* command_id);

    // Execute a command via C ABI.
    int spectra_execute_command(SpectraCommandRegistry registry, const char* command_id);

    // Bind a shortcut via C ABI.
    int spectra_bind_shortcut(SpectraShortcutManager manager,
                              const char*            shortcut_str,
                              const char*            command_id);

    // Push an undo action via C ABI.
    int spectra_push_undo(SpectraUndoManager     manager,
                          const char*            description,
                          SpectraCommandCallback undo_fn,
                          void*                  undo_data,
                          SpectraCommandCallback redo_fn,
                          void*                  redo_data);

}   // extern "C"

// ─── C++ Plugin Manager ──────────────────────────────────────────────────────

// Represents a loaded plugin.
struct PluginEntry
{
    std::string              name;
    std::string              version;
    std::string              author;
    std::string              description;
    std::string              path;   // Path to the shared library
    bool                     loaded      = false;
    bool                     enabled     = true;
    void*                    handle      = nullptr;   // dlopen/LoadLibrary handle
    SpectraPluginShutdownFn  shutdown_fn = nullptr;
    std::vector<std::string> registered_commands;   // Commands registered by this plugin
};

// Manages plugin lifecycle: discovery, loading, unloading.
// Thread-safe.
class PluginManager
{
   public:
    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&)            = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Set the host services that plugins can access.
    void set_command_registry(CommandRegistry* reg) { registry_ = reg; }
    void set_shortcut_manager(ShortcutManager* mgr) { shortcut_mgr_ = mgr; }
    void set_undo_manager(UndoManager* undo) { undo_mgr_ = undo; }

    // Load a plugin from a shared library path.
    // Returns true on success.
    bool load_plugin(const std::string& path);

    // Unload a plugin by name. Calls shutdown, unregisters commands.
    bool unload_plugin(const std::string& name);

    // Unload all plugins.
    void unload_all();

    // Get list of loaded plugins.
    std::vector<PluginEntry> plugins() const;

    // Get a plugin by name.
    const PluginEntry* find_plugin(const std::string& name) const;

    // Number of loaded plugins.
    size_t plugin_count() const;

    // Enable/disable a plugin (disabled plugins' commands are disabled).
    void set_plugin_enabled(const std::string& name, bool enabled);

    // Discover plugins in a directory (scans for .so/.dll/.dylib files).
    std::vector<std::string> discover(const std::string& directory) const;

    // Default plugin directory (~/.config/spectra/plugins/).
    static std::string default_plugin_dir();

    // Serialize plugin state (enabled/disabled) to JSON.
    std::string serialize_state() const;

    // Deserialize plugin state from JSON.
    bool deserialize_state(const std::string& json);

   private:
    CommandRegistry*         registry_     = nullptr;
    ShortcutManager*         shortcut_mgr_ = nullptr;
    UndoManager*             undo_mgr_     = nullptr;
    mutable std::mutex       mutex_;
    std::vector<PluginEntry> plugins_;

    SpectraPluginContext make_context() const;
};

}   // namespace spectra
