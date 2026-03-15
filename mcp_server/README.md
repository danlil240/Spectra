# Spectra MCP Server

An [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) server that exposes Spectra UI actions as tools, allowing AI agents (Claude, Copilot, Cascade, etc.) to programmatically control a running Spectra application.

## Architecture

```
┌─────────────┐   stdio/SSE   ┌──────────────┐   Unix socket   ┌─────────────────┐
│  AI Agent   │ ◄───────────► │  MCP Server  │ ◄─────────────► │  Spectra App    │
│ (Claude,    │               │  (Python)    │   JSON-over-UDS │  (C++ automation│
│  Cascade)   │               │              │                 │   server thread)│
└─────────────┘               └──────────────┘                 └─────────────────┘
```

1. **Spectra App** — Starts an `AutomationServer` thread on init that listens on a Unix domain socket (`/tmp/spectra-auto-<pid>.sock`). Accepts newline-delimited JSON commands, executes them on the main thread, and returns JSON responses.

2. **MCP Server** (`spectra-mcp`) — A Python process using the `mcp` SDK (FastMCP). Connects to Spectra's automation socket and exposes each action as an MCP tool. Communicates with the AI agent via stdio transport.

3. **AI Agent** — Any MCP-compatible client can call the exposed tools to drive Spectra: create figures, execute commands, simulate input, capture screenshots, etc.

## Quick Start

### 1. Start Spectra

```bash
# Build and run any Spectra example (the automation server starts automatically)
cd /path/to/Spectra
cmake --build build
./build/animated_scatter

# The socket path is printed in the log:
#   [automation] Listening on /tmp/spectra-auto-12345.sock
```

You can override the socket path:
```bash
SPECTRA_AUTO_SOCKET=/tmp/my-spectra.sock ./build/animated_scatter
```

### 2. Install the MCP server

```bash
cd mcp_server
pip install -e .
```

### 3. Run the MCP server

```bash
# Auto-discovers the Spectra socket
spectra-mcp

# Or specify the socket explicitly
SPECTRA_AUTO_SOCKET=/tmp/spectra-auto-12345.sock spectra-mcp
```

### 4. Configure your AI agent

#### Windsurf / Cascade

The MCP server is pre-configured in `.windsurf/mcp.json`. Just ensure Spectra is running and the MCP server is installed.

#### Claude Desktop

Add to `~/.config/claude/claude_desktop_config.json`:
```json
{
  "mcpServers": {
    "spectra": {
      "command": "spectra-mcp",
      "args": [],
      "env": {
        "SPECTRA_AUTO_SOCKET": "/tmp/spectra-auto-<pid>.sock"
      }
    }
  }
}
```

## Available Tools

### Connection
| Tool | Description |
|------|-------------|
| `ping` | Verify the connection to Spectra is alive |

### State Inspection
| Tool | Description |
|------|-------------|
| `get_state` | Get current app state (figures, series, undo/redo, 3D mode) |
| `list_commands` | List all registered UI commands with IDs, labels, categories |

### Command Execution
| Tool | Description |
|------|-------------|
| `execute_command(command_id)` | Execute any registered command by ID |

Common command IDs:
- **View**: `view.reset`, `view.autofit`, `view.toggle_grid`, `view.zoom_in`, `view.zoom_out`
- **Figure**: `figure.new`, `figure.close`, `figure.next_tab`, `figure.prev_tab`
- **Edit**: `edit.undo`, `edit.redo`
- **Theme**: `theme.dark`, `theme.light`, `theme.night`, `theme.toggle`
- **Panel**: `panel.toggle_timeline`, `panel.toggle_inspector`, `panel.toggle_nav_rail`
- **Tools**: `tool.pan`, `tool.box_zoom`, `tool.select`, `tool.measure`
- **Animation**: `anim.toggle_play`, `anim.step_forward`, `anim.step_back`
- **Split**: `view.split_right`, `view.split_down`, `view.close_split`

### Mouse Input
| Tool | Description |
|------|-------------|
| `mouse_move(x, y)` | Move cursor to position |
| `mouse_click(x, y, button, modifiers)` | Click at position |
| `mouse_drag(x1, y1, x2, y2, ...)` | Drag from one position to another |
| `scroll(x, y, dx, dy)` | Scroll at position |

### Keyboard Input
| Tool | Description |
|------|-------------|
| `key_press(key, modifiers)` | Press and release a key |

### Figure Management
| Tool | Description |
|------|-------------|
| `create_figure(width, height)` | Create a new figure |
| `switch_figure(figure_id)` | Switch to a figure by ID |
| `add_series(figure_id, type, n_points, label)` | Add a data series |

### Frame Control
| Tool | Description |
|------|-------------|
| `pump_frames(count)` | Advance rendering by N frames |

### Screenshot
| Tool | Description |
|------|-------------|
| `capture_screenshot(path)` | Save a PNG of the active figure (plot area only) |
| `capture_window(path)` | Save a PNG of the full window (plot + ImGui chrome: menubar, sidebar, tabs, status bar) |

## Protocol

The automation socket uses a simple newline-delimited JSON protocol:

**Request** (one JSON object per line):
```json
{"method": "execute_command", "params": {"command_id": "view.reset"}}
```

**Response** (one JSON object per line):
```json
{"id": 1, "ok": true, "result": {"executed": "view.reset"}}
```

**Error response**:
```json
{"id": 2, "ok": false, "error": "Command not found or disabled: foo.bar"}
```

## Direct Socket Usage (without MCP)

You can also talk to the automation socket directly from any language:

```python
import socket, json

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/tmp/spectra-auto-12345.sock")

# Send a command
request = json.dumps({"method": "ping", "params": {}}) + "\n"
sock.sendall(request.encode())

# Read response
data = sock.recv(65536)
response = json.loads(data.decode().strip())
print(response)  # {"id": 1, "ok": true, "result": {"pong": true}}
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `SPECTRA_AUTO_SOCKET` | Override the automation socket path (both C++ and Python sides) |
