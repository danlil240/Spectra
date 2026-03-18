# Add a UI Command

Register a new command in the command palette and optionally bind a keyboard shortcut.

## Steps

1. Define the command implementation in `src/ui/register_commands.cpp`
2. Register it with `CommandRegistry` using a unique ID and human-readable label
3. Optionally add a default keybinding in `src/ui/shortcut_manager.cpp`
4. Add a unit test in `tests/unit/test_command_registry.cpp`
5. Build and verify the command appears in the command palette (Ctrl+K)

## Conventions

- Command IDs use dot notation: `spectra.view.zoom_fit`, `spectra.file.export_png`
- Labels should be concise and action-oriented: "Zoom to Fit", "Export as PNG"
- Commands that modify state should be undoable via `UndoManager`
- Group related commands by category for command palette organization

## Verify via MCP Server

After registering a command, verify it appears and executes via the live MCP server:

```bash
pkill -f spectra || true; sleep 0.5
./build/app/spectra &
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
