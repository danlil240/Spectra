#pragma once

#include <functional>
#include <string>
#include <vector>

namespace spectra
{

class ShortcutManager;

// Persistent shortcut configuration: save/load custom keybindings to JSON.
// Tracks user overrides separately from defaults so reset-to-defaults is trivial.
// Thread-safe: all public methods are safe to call from any thread.
class ShortcutConfig
{
   public:
    ShortcutConfig() = default;
    ~ShortcutConfig() = default;

    ShortcutConfig(const ShortcutConfig&) = delete;
    ShortcutConfig& operator=(const ShortcutConfig&) = delete;

    // A single keybinding override (user customization).
    struct BindingOverride
    {
        std::string command_id;    // e.g. "view.reset"
        std::string shortcut_str;  // e.g. "Ctrl+R" or "" to unbind
        bool removed = false;      // true if user explicitly removed the binding
    };

    // Set the ShortcutManager to apply overrides to.
    void set_shortcut_manager(ShortcutManager* mgr) { manager_ = mgr; }
    ShortcutManager* shortcut_manager() const { return manager_; }

    // Record a user override: rebind a command to a new shortcut.
    // Pass empty shortcut_str to unbind.
    void set_override(const std::string& command_id, const std::string& shortcut_str);

    // Remove a user override (reverts to default for that command).
    void remove_override(const std::string& command_id);

    // Check if a command has a user override.
    bool has_override(const std::string& command_id) const;

    // Get all user overrides.
    std::vector<BindingOverride> overrides() const;

    // Number of user overrides.
    size_t override_count() const;

    // Clear all user overrides (reset to defaults).
    void reset_all();

    // Apply all overrides to the ShortcutManager.
    // Call after register_defaults() to layer user customizations on top.
    void apply_overrides();

    // Save keybinding overrides to a JSON file. Returns true on success.
    bool save(const std::string& path) const;

    // Load keybinding overrides from a JSON file. Returns true on success.
    // Does NOT auto-apply; call apply_overrides() after loading.
    bool load(const std::string& path);

    // Default config file path (~/.config/spectra/keybindings.json).
    static std::string default_path();

    // Serialize overrides to JSON string.
    std::string serialize() const;

    // Deserialize overrides from JSON string. Returns true on success.
    bool deserialize(const std::string& json);

    // Callback when overrides change (for UI refresh).
    using ChangeCallback = std::function<void()>;
    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

   private:
    ShortcutManager* manager_ = nullptr;
    std::vector<BindingOverride> overrides_;
    ChangeCallback on_change_;

    void notify_change();
};

}  // namespace spectra
