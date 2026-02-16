// Plugin API Demo
// Demonstrates how to use the Plugin API for external extensions
//
// This example shows the usage patterns for:
// - Creating external plugins with stable C ABI
// - Registering commands and shortcuts
// - Plugin lifecycle management
// - Communication between plugins and host application

#include <fstream>
#include <iostream>
#include <spectra/spectra.hpp>

void demo_plugin_api_concepts()
{
    std::cout << "=== Plugin API Usage Guide ===\n\n";

    std::cout << "📋 OVERVIEW\n";
    std::cout << "Spectra provides a stable C ABI plugin system that allows external\n";
    std::cout << "developers to create extensions that can register commands, handle\n";
    std::cout << "shortcuts, and interact with the application's core services.\n\n";

    std::cout << "🔧 CORE CONCEPTS\n\n";

    std::cout << "1. Stable C ABI\n";
    std::cout << "   - C-compatible interface for language-agnostic plugin development\n";
    std::cout << "   - Versioned API to ensure compatibility\n";
    std::cout << "   - Memory-safe callbacks and handles\n";
    std::cout << "   - Error handling with return codes\n\n";

    std::cout << "2. PluginManager\n";
    std::cout << "   - Dynamic loading/unloading of plugin libraries\n";
    std::cout << "   - Plugin lifecycle management (initialize/shutdown)\n";
    std::cout << "   - Dependency resolution and ordering\n";
    std::cout << "   - Error isolation and recovery\n\n";

    std::cout << "3. Plugin Context\n";
    std::cout << "   - Safe handle to application services\n";
    std::cout << "   - Access to CommandRegistry, ShortcutManager, UndoManager\n";
    std::cout << "   - Plugin-specific data storage\n";
    std::cout << "   - Logging and error reporting\n\n";

    std::cout << "💻 PLUGIN DEVELOPMENT EXAMPLES\n\n";

    std::cout << "Example 1: Basic Plugin Structure\n";
    std::cout << "```c\n";
    std::cout << "// plugin_example.c\n";
    std::cout << "#include \"plugin_api.h\"\n";
    std::cout << "\n";
    std::cout << "// Plugin entry point\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT int spectra_plugin_init(PluginContext* ctx) {\n";
    std::cout << "    // Register a custom command\n";
    std::cout << "    ctx->register_command(ctx, \"example.hello\", \"Say Hello\", \n";
    std::cout << "                           hello_callback, \"Ctrl+H\", \"Examples\");\n";
    std::cout << "    \n";
    std::cout << "    return SPECTRA_PLUGIN_SUCCESS;\n";
    std::cout << "}\n";
    std::cout << "\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT void spectra_plugin_shutdown(PluginContext* ctx) {\n";
    std::cout << "    // Cleanup resources\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "Example 2: Command Implementation\n";
    std::cout << "```c\n";
    std::cout << "void hello_callback(PluginContext* ctx, void* user_data) {\n";
    std::cout << "    // Access application services\n";
    std::cout << "    CommandRegistry* registry = ctx->get_command_registry(ctx);\n";
    std::cout << "    \n";
    std::cout << "    // Log a message\n";
    std::cout << "    ctx->log_info(ctx, \"Hello from plugin!\");\n";
    std::cout << "    \n";
    std::cout << "    // Show a message box (if UI is available)\n";
    std::cout << "    ctx->show_message(ctx, \"Hello\", \"Plugin says hello!\");\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "Example 3: Advanced Plugin with Undo Support\n";
    std::cout << "```c\n";
    std::cout << "void add_random_data_callback(PluginContext* ctx, void* user_data) {\n";
    std::cout << "    // Get the current figure\n";
    std::cout << "    Figure* fig = ctx->get_active_figure(ctx);\n";
    std::cout << "    if (!fig) return;\n";
    std::cout << "    \n";
    std::cout << "    // Store previous state for undo\n";
    std::cout << "    char* old_data = ctx->serialize_figure(ctx, fig);\n";
    std::cout << "    \n";
    std::cout << "    // Add random data to the figure\n";
    std::cout << "    ctx->add_random_series(ctx, fig);\n";
    std::cout << "    \n";
    std::cout << "    // Register undo action\n";
    std::cout << "    ctx->register_undo_action(ctx, \"Add Random Data\",\n";
    std::cout << "        old_data,  // Data to restore\n";
    std::cout << "        restore_figure_callback);\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "Example 4: Plugin Configuration\n";
    std::cout << "```c\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT int spectra_plugin_init(PluginContext* ctx) {\n";
    std::cout << "    // Get plugin configuration\n";
    std::cout << "    const char* config = ctx->get_plugin_config(ctx);\n";
    std::cout << "    \n";
    std::cout << "    // Parse configuration (JSON format)\n";
    std::cout << "    PluginConfig cfg = parse_config(config);\n";
    std::cout << "    \n";
    std::cout << "    // Configure plugin behavior\n";
    std::cout << "    if (cfg.enable_advanced_features) {\n";
    std::cout << "        register_advanced_commands(ctx);\n";
    std::cout << "    }\n";
    std::cout << "    \n";
    std::cout << "    return SPECTRA_PLUGIN_SUCCESS;\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "🏭 HOST APPLICATION INTEGRATION\n\n";

    std::cout << "Example 5: Loading Plugins\n";
    std::cout << "```cpp\n";
    std::cout << "// In the main application\n";
    std::cout << "PluginManager plugin_manager;\n";
    std::cout << "\n";
    std::cout << "// Set up application services for plugins\n";
    std::cout << "plugin_manager.set_command_registry(&command_registry);\n";
    std::cout << "plugin_manager.set_shortcut_manager(&shortcut_manager);\n";
    std::cout << "plugin_manager.set_undo_manager(&undo_manager);\n";
    std::cout << "\n";
    std::cout << "// Load plugins from directory\n";
    std::cout << "std::vector<std::string> plugin_paths = {\n";
    std::cout << "    \"plugins/data_import.so\",\n";
    std::cout << "    \"plugins/export_tools.so\",\n";
    std::cout << "    \"plugins/analysis.so\"\n";
    std::cout << "};\n";
    std::cout << "\n";
    std::cout << "for (const auto& path : plugin_paths) {\n";
    std::cout << "    if (plugin_manager.load_plugin(path)) {\n";
    std::cout << "        std::cout << \"Loaded plugin: \" << path << \"\\n\";\n";
    std::cout << "    }\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "Example 6: Plugin Discovery\n";
    std::cout << "```cpp\n";
    std::cout << "// Discover plugins in a directory\n";
    std::cout << "std::string plugin_dir = \"./plugins\";\n";
    std::cout << "auto discovered = plugin_manager.discover_plugins(plugin_dir);\n";
    std::cout << "\n";
    std::cout << "for (const auto& info : discovered) {\n";
    std::cout << "    std::cout << \"Found plugin: \" << info.name\n";
    std::cout << "              << \" version: \" << info.version\n";
    std::cout << "              << \" author: \" << info.author << \"\\n\";\n";
    std::cout << "    \n";
    std::cout << "    // Load if enabled\n";
    std::cout << "    if (info.auto_load) {\n";
    std::cout << "        plugin_manager.load_plugin(info.path);\n";
    std::cout << "    }\n";
    std::cout << "}\n";
    std::cout << "```\n\n";

    std::cout << "📁 PLUGIN FILE FORMAT\n\n";
    std::cout << "Plugins are shared libraries (.so, .dll, .dylib) with specific exports:\n";
    std::cout << "```c\n";
    std::cout << "// Required exports\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT const char* spectra_plugin_name();\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT const char* spectra_plugin_version();\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT const char* spectra_plugin_author();\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT int spectra_plugin_init(PluginContext* ctx);\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT void spectra_plugin_shutdown(PluginContext* ctx);\n";
    std::cout << "\n";
    std::cout << "// Optional exports\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT const char* spectra_plugin_description();\n";
    std::cout << "SPECTRA_PLUGIN_EXPORT const char* spectra_plugin_dependencies();\n";
    std::cout << "```\n\n";

    std::cout << "⚡ ADVANCED FEATURES\n\n";
    std::cout << "• Plugin Dependencies: Declare and resolve plugin dependencies\n";
    std::cout << "• Version Compatibility: API versioning for forward/backward compatibility\n";
    std::cout << "• Sandboxing: Isolate plugins from core application memory\n";
    std::cout << "• Hot Reloading: Reload plugins without restarting application\n";
    std::cout << "• Plugin Communication: Allow plugins to communicate with each other\n";
    std::cout << "• Resource Management: Automatic cleanup of plugin resources\n";
    std::cout << "• Error Recovery: Handle plugin crashes gracefully\n\n";

    std::cout << "🎯 BEST PRACTICES\n\n";
    std::cout << "• Always check return codes from API calls\n";
    std::cout << "• Use the provided logging system instead of stdout/stderr\n";
    std::cout << "• Register undo actions for any destructive operations\n";
    std::cout << "• Clean up all resources in the shutdown function\n";
    std::cout << "• Use semantic versioning for plugin compatibility\n";
    std::cout << "• Provide clear descriptions and help text for commands\n";
    std::cout << "• Handle errors gracefully and report them through the API\n\n";

    std::cout << "📚 REFERENCE IMPLEMENTATION\n";
    std::cout << "The actual implementation is in:\n";
    std::cout << "• src/ui/plugin_api.hpp - C ABI definitions and PluginManager\n";
    std::cout << "• src/ui/plugin_api.cpp - Host implementation and loading logic\n";
    std::cout << "• tests/unit/test_plugin_api.cpp - Comprehensive API tests\n\n";

    std::cout << "🔧 PLUGIN DEVELOPMENT WORKFLOW\n\n";
    std::cout << "1. Setup: Include plugin_api.h and link against the ABI\n";
    std::cout << "2. Implementation: Write plugin logic using C ABI functions\n";
    std::cout << "3. Testing: Use the test framework to verify plugin behavior\n";
    std::cout << "4. Packaging: Build as shared library with proper exports\n";
    std::cout << "5. Distribution: Provide plugin metadata and dependencies\n";
    std::cout << "6. Integration: Load and test in host application\n\n";

    std::cout << "=== Demo Complete ===\n";
    std::cout << "This demonstrates the concepts and usage patterns for the\n";
    std::cout << "Plugin API in Spectra. The actual API requires internal headers\n";
    std::cout << "and is designed for external plugin developers.\n";
}

int main()
{
    demo_plugin_api_concepts();
    return 0;
}
