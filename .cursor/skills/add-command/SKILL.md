---
name: add-command
description: >-
  Registers a new Spectra UI command in the command palette and optional
  keyboard shortcut with undo support. Use when adding menu actions, palette
  entries, shortcuts, or execute_command MCP targets in src/ui/commands/.
---

# Add UI Command

1. Implement in `src/ui/register_commands.cpp`
2. Register with `CommandRegistry` (unique dot-notation ID, label)
3. Optional default binding in `src/ui/shortcut_manager.cpp`
4. Test: `tests/unit/test_command_registry.cpp`
5. Build; verify in palette (Ctrl+K) or MCP `list_commands` / `execute_command`

## Conventions

- IDs: `spectra.view.zoom_fit`, `spectra.file.export_png`
- Labels: short, action-oriented
- State-changing commands → `UndoManager`
- Group by category in palette

Verify: [spectra-mcp](../spectra-mcp/SKILL.md) `execute_command` with your `command_id`.
