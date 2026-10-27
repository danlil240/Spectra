#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace spectra
{

// A single command that can be executed, searched, and bound to shortcuts.
struct Command
{
    std::string           id;         // Unique identifier, e.g. "view.reset"
    std::string           label;      // Display label, e.g. "Reset View"
    std::string           category;   // Category for grouping, e.g. "View"
    std::string           shortcut;   // Human-readable shortcut, e.g. "Ctrl+R"
    std::function<void()> callback;
    bool                  enabled = true;

    // Icon hint (maps to ui::Icon enum value, 0 = none)
    uint16_t icon = 0;
};

// Result from a fuzzy search query.
struct CommandSearchResult
{
    const Command* command = nullptr;
    int            score   = 0;   // Higher = better match
    // Display group override (e.g. "Recent"). Empty = group under command->category.
    // Always points at static storage.
    std::string_view section;
};

// Options for the default (empty-query) command listing.
struct CommandListingOptions
{
    std::vector<std::string> featured_ids;         // Common actions, listed in this order
    std::vector<std::string> recent_exclude_ids;   // Never listed under "Recent"
    size_t                   max_recent = 5;
};

// Central registry for all application commands.
// Thread-safe: register/unregister/search/execute may be called from any thread.
class CommandRegistry
{
   public:
    CommandRegistry()  = default;
    ~CommandRegistry() = default;

    CommandRegistry(const CommandRegistry&)            = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    // Register a command. Overwrites if id already exists.
    void register_command(Command cmd);

    // Register a simple command with minimal args.
    void register_command(const std::string&    id,
                          const std::string&    label,
                          std::function<void()> callback,
                          const std::string&    shortcut = "",
                          const std::string&    category = "General",
                          uint16_t              icon     = 0);

    // Unregister a command by id.
    void unregister_command(const std::string& id);

    // Execute a command by id. Returns false if not found or disabled.
    bool execute(const std::string& id);

    // Fuzzy search across all commands. Returns results sorted by score (descending).
    // Empty query returns all commands (sorted by category, then label).
    std::vector<CommandSearchResult> search(const std::string& query,
                                            size_t             max_results = 50) const;

    // Listing shown for an empty palette query: recently executed commands
    // (section "Recent"), then the featured commands that exist (section "Suggested"),
    // then every other command sorted by category, then label. No command appears twice.
    static constexpr std::string_view RECENT_SECTION    = "Recent";
    static constexpr std::string_view SUGGESTED_SECTION = "Suggested";
    std::vector<CommandSearchResult>  default_listing(const CommandListingOptions& options) const;

    // Get a command by id. Returns nullptr if not found.
    const Command* find(const std::string& id) const;

    // Get all registered commands.
    std::vector<const Command*> all_commands() const;

    // Get commands in a specific category.
    std::vector<const Command*> commands_in_category(const std::string& category) const;

    // Get all category names.
    std::vector<std::string> categories() const;

    // Total number of registered commands.
    size_t count() const;

    // Enable/disable a command.
    void set_enabled(const std::string& id, bool enabled);

    // Track recent commands (for "recent" section in palette).
    void                        record_execution(const std::string& id);
    std::vector<const Command*> recent_commands(size_t max_count = 10) const;
    void                        clear_recent();

   private:
    // Fuzzy match score: higher = better. Returns 0 if no match.
    static int fuzzy_score(const std::string& query, const std::string& text);

    mutable std::mutex                       mutex_;
    std::unordered_map<std::string, Command> commands_;
    std::vector<std::string>                 recent_ids_;   // Most recent first
    static constexpr size_t                  MAX_RECENT = 20;
};

}   // namespace spectra
