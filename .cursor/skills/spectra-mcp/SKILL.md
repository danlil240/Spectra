---
name: spectra-mcp
description: >-
  Drives a live Spectra instance via the embedded MCP automation server
  (default http://127.0.0.1:8765/mcp). Use for smoke tests, screenshots, UI
  commands, figure/series setup, and visual debugging after builds — whenever
  validating rendering without writing a harness.
---

# Spectra MCP Automation

The MCP server is **off by default**. Start Spectra with `SPECTRA_AUTOMATION=1`; every request needs `Authorization: Bearer $SPECTRA_MCP_TOKEN` (set the env var before launch, or read the token from `$XDG_RUNTIME_DIR/spectra-mcp-<pid>.json`). Screenshot paths are relative to `$SPECTRA_CAPTURE_DIR`. See `SECURITY.md`. Env: `SPECTRA_MCP_PORT` (default `8765`), `SPECTRA_MCP_BIND` (default `127.0.0.1`).

## Startup pattern

```bash
pkill -f spectra || true
pkill -f spectra-backend || true
sleep 0.5
export SPECTRA_AUTOMATION=1 SPECTRA_MCP_TOKEN="${SPECTRA_MCP_TOKEN:-$(openssl rand -hex 32)}"  # automation is opt-in (SECURITY.md)
./build/app/spectra &
sleep 1
curl -s -H "Authorization: Bearer $SPECTRA_MCP_TOKEN" http://127.0.0.1:8765/
```

Use `./build/examples/basic_line &` when testing a specific example binary.

## JSON-RPC shape

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Authorization: Bearer $SPECTRA_MCP_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"TOOL","arguments":{}}}'
```

## Tools

| Tool | Purpose |
|------|---------|
| `ping` | Server alive |
| `get_state` | Figures, active window, undo stack |
| `list_commands` | All registered UI command IDs |
| `execute_command` | Run command by ID |
| `create_figure` | New figure (`width`, `height`) |
| `switch_figure` | Focus figure by ID |
| `add_series` | Add series (`figure_id`, `series_type`, `n_points`, `label`) |
| `get_figure_info` | Axes/series metadata |
| `wait_frames` | Block until N frames rendered |
| `pump_frames` | Advance frame loop N times |
| `capture_screenshot` | Canvas PNG to path |
| `capture_window` | Full window PNG to path |
| `get_screenshot_base64` | Inline PNG for review |
| `mouse_move` / `mouse_click` / `mouse_drag` / `double_click` / `scroll` | Input |
| `key_press` / `text_input` | Keyboard |
| `get_window_size` / `resize_window` | Window geometry |
| `fuzz_reset` / `fuzz_step` / `list_fuzz_actions` | Weighted random QA fuzz (qa_agent parity) |
| `list_methods` | Full automation method catalog |

## Typical visual check

1. `create_figure` → `add_series` (match change type: `line`, `scatter`, `surface`, …)
2. `wait_frames` with `count` ≥ 10
3. `get_screenshot_base64` or `capture_window`

Full graphical workflow: [graphical-change-workflow](../graphical-change-workflow/SKILL.md).

## Pitfalls

- **Connection refused** — automation not enabled (`SPECTRA_AUTOMATION=1`), or a stale process on port 8765; kill and wait 0.5s after relaunch.
- **401 Unauthorized** — missing/wrong `Authorization: Bearer` token.
- **403 / 415** — request carried an `Origin` header, a non-loopback `Host`, or a non-JSON `Content-Type`.
- **Black screenshot** — call `wait_frames` before capture.
- **Wrong binary** — rebuild the target you launch (`app/spectra` vs `examples/`).
