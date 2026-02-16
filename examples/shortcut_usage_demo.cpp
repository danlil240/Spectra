// Shortcut Usage Demo
// Demonstrates how shortcut persistence works in Plotix (conceptual example)
//
// This example shows the usage patterns for:
// - Custom shortcut configuration
// - JSON persistence
// - Integration with the command system

#include <fstream>
#include <iostream>
#include <plotix/plotix.hpp>

void demo_shortcut_concepts()
{
    std::cout << "=== Shortcut Configuration Usage Guide ===\n\n";

    std::cout << "📋 OVERVIEW\n";
    std::cout << "Plotix provides a persistent shortcut configuration system that allows\n";
    std::cout << "users to customize keybindings and have them saved across sessions.\n\n";

    std::cout << "🔧 CORE CONCEPTS\n\n";

    std::cout << "1. ShortcutConfig Class\n";
    std::cout << "   - Manages user shortcut overrides separately from defaults\n";
    std::cout << "   - Provides JSON serialization for persistence\n";
    std::cout << "   - Thread-safe operations\n";
    std::cout << "   - Callback system for change notifications\n\n";

    std::cout << "2. Integration Pattern\n";
    std::cout << "   - App loads shortcuts at startup from ~/.plotix/shortcuts.json\n";
    std::cout << "   - Overrides are applied to ShortcutManager\n";
    std::cout << "   - Changes are automatically saved on exit\n";
    std::cout << "   - Real-time updates via callback system\n\n";

    std::cout << "💻 USAGE EXAMPLES\n\n";

    std::cout << "Example 1: Basic Shortcut Override\n";
    std::cout << "```cpp\n";
    std::cout << "// Create shortcut config\n";
    std::cout << "ShortcutConfig config;\n";
    std::cout << "\n";
    std::cout << "// Override default shortcuts\n";
    std::cout << "config.set_override(\"view.reset\", \"Ctrl+R\");\n";
    std::cout << "config.set_override(\"panel.toggle_inspector\", \"I\");\n";
    std::cout << "config.set_override(\"anim.toggle_play\", \"Space\");\n";
    std::cout << "```\n\n";

    std::cout << "Example 2: Save/Load Operations\n";
    std::cout << "```cpp\n";
    std::cout << "// Save to JSON file\n";
    std::cout << "config.save_to_file(\"custom_shortcuts.json\");\n";
    std::cout << "\n";
    std::cout << "// Load from JSON file\n";
    std::cout << "ShortcutConfig loaded;\n";
    std::cout << "if (loaded.load_from_file(\"custom_shortcuts.json\")) {\n";
    std::cout << "    // Apply to ShortcutManager\n";
    std::cout << "    loaded.apply_to(&shortcut_manager);\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "Example 3: Callback System\n";
    std::cout << "```cpp\n";
    std::cout << "// Set up change notifications\n";
    std::cout
        << "config.set_change_callback([](const std::string& cmd, const std::string& shortcut) {\n";
    std::cout
        << "    std::cout << \"Shortcut changed: \" << cmd << \" -> \" << shortcut << \"\\n\";\n";
    std::cout << "    \n";
    std::cout << "    // Auto-save on changes\n";
    std::cout << "    config.save_to_file(\"shortcuts.json\");\n";
    std::cout << "});\n";
    std::cout << "```\n\n";

    std::cout << "Example 4: Bulk Operations\n";
    std::cout << "```cpp\n";
    std::cout << "// Set multiple shortcuts at once\n";
    std::cout << "std::unordered_map<std::string, std::string> overrides = {\n";
    std::cout << "    {\"file.save\", \"Ctrl+S\"},\n";
    std::cout << "    {\"file.open\", \"Ctrl+O\"},\n";
    std::cout << "    {\"edit.undo\", \"Ctrl+Z\"},\n";
    std::cout << "    {\"edit.redo\", \"Ctrl+Y\"}\n";
    std::cout << "};\n";
    std::cout << "config.set_overrides(overrides);\n";
    std::cout << "```\n\n";

    std::cout << "📁 FILE FORMAT\n\n";
    std::cout << "The shortcuts are stored in JSON format:\n";
    std::cout << "```json\n";
    std::cout << "{\n";
    std::cout << "  \"version\": 1,\n";
    std::cout << "  \"shortcuts\": {\n";
    std::cout << "    \"view.reset\": \"Ctrl+R\",\n";
    std::cout << "    \"panel.toggle_inspector\": \"I\",\n";
    std::cout << "    \"anim.toggle_play\": \"Space\"\n";
    std::cout << "  }\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "🔄 INTEGRATION WORKFLOW\n\n";
    std::cout << "1. Application Startup:\n";
    std::cout << "   - Load ShortcutConfig from user config directory\n";
    std::cout << "   - Apply overrides to ShortcutManager\n";
    std::cout << "   - Set up change callbacks for auto-save\n\n";

    std::cout << "2. During Runtime:\n";
    std::cout << "   - User changes shortcuts via preferences UI\n";
    std::cout << "   - ShortcutConfig updates automatically\n";
    std::cout << "   - Callbacks trigger auto-save\n";
    std::cout << "   - ShortcutManager reflects changes immediately\n\n";

    std::cout << "3. Application Shutdown:\n";
    std::cout << "   - Final save of ShortcutConfig to file\n";
    std::cout << "   - Clean up resources\n\n";

    std::cout << "⚡ ADVANCED FEATURES\n\n";
    std::cout << "• Conflict Detection: Warns when new shortcuts conflict with existing ones\n";
    std::cout << "• Validation: Ensures shortcut strings are properly formatted\n";
    std::cout << "• Reset to Defaults: Clear all overrides with one call\n";
    std::cout << "• Import/Export: Share shortcut configurations between users\n";
    std::cout << "• Profile Support: Multiple shortcut profiles for different workflows\n\n";

    std::cout << "🎯 BEST PRACTICES\n\n";
    std::cout << "• Always validate shortcut strings before setting them\n";
    std::cout << "• Use descriptive command IDs that match the UI labels\n";
    std::cout << "• Provide visual feedback when shortcuts are changed\n";
    std::cout << "• Auto-save changes to prevent data loss\n";
    std::cout << "• Offer reset-to-defaults option for easy recovery\n\n";

    std::cout << "📚 REFERENCE IMPLEMENTATION\n";
    std::cout << "The actual implementation is in:\n";
    std::cout << "• src/ui/shortcut_config.hpp - Class definition\n";
    std::cout << "• src/ui/shortcut_config.cpp - Full implementation\n";
    std::cout << "• tests/unit/test_shortcut_config.cpp - Comprehensive tests\n\n";

    std::cout << "=== Demo Complete ===\n";
    std::cout << "This demonstrates the concepts and usage patterns for shortcut\n";
    std::cout << "persistence in Plotix. The actual API requires internal headers.\n";
}

int main()
{
    demo_shortcut_concepts();
    return 0;
}
