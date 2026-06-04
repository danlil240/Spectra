# Add a UI Command

Register a new command in the command palette and optionally bind a keyboard shortcut.

## Steps

1. Add a `CommandDescriptor` entry in the appropriate category file under `src/ui/app/commands/`:
   - View: `commands_view.cpp`
   - Edit: `commands_edit.cpp`
   - File: `commands_file.cpp`
   - Figure: `commands_figure.cpp`
   - Series: `commands_series.cpp`
   - Animation: `commands_animation.cpp`
   - Panel: `commands_panel.cpp`
   - Theme: `commands_theme.cpp`
   - Tools: `commands_tools.cpp`
   - Data: `commands_data.cpp`
   - App: `commands_app.cpp`
2. If adding a new category, declare `make_<category>_commands()` in `command_groups.hpp` and call it from `register_standard_commands()` in `register_commands.cpp`.
3. Shortcuts are declared on the descriptor (`shortcut` field) and bound automatically by `register_descriptors()` — no separate `ShortcutManager` call needed.
4. Add a unit test in `tests/unit/test_command_registry.cpp` or `tests/unit/test_phase2_integration.cpp` if the command has non-trivial behavior.
5. Build and verify the command appears in the command palette (Ctrl+K).

## Descriptor shape

```cpp
cmds.push_back({"view.my_command",       // id (dot notation)
                "My Command",            // label
                "Ctrl+M",                // shortcut ("" = none)
                "View",                  // category
                static_cast<uint16_t>(ui::Icon::Home),  // icon (0 = none)
                [&]() { /* action */ }});               // callback
```

`CommandContext` (built from `CommandBindings` in `register_standard_commands()`) provides shared dependencies: `ui_ctx`, `registry`, `active_figure`, `active_figure_id`, `session`, and optionally `window_mgr`.

## Conventions

- Command IDs use dot notation: `view.reset`, `file.export_png`
- Labels should be concise and action-oriented: "Zoom to Fit", "Export as PNG"
- Commands that modify state should be undoable via `UndoManager`
- Group related commands by category for command palette organization

## Verify via MCP Server

After registering a command, verify it appears and executes via the live MCP server:

```bash
pkill -f spectra || true; sleep 0.5
./build/spectra &
sleep 1

# List all commands (verify yours appears)
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_commands","arguments":{}}}'

# Execute your new command
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"execute_command","arguments":{"command_id":"your.command.id"}}}'
```

## Verify via automation socket

Automation methods are registered in `src/ui/automation/handlers/`. Use `list_methods` to inspect handler metadata:

```bash
echo '{"method":"list_methods","params":{}}' | nc -U /tmp/spectra-auto-<pid>.sock
```
