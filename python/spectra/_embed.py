"""EmbedSurface — in-process GPU-accelerated plotting for embedding in Qt/GTK/etc.

Unlike the IPC-based Session/Figure API, EmbedSurface runs the Vulkan renderer
directly inside the host process and renders plots to a CPU pixel buffer that
can be painted into any widget.

Requires ``libspectra_embed.so`` (build with ``-DSPECTRA_BUILD_EMBED_SHARED=ON``).

Usage::

    from spectra._embed import EmbedSurface
    surface = EmbedSurface(800, 600)
    fig = surface.figure()
    ax = fig.subplot(1, 1, 1)
    ax.line([0,1,2,3], [0,1,4,9])
    pixels = surface.render()  # returns bytes (RGBA, width*height*4)
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
from pathlib import Path
from typing import Optional, Tuple

# ─── Library loading ─────────────────────────────────────────────────────────

_lib: Optional[ctypes.CDLL] = None


def _find_library() -> str:
    """Locate libspectra_embed.so, searching common paths."""
    # 1. Explicit env var
    env_path = os.environ.get("SPECTRA_EMBED_LIB")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. Next to this file (installed package)
    pkg_dir = Path(__file__).parent
    for candidate in [
        pkg_dir / "libspectra_embed.so",
        pkg_dir / "spectra_embed.dll",
        pkg_dir / "libspectra_embed.dylib",
    ]:
        if candidate.is_file():
            return str(candidate)

    # 3. Build directory (development)
    # Walk up from python/spectra/ to project root, then check build/
    project_root = pkg_dir.parent.parent
    for build_dir in ["build", "cmake-build-debug", "cmake-build-release"]:
        candidate = project_root / build_dir / "libspectra_embed.so"
        if candidate.is_file():
            return str(candidate)

    # 4. System search
    found = ctypes.util.find_library("spectra_embed")
    if found:
        return found

    raise FileNotFoundError(
        "Cannot find libspectra_embed.so. Build with:\n"
        "  cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..\n"
        "  make spectra_embed\n"
        "Or set SPECTRA_EMBED_LIB=/path/to/libspectra_embed.so"
    )


def _load_lib() -> ctypes.CDLL:
    global _lib
    if _lib is not None:
        return _lib

    path = _find_library()
    _lib = ctypes.CDLL(path)

    # ── Declare function signatures ──────────────────────────────────────

    # Lifecycle
    _lib.spectra_embed_create.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    _lib.spectra_embed_create.restype = ctypes.c_void_p

    _lib.spectra_embed_destroy.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_destroy.restype = None

    _lib.spectra_embed_is_valid.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_is_valid.restype = ctypes.c_int

    # Figure
    _lib.spectra_embed_figure.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_figure.restype = ctypes.c_void_p

    _lib.spectra_embed_active_figure.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_active_figure.restype = ctypes.c_void_p

    # Axes
    _lib.spectra_figure_subplot.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int
    ]
    _lib.spectra_figure_subplot.restype = ctypes.c_void_p

    _lib.spectra_figure_subplot3d.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int
    ]
    _lib.spectra_figure_subplot3d.restype = ctypes.c_void_p

    # Series
    _lib.spectra_axes_line.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_char_p,
    ]
    _lib.spectra_axes_line.restype = ctypes.c_void_p

    _lib.spectra_axes_scatter.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_char_p,
    ]
    _lib.spectra_axes_scatter.restype = ctypes.c_void_p

    _lib.spectra_series_set_x.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_uint32
    ]
    _lib.spectra_series_set_x.restype = None

    _lib.spectra_series_set_y.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_uint32
    ]
    _lib.spectra_series_set_y.restype = None

    _lib.spectra_series_set_data.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
    ]
    _lib.spectra_series_set_data.restype = None

    # Rendering
    _lib.spectra_embed_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8)]
    _lib.spectra_embed_render.restype = ctypes.c_int

    _lib.spectra_embed_resize.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
    _lib.spectra_embed_resize.restype = ctypes.c_int

    _lib.spectra_embed_width.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_width.restype = ctypes.c_uint32

    _lib.spectra_embed_height.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_height.restype = ctypes.c_uint32

    # Input
    _lib.spectra_embed_mouse_move.argtypes = [
        ctypes.c_void_p, ctypes.c_float, ctypes.c_float
    ]
    _lib.spectra_embed_mouse_move.restype = None

    _lib.spectra_embed_mouse_button.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.c_float, ctypes.c_float,
    ]
    _lib.spectra_embed_mouse_button.restype = None

    _lib.spectra_embed_scroll.argtypes = [
        ctypes.c_void_p,
        ctypes.c_float, ctypes.c_float,
        ctypes.c_float, ctypes.c_float,
    ]
    _lib.spectra_embed_scroll.restype = None

    _lib.spectra_embed_key.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int
    ]
    _lib.spectra_embed_key.restype = None

    _lib.spectra_embed_update.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_embed_update.restype = None

    # Display configuration
    _lib.spectra_embed_set_dpi_scale.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_embed_set_dpi_scale.restype = None

    _lib.spectra_embed_get_dpi_scale.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_get_dpi_scale.restype = ctypes.c_float

    # Theme & UI chrome
    _lib.spectra_embed_set_theme.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_embed_set_theme.restype = None

    _lib.spectra_embed_set_show_command_bar.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_embed_set_show_command_bar.restype = None

    _lib.spectra_embed_set_show_status_bar.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_embed_set_show_status_bar.restype = None

    _lib.spectra_embed_set_show_nav_rail.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_embed_set_show_nav_rail.restype = None

    _lib.spectra_embed_set_show_inspector.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_embed_set_show_inspector.restype = None

    # Axes configuration
    _lib.spectra_axes_set_xlabel.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_axes_set_xlabel.restype = None

    _lib.spectra_axes_set_ylabel.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_axes_set_ylabel.restype = None

    _lib.spectra_axes_set_title.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_axes_set_title.restype = None

    _lib.spectra_axes_set_xlim.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_float]
    _lib.spectra_axes_set_xlim.restype = None

    _lib.spectra_axes_set_ylim.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_float]
    _lib.spectra_axes_set_ylim.restype = None

    _lib.spectra_axes_set_grid.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_axes_set_grid.restype = None

    # Figure configuration
    _lib.spectra_figure_set_title.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_figure_set_title.restype = None

    return _lib


# ─── Helper: convert Python list/numpy array to ctypes float pointer ─────────

def _to_float_ptr(data) -> Tuple[ctypes.POINTER(ctypes.c_float), int]:
    """Convert a sequence of floats to (ctypes float pointer, count)."""
    try:
        import numpy as np
        if isinstance(data, np.ndarray):
            arr = data.astype(np.float32, copy=False)
            ptr = arr.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            return ptr, len(arr)
    except ImportError:
        pass

    # Plain Python list/tuple
    n = len(data)
    arr = (ctypes.c_float * n)(*data)
    return ctypes.cast(arr, ctypes.POINTER(ctypes.c_float)), n


# ─── Constants ───────────────────────────────────────────────────────────────

MOUSE_LEFT = 0
MOUSE_RIGHT = 1
MOUSE_MIDDLE = 2

ACTION_RELEASE = 0
ACTION_PRESS = 1

MOD_SHIFT = 0x0001
MOD_CONTROL = 0x0002
MOD_ALT = 0x0004

KEY_ESCAPE = 256
KEY_R = 82
KEY_G = 71
KEY_A = 65
KEY_S = 83


# ─── High-level Python wrappers ─────────────────────────────────────────────

class EmbedSeries:
    """Proxy for a series in an embedded plot."""

    __slots__ = ("_handle", "_lib")

    def __init__(self, handle: ctypes.c_void_p) -> None:
        self._handle = handle
        self._lib = _load_lib()

    def set_x(self, data) -> None:
        ptr, n = _to_float_ptr(data)
        self._lib.spectra_series_set_x(self._handle, ptr, n)

    def set_y(self, data) -> None:
        ptr, n = _to_float_ptr(data)
        self._lib.spectra_series_set_y(self._handle, ptr, n)

    def set_data(self, x, y) -> None:
        """Update both X and Y data atomically (no intermediate mismatch)."""
        xp, xn = _to_float_ptr(x)
        yp, yn = _to_float_ptr(y)
        assert xn == yn, f"x and y must have same length ({xn} vs {yn})"
        self._lib.spectra_series_set_data(self._handle, xp, yp, xn)


class EmbedAxes:
    """Proxy for axes in an embedded plot."""

    __slots__ = ("_handle", "_lib")

    def __init__(self, handle: ctypes.c_void_p) -> None:
        self._handle = handle
        self._lib = _load_lib()

    def line(self, x, y, label: Optional[str] = None) -> EmbedSeries:
        xp, n = _to_float_ptr(x)
        yp, yn = _to_float_ptr(y)
        assert n == yn, f"x and y must have same length ({n} vs {yn})"
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes_line(self._handle, xp, yp, n, lbl)
        if not h:
            raise RuntimeError("Failed to create line series")
        return EmbedSeries(h)

    def scatter(self, x, y, label: Optional[str] = None) -> EmbedSeries:
        xp, n = _to_float_ptr(x)
        yp, yn = _to_float_ptr(y)
        assert n == yn, f"x and y must have same length ({n} vs {yn})"
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes_scatter(self._handle, xp, yp, n, lbl)
        if not h:
            raise RuntimeError("Failed to create scatter series")
        return EmbedSeries(h)

    def set_xlabel(self, label: str) -> None:
        """Set the X-axis label."""
        self._lib.spectra_axes_set_xlabel(self._handle, label.encode("utf-8"))

    def set_ylabel(self, label: str) -> None:
        """Set the Y-axis label."""
        self._lib.spectra_axes_set_ylabel(self._handle, label.encode("utf-8"))

    def set_title(self, title: str) -> None:
        """Set the axes title."""
        self._lib.spectra_axes_set_title(self._handle, title.encode("utf-8"))

    def set_xlim(self, min_val: float, max_val: float) -> None:
        """Set X-axis limits."""
        self._lib.spectra_axes_set_xlim(self._handle, min_val, max_val)

    def set_ylim(self, min_val: float, max_val: float) -> None:
        """Set Y-axis limits."""
        self._lib.spectra_axes_set_ylim(self._handle, min_val, max_val)

    def set_grid(self, enabled: bool = True) -> None:
        """Enable or disable grid lines."""
        self._lib.spectra_axes_set_grid(self._handle, 1 if enabled else 0)


class EmbedFigure:
    """Proxy for a figure in an embedded plot."""

    __slots__ = ("_handle", "_lib")

    def __init__(self, handle: ctypes.c_void_p) -> None:
        self._handle = handle
        self._lib = _load_lib()

    def subplot(self, rows: int = 1, cols: int = 1, index: int = 1) -> EmbedAxes:
        h = self._lib.spectra_figure_subplot(self._handle, rows, cols, index)
        if not h:
            raise RuntimeError("Failed to create subplot")
        return EmbedAxes(h)

    def subplot3d(self, rows: int = 1, cols: int = 1, index: int = 1) -> EmbedAxes:
        h = self._lib.spectra_figure_subplot3d(self._handle, rows, cols, index)
        if not h:
            raise RuntimeError("Failed to create 3D subplot")
        return EmbedAxes(h)

    def set_title(self, title: str) -> None:
        """Set the figure title (sets first axes title)."""
        self._lib.spectra_figure_set_title(self._handle, title.encode("utf-8"))


class EmbedSurface:
    """In-process GPU-accelerated plot renderer for embedding in GUI frameworks.

    Usage::

        surface = EmbedSurface(800, 600)
        fig = surface.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0,1,2,3], [0,1,4,9], label="data")
        pixels = surface.render()  # bytes, RGBA, 800*600*4
    """

    def __init__(self, width: int = 800, height: int = 600) -> None:
        self._lib = _load_lib()
        self._handle = self._lib.spectra_embed_create(width, height)
        if not self._handle:
            raise RuntimeError(
                "Failed to create EmbedSurface. "
                "Is Vulkan available? (headless rendering requires a Vulkan driver)"
            )
        self._width = width
        self._height = height
        # Pre-allocate pixel buffer
        self._buf = (ctypes.c_uint8 * (width * height * 4))()

    def __del__(self) -> None:
        if hasattr(self, "_handle") and self._handle:
            self._lib.spectra_embed_destroy(self._handle)
            self._handle = None

    @property
    def width(self) -> int:
        return self._lib.spectra_embed_width(self._handle)

    @property
    def height(self) -> int:
        return self._lib.spectra_embed_height(self._handle)

    @property
    def is_valid(self) -> bool:
        return bool(self._lib.spectra_embed_is_valid(self._handle))

    def figure(self) -> EmbedFigure:
        """Create a new figure on this surface."""
        h = self._lib.spectra_embed_figure(self._handle)
        if not h:
            raise RuntimeError("Failed to create figure")
        return EmbedFigure(h)

    def resize(self, width: int, height: int) -> bool:
        """Resize the offscreen framebuffer."""
        ok = self._lib.spectra_embed_resize(self._handle, width, height)
        if ok:
            self._width = width
            self._height = height
            self._buf = (ctypes.c_uint8 * (width * height * 4))()
        return bool(ok)

    def render(self) -> bytes:
        """Render one frame and return RGBA pixel data as bytes."""
        w = self.width
        h = self.height
        buf_size = w * h * 4
        if len(self._buf) != buf_size:
            self._buf = (ctypes.c_uint8 * buf_size)()
        ok = self._lib.spectra_embed_render(self._handle, self._buf)
        if not ok:
            raise RuntimeError("render_to_buffer failed")
        return bytes(self._buf)

    def render_into(self, buf: ctypes.Array) -> bool:
        """Render directly into a pre-allocated ctypes buffer (zero-copy)."""
        return bool(self._lib.spectra_embed_render(self._handle, buf))

    # ── Input forwarding ─────────────────────────────────────────────────

    def mouse_move(self, x: float, y: float) -> None:
        self._lib.spectra_embed_mouse_move(self._handle, x, y)

    def mouse_button(self, button: int, action: int, mods: int,
                     x: float, y: float) -> None:
        self._lib.spectra_embed_mouse_button(self._handle, button, action, mods, x, y)

    def scroll(self, dx: float, dy: float, cx: float, cy: float) -> None:
        self._lib.spectra_embed_scroll(self._handle, dx, dy, cx, cy)

    def key(self, key: int, action: int, mods: int) -> None:
        self._lib.spectra_embed_key(self._handle, key, action, mods)

    def update(self, dt: float) -> None:
        """Advance internal animations by dt seconds."""
        self._lib.spectra_embed_update(self._handle, dt)

    # ── Display configuration ────────────────────────────────────────────

    def set_dpi_scale(self, scale: float) -> None:
        """Set DPI scale factor (1.0 = 96 DPI, 2.0 = Retina/HiDPI)."""
        self._lib.spectra_embed_set_dpi_scale(self._handle, scale)

    @property
    def dpi_scale(self) -> float:
        """Get current DPI scale factor."""
        return self._lib.spectra_embed_get_dpi_scale(self._handle)

    # ── Theme & UI chrome ─────────────────────────────────────────────────

    def set_theme(self, theme: str) -> None:
        """Set theme ('dark' or 'light')."""
        self._lib.spectra_embed_set_theme(self._handle, theme.encode("utf-8"))

    def set_show_command_bar(self, visible: bool) -> None:
        """Show/hide the top command bar (requires ImGui build)."""
        self._lib.spectra_embed_set_show_command_bar(self._handle, 1 if visible else 0)

    def set_show_status_bar(self, visible: bool) -> None:
        """Show/hide the bottom status bar (requires ImGui build)."""
        self._lib.spectra_embed_set_show_status_bar(self._handle, 1 if visible else 0)

    def set_show_nav_rail(self, visible: bool) -> None:
        """Show/hide the left navigation rail (requires ImGui build)."""
        self._lib.spectra_embed_set_show_nav_rail(self._handle, 1 if visible else 0)

    def set_show_inspector(self, visible: bool) -> None:
        """Show/hide the right inspector panel (requires ImGui build)."""
        self._lib.spectra_embed_set_show_inspector(self._handle, 1 if visible else 0)
