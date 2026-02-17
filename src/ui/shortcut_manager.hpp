#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{

class CommandRegistry;

// Modifier flags (matching GLFW modifier bits)
enum class KeyMod : uint8_t
{
    None = 0,
    Shift = 0x01,
    Control = 0x02,
    Alt = 0x04,
    Super = 0x08,
};

inline KeyMod operator|(KeyMod a, KeyMod b)
{
    return static_cast<KeyMod>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline KeyMod operator&(KeyMod a, KeyMod b)
{
    return static_cast<KeyMod>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool has_mod(KeyMod mods, KeyMod flag)
{
    return (static_cast<uint8_t>(mods) & static_cast<uint8_t>(flag)) != 0;
}

// A keyboard shortcut: key + modifiers.
struct Shortcut
{
    int key = 0;  // GLFW key code
    KeyMod mods = KeyMod::None;

    bool operator==(const Shortcut& o) const { return key == o.key && mods == o.mods; }
    bool operator!=(const Shortcut& o) const { return !(*this == o); }

    // Convert to human-readable string, e.g. "Ctrl+K"
    std::string to_string() const;

    // Parse from human-readable string. Returns empty shortcut on failure.
    static Shortcut from_string(const std::string& str);

    // Check if this shortcut is valid (has a key)
    bool valid() const { return key != 0; }
};

struct ShortcutHash
{
    size_t operator()(const Shortcut& s) const
    {
        return std::hash<int>()(s.key) ^ (std::hash<uint8_t>()(static_cast<uint8_t>(s.mods)) << 16);
    }
};

// Binding: shortcut → command id.
struct ShortcutBinding
{
    Shortcut shortcut;
    std::string command_id;
};

// Manages keyboard shortcut bindings and dispatches key events to commands.
// Thread-safe for bind/unbind. on_key should be called from the main thread.
class ShortcutManager
{
   public:
    ShortcutManager() = default;
    ~ShortcutManager() = default;

    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    // Set the command registry to execute commands through.
    void set_command_registry(CommandRegistry* registry) { registry_ = registry; }
    CommandRegistry* command_registry() const { return registry_; }

    // Bind a shortcut to a command id. Replaces existing binding for that shortcut.
    void bind(Shortcut shortcut, const std::string& command_id);

    // Unbind a shortcut.
    void unbind(const Shortcut& shortcut);

    // Unbind all shortcuts for a command.
    void unbind_command(const std::string& command_id);

    // Get the command id bound to a shortcut. Empty string if unbound.
    std::string command_for_shortcut(const Shortcut& shortcut) const;

    // Get the shortcut bound to a command. Returns invalid shortcut if unbound.
    Shortcut shortcut_for_command(const std::string& command_id) const;

    // Get all bindings.
    std::vector<ShortcutBinding> all_bindings() const;

    // Handle a key event. Returns true if a command was executed.
    // key: GLFW key code, action: GLFW_PRESS/RELEASE/REPEAT, mods: GLFW modifier bits.
    bool on_key(int key, int action, int mods);

    // Register all default shortcuts (call once at startup).
    void register_defaults();

    // Total number of bindings.
    size_t count() const;

    // Clear all bindings.
    void clear();

   private:
    CommandRegistry* registry_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<Shortcut, std::string, ShortcutHash> bindings_;

    // GLFW action constants (to avoid including GLFW in header).
    // Named with kGlfw prefix to avoid macro conflicts when GLFW is included.
    static constexpr int kGlfwPress = 1;
};

}  // namespace spectra
