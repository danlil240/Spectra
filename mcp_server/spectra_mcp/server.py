"""MCP server that exposes Spectra UI actions as tools for external agents."""

import json
import os
import sys
from typing import Any

from mcp.server.fastmcp import FastMCP

from .client import SpectraClient, default_socket_path

# ─── MCP Server ───────────────────────────────────────────────────────────────

mcp = FastMCP(
    name="spectra-automation",
    instructions=(
        "Control a running Spectra application. Use these tools to interact "
        "with Spectra's UI: execute commands, simulate mouse/keyboard input, "
        "create figures, manage series data, and capture screenshots. "
        "Start with 'ping' to verify the connection, then 'list_commands' to "
        "see available UI commands, and 'get_state' for the current app state."
    ),
)

# Global client (lazy-connected)
_client: SpectraClient | None = None


def _get_client() -> SpectraClient:
    global _client
    if _client is None:
        _client = SpectraClient()
    if _client._sock is None:
        _client.connect()
    return _client


def _call(method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    """Send a command to Spectra and return the result."""
    client = _get_client()
    resp = client.send(method, params)
    if not resp.get("ok", False):
        error = resp.get("error", "Unknown error")
        raise RuntimeError(f"Spectra error: {error}")
    return resp.get("result", {})


# ─── Connection tools ─────────────────────────────────────────────────────────


@mcp.tool()
def ping() -> str:
    """Ping the Spectra application to verify the connection is alive."""
    result = _call("ping")
    return json.dumps(result)


# ─── State inspection tools ───────────────────────────────────────────────────


@mcp.tool()
def get_state() -> str:
    """Get the current state of the Spectra application.

    Returns figure count, active figure, dimensions, series counts,
    undo/redo state, and 3D mode status.
    """
    result = _call("get_state")
    return json.dumps(result, indent=2)


@mcp.tool()
def list_commands() -> str:
    """List all registered UI commands in the Spectra application.

    Returns command IDs, labels, categories, shortcuts, and enabled state.
    Use the command IDs with execute_command to trigger specific actions.
    """
    result = _call("list_commands")
    return json.dumps(result, indent=2)


# ─── Command execution tools ─────────────────────────────────────────────────


@mcp.tool()
def execute_command(command_id: str) -> str:
    """Execute a registered Spectra UI command by its ID.

    Common commands include:
    - view.reset, view.autofit, view.toggle_grid, view.toggle_3d
    - view.zoom_in, view.zoom_out, view.pan_left, view.pan_right
    - view.split_right, view.split_down, view.close_split
    - figure.new, figure.close, figure.next_tab, figure.prev_tab
    - edit.undo, edit.redo
    - theme.dark, theme.light, theme.night, theme.toggle
    - panel.toggle_timeline, panel.toggle_inspector, panel.toggle_nav_rail
    - tool.pan, tool.box_zoom, tool.select, tool.measure, tool.annotate
    - anim.toggle_play, anim.step_forward, anim.step_back, anim.stop
    - file.export_png, file.export_svg
    - series.cycle_selection, series.delete, series.copy, series.paste
    - data.copy_to_clipboard

    Use list_commands to see all available commands.

    Args:
        command_id: The command ID to execute (e.g. "view.reset").
    """
    result = _call("execute_command", {"command_id": command_id})
    return json.dumps(result)


# ─── Mouse input tools ───────────────────────────────────────────────────────


@mcp.tool()
def mouse_move(x: float, y: float) -> str:
    """Move the mouse cursor to the specified position.

    Args:
        x: X coordinate in pixels from the left edge of the window.
        y: Y coordinate in pixels from the top edge of the window.
    """
    result = _call("mouse_move", {"x": x, "y": y})
    return json.dumps(result)


@mcp.tool()
def mouse_click(x: float, y: float, button: int = 0, modifiers: int = 0) -> str:
    """Click at the specified position.

    Args:
        x: X coordinate in pixels.
        y: Y coordinate in pixels.
        button: Mouse button (0=left, 1=right, 2=middle).
        modifiers: Modifier keys bitmask (1=Shift, 2=Ctrl, 4=Alt).
    """
    result = _call("mouse_click", {"x": x, "y": y, "button": button, "modifiers": modifiers})
    return json.dumps(result)


@mcp.tool()
def mouse_drag(
    x1: float, y1: float, x2: float, y2: float,
    button: int = 0, modifiers: int = 0, steps: int = 10
) -> str:
    """Drag the mouse from one position to another.

    Args:
        x1: Start X coordinate.
        y1: Start Y coordinate.
        x2: End X coordinate.
        y2: End Y coordinate.
        button: Mouse button (0=left, 1=right, 2=middle).
        modifiers: Modifier keys bitmask (1=Shift, 2=Ctrl, 4=Alt).
        steps: Number of intermediate move events (default 10).
    """
    result = _call("mouse_drag", {
        "x1": x1, "y1": y1, "x2": x2, "y2": y2,
        "button": button, "modifiers": modifiers, "steps": steps,
    })
    return json.dumps(result)


@mcp.tool()
def scroll(x: float, y: float, dx: float = 0.0, dy: float = 1.0) -> str:
    """Scroll at the specified position.

    Args:
        x: X coordinate in pixels.
        y: Y coordinate in pixels.
        dx: Horizontal scroll amount (positive = right).
        dy: Vertical scroll amount (positive = up/zoom in).
    """
    result = _call("scroll", {"x": x, "y": y, "dx": dx, "dy": dy})
    return json.dumps(result)


# ─── Keyboard input tools ────────────────────────────────────────────────────


@mcp.tool()
def key_press(key: int, modifiers: int = 0) -> str:
    """Press and release a keyboard key.

    Args:
        key: Key code (ASCII value for printable keys, GLFW key codes otherwise).
        modifiers: Modifier keys bitmask (1=Shift, 2=Ctrl, 4=Alt).
    """
    result = _call("key_press", {"key": key, "modifiers": modifiers})
    return json.dumps(result)


# ─── Figure management tools ─────────────────────────────────────────────────


@mcp.tool()
def create_figure(width: int = 1280, height: int = 720) -> str:
    """Create a new figure in Spectra.

    Args:
        width: Figure width in pixels (default 1280).
        height: Figure height in pixels (default 720).
    """
    result = _call("create_figure", {"width": width, "height": height})
    return json.dumps(result)


@mcp.tool()
def switch_figure(figure_id: int) -> str:
    """Switch to a specific figure by its ID.

    Args:
        figure_id: The figure ID to switch to.
    """
    result = _call("switch_figure", {"figure_id": figure_id})
    return json.dumps(result)


@mcp.tool()
def add_series(
    figure_id: int,
    series_type: str = "line",
    n_points: int = 100,
    label: str = "",
) -> str:
    """Add a data series to a figure.

    Args:
        figure_id: The figure ID to add the series to.
        series_type: Type of series ("line" or "scatter").
        n_points: Number of data points to generate (default 100).
        label: Label for the series.
    """
    result = _call("add_series", {
        "figure_id": figure_id,
        "type": series_type,
        "n_points": n_points,
        "label": label,
    })
    return json.dumps(result)


# ─── Frame control tools ─────────────────────────────────────────────────────


@mcp.tool()
def pump_frames(count: int = 1) -> str:
    """Advance the application by rendering the specified number of frames.

    This is essential after making changes to see their effect.
    Most operations need 1-5 frames to take full effect.

    Args:
        count: Number of frames to render (1-600, default 1).
    """
    result = _call("pump_frames", {"count": count})
    return json.dumps(result)


# ─── Screenshot tools ─────────────────────────────────────────────────────────


@mcp.tool()
def capture_screenshot(path: str = "/tmp/spectra_auto_screenshot.png") -> str:
    """Capture a screenshot of the current active figure (plot area only).

    This captures only the figure's offscreen render — no menubar, sidebar,
    tabs, or status bar.  Use capture_window for a full-window screenshot.

    Args:
        path: File path to save the PNG screenshot.
    """
    result = _call("capture_screenshot", {"path": path})
    return json.dumps(result)


@mcp.tool()
def capture_window(path: str = "/tmp/spectra_auto_window.png") -> str:
    """Capture a full-window screenshot including all UI chrome.

    This reads back the Vulkan swapchain image, which includes the menubar,
    sidebar, tabs, status bar, and the plot area — everything visible in the
    application window.  Returns the saved path and image dimensions.

    Args:
        path: File path to save the PNG screenshot.
    """
    result = _call("capture_window", {"path": path})
    return json.dumps(result)


# ─── Entry point ──────────────────────────────────────────────────────────────


def main() -> None:
    """Run the MCP server via stdio transport."""
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
