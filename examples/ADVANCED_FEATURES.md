# Advanced Features Usage Guide

This directory contains examples and documentation for Plotix's advanced features: shortcut persistence and plugin API.

## Overview

Plotix provides two powerful extension mechanisms:

1. **Shortcut Persistence** - Save/load custom keybindings to JSON
2. **Plugin API** - Stable C ABI for external plugins to register commands

## Shortcut Configuration System

### Purpose
Allow users to customize keyboard shortcuts and have those preferences persist across application sessions.

### Key Components

#### ShortcutConfig Class
- **Location**: `src/ui/shortcut_config.hpp/.cpp`
- **Purpose**: Manages user shortcut overrides separately from defaults
- **Features**: JSON serialization, thread-safe operations, callback system

#### Integration Points
- **App Startup**: Load shortcuts from `~/.plotix/shortcuts.json`
- **Runtime**: Apply overrides to `ShortcutManager`
- **App Shutdown**: Save current configuration to file

### Usage Examples

#### Basic Override Pattern
```cpp
// Create shortcut configuration
ShortcutConfig config;

// Set custom shortcuts
config.set_override("view.reset", "Ctrl+R");
config.set_override("panel.toggle_inspector", "I");
config.set_override("anim.toggle_play", "Space");

// Apply to ShortcutManager
config.apply_to(&shortcut_manager);

// Save to file
config.save_to_file("custom_shortcuts.json");
```

#### Load/Save Workflow
```cpp
// At startup
ShortcutConfig config;
if (config.load_from_file("shortcuts.json")) {
    config.apply_to(&shortcut_manager);
}

// Set up change callback for auto-save
config.set_change_callback([](const std::string& cmd, const std::string& shortcut) {
    // Auto-save when shortcuts change
    config.save_to_file("shortcuts.json");
});
```

#### Bulk Operations
```cpp
// Set multiple shortcuts at once
std::unordered_map<std::string, std::string> overrides = {
    {"file.save", "Ctrl+S"},
    {"file.open", "Ctrl+O"},
    {"edit.undo", "Ctrl+Z"},
    {"edit.redo", "Ctrl+Y"}
};
config.set_overrides(overrides);
```

### JSON Format
```json
{
  "version": 1,
  "shortcuts": {
    "view.reset": "Ctrl+R",
    "panel.toggle_inspector": "I",
    "anim.toggle_play": "Space",
    "file.save": "Ctrl+S",
    "file.open": "Ctrl+O"
  }
}
```

### API Reference

#### Core Methods
- `set_override(command_id, shortcut)` - Set a custom shortcut
- `get_override(command_id)` - Get custom shortcut for command
- `remove_override(command_id)` - Remove a custom shortcut
- `clear_overrides()` - Remove all custom shortcuts
- `save_to_file(filename)` - Save configuration to JSON file
- `load_from_file(filename)` - Load configuration from JSON file
- `apply_to(shortcut_manager)` - Apply overrides to ShortcutManager
- `to_json()` - Serialize to JSON string
- `from_json(json_string)` - Deserialize from JSON string

#### Advanced Features
- **Change Callbacks**: Get notified when shortcuts change
- **Validation**: Ensure shortcut strings are properly formatted
- **Conflict Detection**: Warn about shortcut conflicts
- **Thread Safety**: All operations are thread-safe

## Plugin API System

### Purpose
Provide a stable, language-agnostic interface for external developers to create plugins that can extend Plotix functionality.

### Key Components

#### C ABI Interface
- **Location**: `src/ui/plugin_api.hpp/.cpp`
- **Purpose**: Stable C-compatible interface for plugins
- **Features**: Versioned API, memory safety, error handling

#### PluginManager
- **Purpose**: Dynamic loading/unloading of plugin libraries
- **Features**: Lifecycle management, dependency resolution, error isolation

#### PluginContext
- **Purpose**: Safe handle to application services for plugins
- **Features**: Access to core services, logging, data storage

### Plugin Development

#### Basic Plugin Structure
```c
// plugin_example.c
#include "plugin_api.h"

// Required exports
PLOTIX_PLUGIN_EXPORT const char* plotix_plugin_name() {
    return "Example Plugin";
}

PLOTIX_PLUGIN_EXPORT const char* plotix_plugin_version() {
    return "1.0.0";
}

PLOTIX_PLUGIN_EXPORT const char* plotix_plugin_author() {
    return "Developer Name";
}

PLOTIX_PLUGIN_EXPORT int plotix_plugin_init(PluginContext* ctx) {
    // Register commands
    ctx->register_command(ctx, "example.hello", "Say Hello", 
                          hello_callback, "Ctrl+H", "Examples");
    
    return PLOTIX_PLUGIN_SUCCESS;
}

PLOTIX_PLUGIN_EXPORT void plotix_plugin_shutdown(PluginContext* ctx) {
    // Cleanup resources
}
```

#### Command Implementation
```c
void hello_callback(PluginContext* ctx, void* user_data) {
    // Access application services
    CommandRegistry* registry = ctx->get_command_registry(ctx);
    
    // Log message
    ctx->log_info(ctx, "Hello from plugin!");
    
    // Show message (if UI available)
    ctx->show_message(ctx, "Hello", "Plugin says hello!");
}
```

#### Advanced Plugin with Undo Support
```c
void add_data_callback(PluginContext* ctx, void* user_data) {
    Figure* fig = ctx->get_active_figure(ctx);
    if (!fig) return;
    
    // Store previous state for undo
    char* old_data = ctx->serialize_figure(ctx, fig);
    
    // Modify figure
    ctx->add_random_series(ctx, fig);
    
    // Register undo action
    ctx->register_undo_action(ctx, "Add Random Data",
        old_data, restore_figure_callback);
}
```

### Host Application Integration

#### Loading Plugins
```cpp
// Initialize plugin manager
PluginManager plugin_manager;
plugin_manager.set_command_registry(&command_registry);
plugin_manager.set_shortcut_manager(&shortcut_manager);
plugin_manager.set_undo_manager(&undo_manager);

// Load plugins
std::vector<std::string> plugin_paths = {
    "plugins/data_import.so",
    "plugins/export_tools.so",
    "plugins/analysis.so"
};

for (const auto& path : plugin_paths) {
    if (plugin_manager.load_plugin(path)) {
        std::cout << "Loaded plugin: " << path << std::endl;
    }
}
```

#### Plugin Discovery
```cpp
// Discover plugins in directory
auto discovered = plugin_manager.discover_plugins("./plugins");

for (const auto& info : discovered) {
    std::cout << "Found: " << info.name 
              << " v" << info.version 
              << " by " << info.author << std::endl;
    
    if (info.auto_load) {
        plugin_manager.load_plugin(info.path);
    }
}
```

### Plugin API Reference

#### Core Functions
- `register_command()` - Register a new command
- `unregister_command()` - Remove a command
- `get_command_registry()` - Access command registry
- `get_shortcut_manager()` - Access shortcut manager
- `get_undo_manager()` - Access undo manager
- `get_active_figure()` - Get current figure
- `log_info()/log_error()` - Logging functions
- `show_message()` - Display message to user

#### Advanced Functions
- `register_undo_action()` - Register undoable action
- `serialize_figure()` - Serialize figure state
- `get_plugin_config()` - Get plugin configuration
- `set_plugin_data()` - Store plugin-specific data
- `get_plugin_data()` - Retrieve plugin-specific data

#### Error Handling
- All functions return error codes
- Use `get_last_error()` for detailed error information
- Plugins should handle errors gracefully

## Examples

### Available Examples

1. **shortcut_usage_demo.cpp** - Comprehensive guide to shortcut configuration
2. **plugin_api_demo.cpp** - Complete plugin development guide
3. **timeline_curve_demo.cpp** - Timeline and curve editor integration
4. **advanced_animation_demo.cpp** - Full animation system demonstration

### Building Examples

```bash
cd build
cmake --build . --target shortcut_usage_demo
cmake --build . --target plugin_api_demo
cmake --build . --target timeline_curve_demo
cmake --build . --target advanced_animation_demo
```

### Running Examples

```bash
./build/examples/shortcut_usage_demo
./build/examples/plugin_api_demo
./build/examples/timeline_curve_demo
./build/examples/advanced_animation_demo
```

## Best Practices

### Shortcut Configuration
- Always validate shortcut strings before setting them
- Use descriptive command IDs that match UI labels
- Provide visual feedback when shortcuts change
- Auto-save changes to prevent data loss
- Offer reset-to-defaults option

### Plugin Development
- Always check return codes from API calls
- Use the provided logging system
- Register undo actions for destructive operations
- Clean up all resources in shutdown
- Use semantic versioning for compatibility
- Handle errors gracefully

### Integration
- Load shortcuts at application startup
- Set up change callbacks for auto-save
- Initialize plugin manager before loading plugins
- Handle plugin failures gracefully
- Provide plugin management UI for users

## Testing

### Shortcut Configuration Tests
- **Location**: `tests/unit/test_shortcut_config.cpp`
- **Coverage**: 26 tests covering all functionality
- **Areas**: Override management, serialization, file I/O, callbacks

### Plugin API Tests
- **Location**: `tests/unit/test_plugin_api.cpp`
- **Coverage**: 31 tests covering C ABI and PluginManager
- **Areas**: C ABI functions, PluginManager, serialization, error handling

### Running Tests

```bash
cd build
ctest --output-on-failure -R "shortcut_config|plugin_api"
```

## Architecture

### Shortcut Configuration Architecture
```
User Input → ShortcutConfig → JSON File → ShortcutManager → CommandRegistry
     ↑              ↓              ↑              ↓              ↓
   UI          Validation      Persistence   Application   Commands
```

### Plugin API Architecture
```
Plugin Library → C ABI → PluginManager → Application Services
      ↑             ↑          ↑              ↑
   External      Stable    Lifecycle    Core Systems
   Code         Interface   Management
```

## Future Enhancements

### Shortcut Configuration
- [ ] Import/export shortcut profiles
- [ ] Multiple shortcut sets per user
- [ ] Conflict resolution UI
- [ ] Shortcut recording macros
- [ ] Cloud synchronization

### Plugin API
- [ ] Plugin marketplace
- [ ] Automatic updates
- [ ] Plugin sandboxing
- [ ] Inter-plugin communication
- [ ] Plugin signing/security

## Support

For questions about these advanced features:

1. Check the comprehensive tests for usage patterns
2. Review the implementation in `src/ui/`
3. Run the example programs to see concepts in action
4. Consult the API documentation in the header files

These systems are designed to be extensible and maintainable, providing a solid foundation for both user customization and third-party extensions.
