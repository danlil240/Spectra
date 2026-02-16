// Shortcut Configuration Demo
// Demonstrates shortcut persistence, custom keybindings, and save/load functionality
//
// This example shows:
// - Creating custom shortcut overrides
// - Saving shortcuts to JSON file
// - Loading shortcuts from JSON file
// - Applying shortcuts to the ShortcutManager
// - Callback system for shortcut changes

#include <iostream>
#include <spectra/spectra.hpp>
#include <unordered_map>

#include "../src/ui/shortcut_config.hpp"

void demo_shortcut_persistence()
{
    std::cout << "=== Shortcut Configuration Demo ===\n\n";

    // Create a ShortcutConfig instance
    spectra::ShortcutConfig config;

    std::cout << "1. Setting custom shortcut overrides...\n";

    // Override some default shortcuts with custom keybindings
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.zoom_in", "Ctrl+Plus");
    config.set_override("view.zoom_out", "Ctrl+Minus");
    config.set_override("panel.toggle_inspector", "I");
    config.set_override("panel.toggle_timeline", "T");
    config.set_override("anim.toggle_play", "Space");
    config.set_override("theme.dark", "Ctrl+Shift+D");
    config.set_override("theme.light", "Ctrl+Shift+L");

    std::cout << "   - view.reset: Ctrl+R (was Home)\n";
    std::cout << "   - view.zoom_in: Ctrl+Plus (was +)\n";
    std::cout << "   - view.zoom_out: Ctrl+Minus (was -)\n";
    std::cout << "   - panel.toggle_inspector: I (was Ctrl+I)\n";
    std::cout << "   - panel.toggle_timeline: T (was T)\n";
    std::cout << "   - anim.toggle_play: Space (was Space)\n";
    std::cout << "   - theme.dark: Ctrl+Shift+D (was Ctrl+Shift+D)\n";
    std::cout << "   - theme.light: Ctrl+Shift+L (was Ctrl+Shift+L)\n";

    std::cout << "\n2. Saving shortcuts to JSON file...\n";

    // Save to file
    const std::string filename = "custom_shortcuts.json";
    if (config.save(filename))
    {
        std::cout << "   ✓ Saved to " << filename << "\n";
    }
    else
    {
        std::cout << "   ✗ Failed to save to file\n";
        return;
    }

    std::cout << "\n3. Loading shortcuts from JSON file...\n";

    // Create a new config and load from file
    spectra::ShortcutConfig loaded_config;
    if (loaded_config.load(filename))
    {
        std::cout << "   ✓ Loaded from " << filename << "\n";
    }
    else
    {
        std::cout << "   ✗ Failed to load from file\n";
        return;
    }

    std::cout << "\n4. Verifying loaded shortcuts...\n";

    // Check that the shortcuts were loaded correctly
    auto verify_shortcut = [&](const std::string& command_id, const std::string& expected)
    {
        auto overrides = loaded_config.overrides();
        bool found = false;
        for (const auto& override : overrides)
        {
            if (override.command_id == command_id && override.shortcut_str == expected)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            std::cout << "   ✓ " << command_id << " -> " << expected << "\n";
        }
        else
        {
            std::cout << "   ✗ " << command_id << " -> expected " << expected << ", not found\n";
        }
    };

    verify_shortcut("view.reset", "Ctrl+R");
    verify_shortcut("view.zoom_in", "Ctrl+Plus");
    verify_shortcut("view.zoom_out", "Ctrl+Minus");
    verify_shortcut("panel.toggle_inspector", "I");
    verify_shortcut("panel.toggle_timeline", "T");
    verify_shortcut("anim.toggle_play", "Space");

    std::cout << "\n5. Demonstrating JSON serialization...\n";

    // Show the JSON content
    std::string json = config.serialize();
    std::cout << "   JSON content (first 200 chars):\n";
    std::cout << "   " << json.substr(0, 200) << "...\n";

    std::cout << "\n6. Demonstrating callback system...\n";

    // Set up change callbacks
    config.set_on_change([]() { std::cout << "   📝 Shortcut configuration changed\n"; });

    // Modify a shortcut to trigger callback
    std::cout << "   Modifying 'view.reset' shortcut...\n";
    config.set_override("view.reset", "Ctrl+Shift+R");

    std::cout << "\n7. Demonstrating bulk operations...\n";

    // Set multiple shortcuts at once
    std::unordered_map<std::string, std::string> bulk_overrides = {{"file.save", "Ctrl+S"},
                                                                   {"file.open", "Ctrl+O"},
                                                                   {"file.export", "Ctrl+E"},
                                                                   {"edit.undo", "Ctrl+Z"},
                                                                   {"edit.redo", "Ctrl+Y"}};

    // Set multiple shortcuts at once
    for (const auto& [command, shortcut] : bulk_overrides)
    {
        config.set_override(command, shortcut);
    }
    std::cout << "   Set " << bulk_overrides.size() << " shortcuts in bulk\n";

    std::cout << "\n8. Demonstrating removal...\n";

    // Remove a specific override
    config.remove_override("view.reset");
    std::cout << "   Removed override for 'view.reset'\n";

    // Check if it's gone
    bool has_override = config.has_override("view.reset");
    if (!has_override)
    {
        std::cout << "   ✓ 'view.reset' override successfully removed\n";
    }
    else
    {
        std::cout << "   ✗ 'view.reset' override still exists\n";
    }

    std::cout << "\n9. Demonstrating clear operation...\n";

    // Clear all overrides
    size_t count_before = config.override_count();
    config.reset_all();
    size_t count_after = config.override_count();

    std::cout << "   Cleared " << count_before << " overrides, " << count_after << " remain\n";

    std::cout << "\n=== Integration with ShortcutManager ===\n";
    std::cout << "In a real application, you would:\n";
    std::cout << "1. Load ShortcutConfig at startup\n";
    std::cout << "2. Apply overrides to ShortcutManager: config.apply_overrides()\n";
    std::cout << "3. Save changes on exit: config.save('shortcuts.json')\n";
    std::cout << "4. Handle shortcut changes in real-time via callbacks\n";

    std::cout << "\n=== Demo Complete ===\n";

    // Clean up the demo file
    std::remove(filename.c_str());
}

int main()
{
    demo_shortcut_persistence();
    return 0;
}
