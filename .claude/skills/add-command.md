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
