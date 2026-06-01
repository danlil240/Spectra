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

# Callback prototypes (Phase 4). Keep references to installed callbacks alive
# on the owning Python object so they are not garbage-collected while the C
# side still holds the function pointer.
SpectraFrameCb = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.c_float, ctypes.c_float, ctypes.c_void_p
)
SpectraRedrawCb = ctypes.CFUNCTYPE(None, ctypes.c_void_p)

# Interactive event callback prototypes (Phase 3).
SpectraPointSelectedCb = ctypes.CFUNCTYPE(
    None, ctypes.c_int, ctypes.c_int, ctypes.c_size_t,
    ctypes.c_double, ctypes.c_double, ctypes.c_void_p
)
SpectraSeriesSelectedCb = ctypes.CFUNCTYPE(
    None, ctypes.c_int, ctypes.c_int, ctypes.c_void_p
)
SpectraHoverCb = ctypes.CFUNCTYPE(
    None, ctypes.c_int, ctypes.c_int, ctypes.c_size_t,
    ctypes.c_double, ctypes.c_double, ctypes.c_void_p
)
SpectraViewChangedCb = ctypes.CFUNCTYPE(
    None, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double,
    ctypes.c_void_p
)


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

    _lib.spectra_embed_create_ex.argtypes = [
        ctypes.c_uint32,   # width
        ctypes.c_uint32,   # height
        ctypes.c_char_p,   # theme
        ctypes.c_float,    # dpi_scale
        ctypes.c_uint32,   # msaa
        ctypes.c_float,    # bg_alpha
    ]
    _lib.spectra_embed_create_ex.restype = ctypes.c_void_p

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

    _lib.spectra_embed_set_background_alpha.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_embed_set_background_alpha.restype = None

    _lib.spectra_embed_get_background_alpha.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_get_background_alpha.restype = ctypes.c_float

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

    _lib.spectra_axes_auto_fit.argtypes = [ctypes.c_void_p]
    _lib.spectra_axes_auto_fit.restype = None

    _lib.spectra_axes_histogram.argtypes = [
        ctypes.c_void_p,                      # ax
        ctypes.POINTER(ctypes.c_float),        # values
        ctypes.c_uint32,                       # count
        ctypes.c_int,                          # bins
        ctypes.c_char_p,                       # label
    ]
    _lib.spectra_axes_histogram.restype = ctypes.c_void_p

    _lib.spectra_axes_bar.argtypes = [
        ctypes.c_void_p,                      # ax
        ctypes.POINTER(ctypes.c_float),        # positions
        ctypes.POINTER(ctypes.c_float),        # heights
        ctypes.c_uint32,                       # count
        ctypes.c_char_p,                       # label
    ]
    _lib.spectra_axes_bar.restype = ctypes.c_void_p

    # Figure configuration
    _lib.spectra_figure_set_title.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_figure_set_title.restype = None

    # ── Phase 1A: Series styling ──────────────────────────────────────────
    _lib.spectra_series_set_color.argtypes = [
        ctypes.c_void_p, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float
    ]
    _lib.spectra_series_set_color.restype = None
    _lib.spectra_series_set_opacity.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_series_set_opacity.restype = None
    _lib.spectra_series_set_line_width.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_series_set_line_width.restype = None
    _lib.spectra_series_set_marker_size.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_series_set_marker_size.restype = None
    _lib.spectra_series_set_marker_style.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_marker_style.restype = None
    _lib.spectra_series_set_line_style.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_line_style.restype = None
    _lib.spectra_series_set_label.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_series_set_label.restype = None

    # ── Phase 1G: Series streaming ────────────────────────────────────────
    _lib.spectra_series_append_xy.argtypes = [
        ctypes.c_void_p, ctypes.c_float, ctypes.c_float
    ]
    _lib.spectra_series_append_xy.restype = None
    _lib.spectra_series_append_data.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
    ]
    _lib.spectra_series_append_data.restype = None
    _lib.spectra_series_set_capacity.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    _lib.spectra_series_set_capacity.restype = None
    _lib.spectra_series_clear.argtypes = [ctypes.c_void_p]
    _lib.spectra_series_clear.restype = None

    # ── Phase 1B: Bar options ─────────────────────────────────────────────
    _lib.spectra_series_set_bar_width.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_series_set_bar_width.restype = None
    _lib.spectra_series_set_bar_baseline.argtypes = [ctypes.c_void_p, ctypes.c_float]
    _lib.spectra_series_set_bar_baseline.restype = None
    _lib.spectra_series_set_bar_orientation.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_bar_orientation.restype = None
    _lib.spectra_series_set_bar_gradient.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_bar_gradient.restype = None

    # ── Phase 1C: Histogram options ───────────────────────────────────────
    _lib.spectra_series_set_histogram_bins.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_histogram_bins.restype = None
    _lib.spectra_series_set_histogram_cumulative.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_histogram_cumulative.restype = None
    _lib.spectra_series_set_histogram_density.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_histogram_density.restype = None
    _lib.spectra_series_set_histogram_gradient.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_histogram_gradient.restype = None

    # ── Phase 1D: 3D series ───────────────────────────────────────────────
    _lib.spectra_axes3d_line.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_char_p,
    ]
    _lib.spectra_axes3d_line.restype = ctypes.c_void_p
    _lib.spectra_axes3d_scatter.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_char_p,
    ]
    _lib.spectra_axes3d_scatter.restype = ctypes.c_void_p
    _lib.spectra_axes3d_surf.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float), ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_float), ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_char_p,
    ]
    _lib.spectra_axes3d_surf.restype = ctypes.c_void_p
    _lib.spectra_series_set_z.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_uint32
    ]
    _lib.spectra_series_set_z.restype = None
    _lib.spectra_series_set_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_series_set_colormap.restype = None
    _lib.spectra_series_set_colormap_range.argtypes = [
        ctypes.c_void_p, ctypes.c_float, ctypes.c_float
    ]
    _lib.spectra_series_set_colormap_range.restype = None

    # ── Phase 1E: Legend control ──────────────────────────────────────────
    _lib.spectra_axes_show_legend.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_axes_show_legend.restype = None
    _lib.spectra_axes_set_legend_position.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_axes_set_legend_position.restype = None

    # ── Phase 1F: PNG render ──────────────────────────────────────────────
    _lib.spectra_embed_render_png.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.spectra_embed_render_png.restype = ctypes.c_int

    # ── Phase 2: Extra chrome setters + getters ───────────────────────────
    _lib.spectra_embed_set_show_legend.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_embed_set_show_legend.restype = None
    _lib.spectra_embed_set_show_crosshair.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.spectra_embed_set_show_crosshair.restype = None
    for _getter in (
        "spectra_embed_is_command_bar_visible",
        "spectra_embed_is_status_bar_visible",
        "spectra_embed_is_nav_rail_visible",
        "spectra_embed_is_inspector_visible",
        "spectra_embed_is_legend_visible",
        "spectra_embed_is_crosshair_visible",
    ):
        fn = getattr(_lib, _getter)
        fn.argtypes = [ctypes.c_void_p]
        fn.restype = ctypes.c_int

    # ── Phase 4: Animation & frame callbacks ──────────────────────────────
    _lib.spectra_embed_set_on_frame.argtypes = [
        ctypes.c_void_p, SpectraFrameCb, ctypes.c_void_p
    ]
    _lib.spectra_embed_set_on_frame.restype = None
    _lib.spectra_embed_clear_on_frame.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_clear_on_frame.restype = None
    _lib.spectra_embed_set_redraw_callback.argtypes = [
        ctypes.c_void_p, SpectraRedrawCb, ctypes.c_void_p
    ]
    _lib.spectra_embed_set_redraw_callback.restype = None
    _lib.spectra_embed_animation_play.argtypes = [
        ctypes.c_void_p, ctypes.c_float, ctypes.c_float
    ]
    _lib.spectra_embed_animation_play.restype = ctypes.c_int
    _lib.spectra_embed_animation_stop.argtypes = [ctypes.c_void_p]
    _lib.spectra_embed_animation_stop.restype = None

    # ── Phase 3: Interactive event callbacks ──────────────────────────────
    _lib.spectra_embed_set_on_point_selected.argtypes = [
        ctypes.c_void_p, SpectraPointSelectedCb, ctypes.c_void_p
    ]
    _lib.spectra_embed_set_on_point_selected.restype = None
    _lib.spectra_embed_set_on_series_selected.argtypes = [
        ctypes.c_void_p, SpectraSeriesSelectedCb, ctypes.c_void_p
    ]
    _lib.spectra_embed_set_on_series_selected.restype = None
    _lib.spectra_embed_set_on_hover.argtypes = [
        ctypes.c_void_p, SpectraHoverCb, ctypes.c_void_p
    ]
    _lib.spectra_embed_set_on_hover.restype = None
    _lib.spectra_embed_set_on_view_changed.argtypes = [
        ctypes.c_void_p, SpectraViewChangedCb, ctypes.c_void_p
    ]
    _lib.spectra_embed_set_on_view_changed.restype = None

    return _lib


# ─── Helper: convert Python list/numpy array to ctypes float pointer ─────────

def _to_cfloat(data) -> Tuple[ctypes.POINTER(ctypes.c_float), int, object]:
    """Convert a sequence/ndarray to (float pointer, count, owner).

    For contiguous float32 numpy arrays this is zero-copy; the returned owner
    must be kept alive by the caller for as long as the pointer is used.
    """
    try:
        import numpy as np
        if isinstance(data, np.ndarray):
            arr = np.ascontiguousarray(data, dtype=np.float32)
            ptr = arr.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            return ptr, arr.size, arr
    except ImportError:
        pass

    n = len(data)
    arr = (ctypes.c_float * n)(*data)
    return ctypes.cast(arr, ctypes.POINTER(ctypes.c_float)), n, arr


def _to_float_ptr(data) -> Tuple[ctypes.POINTER(ctypes.c_float), int]:
    """Convert a sequence of floats to (ctypes float pointer, count)."""
    ptr, n, _owner = _to_cfloat(data)
    return ptr, n


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

# Line styles (spectra::LineStyle)
LINE_NONE = 0
LINE_SOLID = 1
LINE_DASHED = 2
LINE_DOTTED = 3
_LINE_STYLES = {"none": 0, "solid": 1, "-": 1, "dashed": 2, "--": 2,
                "dotted": 3, ":": 3}

# Marker styles (spectra::MarkerStyle)
_MARKER_STYLES = {
    "none": 0, "circle": 1, "o": 1, "plus": 2, "+": 2, "cross": 3, "x": 3,
    "star": 4, "*": 4, "square": 5, "s": 5, "diamond": 6, "d": 6,
    "triangle_up": 7, "^": 7, "triangle_down": 8, "v": 8,
    "triangle_left": 9, "<": 9, "triangle_right": 10, ">": 10,
}

# Bar orientation
BAR_VERTICAL = 0
BAR_HORIZONTAL = 1

# Colormaps (spectra::ColormapType)
_COLORMAPS = {
    "none": 0, "viridis": 1, "plasma": 2, "inferno": 3, "magma": 4,
    "jet": 5, "coolwarm": 6, "grayscale": 7, "gray": 7,
}

# Legend positions (spectra::LegendPosition)
_LEGEND_POSITIONS = {
    "top_right": 0, "upper right": 0, "top_left": 1, "upper left": 1,
    "bottom_right": 2, "lower right": 2, "bottom_left": 3, "lower left": 3,
    "none": 4,
}


def _resolve(value, mapping, kind: str) -> int:
    """Resolve a style name (str) or int code to an integer enum value."""
    if isinstance(value, str):
        key = value.strip().lower()
        if key not in mapping:
            raise ValueError(f"Unknown {kind}: {value!r}")
        return mapping[key]
    return int(value)


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

    # ── Phase 1A: styling ────────────────────────────────────────────────

    def set_color(self, r: float, g: float, b: float, a: float = 1.0) -> "EmbedSeries":
        """Set the series color (RGBA components in [0, 1])."""
        self._lib.spectra_series_set_color(self._handle, r, g, b, a)
        return self

    def set_opacity(self, value: float) -> "EmbedSeries":
        """Set the series opacity (0 = transparent, 1 = opaque)."""
        self._lib.spectra_series_set_opacity(self._handle, value)
        return self

    def set_line_width(self, value: float) -> "EmbedSeries":
        """Set the line width in pixels (line series only)."""
        self._lib.spectra_series_set_line_width(self._handle, value)
        return self

    def set_marker_size(self, value: float) -> "EmbedSeries":
        """Set the marker size in pixels."""
        self._lib.spectra_series_set_marker_size(self._handle, value)
        return self

    def set_marker_style(self, style) -> "EmbedSeries":
        """Set the marker style (name like 'circle'/'square' or int code)."""
        self._lib.spectra_series_set_marker_style(
            self._handle, _resolve(style, _MARKER_STYLES, "marker style"))
        return self

    def set_line_style(self, style) -> "EmbedSeries":
        """Set the line style (name like 'solid'/'dashed'/'dotted' or int code)."""
        self._lib.spectra_series_set_line_style(
            self._handle, _resolve(style, _LINE_STYLES, "line style"))
        return self

    def set_label(self, label: str) -> "EmbedSeries":
        """Set the legend label for this series."""
        self._lib.spectra_series_set_label(self._handle, label.encode("utf-8"))
        return self

    # ── Phase 1G: streaming ──────────────────────────────────────────────

    def append_xy(self, x: float, y: float) -> None:
        """Append a single (x, y) sample to the series."""
        self._lib.spectra_series_append_xy(self._handle, x, y)

    def append_data(self, x, y) -> None:
        """Append a batch of (x, y) samples to the series."""
        xp, xn = _to_float_ptr(x)
        yp, yn = _to_float_ptr(y)
        assert xn == yn, f"x and y must have same length ({xn} vs {yn})"
        self._lib.spectra_series_append_data(self._handle, xp, yp, xn)

    def set_capacity(self, max_points: int) -> None:
        """Cap the number of retained points (ring buffer; 0 = unbounded)."""
        self._lib.spectra_series_set_capacity(self._handle, max_points)

    def clear(self) -> None:
        """Remove all data points from the series."""
        self._lib.spectra_series_clear(self._handle)

    # ── Phase 1B: bar options ────────────────────────────────────────────

    def set_bar_width(self, width: float) -> "EmbedSeries":
        """Set the bar width (bar series only)."""
        self._lib.spectra_series_set_bar_width(self._handle, width)
        return self

    def set_bar_baseline(self, baseline: float) -> "EmbedSeries":
        """Set the bar baseline value (bar series only)."""
        self._lib.spectra_series_set_bar_baseline(self._handle, baseline)
        return self

    def set_bar_orientation(self, orientation) -> "EmbedSeries":
        """Set the bar orientation ('vertical'/'horizontal' or int code)."""
        code = orientation
        if isinstance(orientation, str):
            code = BAR_HORIZONTAL if orientation.lower().startswith("h") else BAR_VERTICAL
        self._lib.spectra_series_set_bar_orientation(self._handle, int(code))
        return self

    def set_bar_gradient(self, enabled: bool = True) -> "EmbedSeries":
        """Enable/disable the bar gradient fill (bar series only)."""
        self._lib.spectra_series_set_bar_gradient(self._handle, 1 if enabled else 0)
        return self

    # ── Phase 1C: histogram options ──────────────────────────────────────

    def set_histogram_bins(self, bins: int) -> "EmbedSeries":
        """Set the number of histogram bins."""
        self._lib.spectra_series_set_histogram_bins(self._handle, bins)
        return self

    def set_histogram_cumulative(self, enabled: bool = True) -> "EmbedSeries":
        """Enable/disable cumulative histogram mode."""
        self._lib.spectra_series_set_histogram_cumulative(self._handle, 1 if enabled else 0)
        return self

    def set_histogram_density(self, enabled: bool = True) -> "EmbedSeries":
        """Enable/disable density-normalized histogram mode."""
        self._lib.spectra_series_set_histogram_density(self._handle, 1 if enabled else 0)
        return self

    def set_histogram_gradient(self, enabled: bool = True) -> "EmbedSeries":
        """Enable/disable the histogram gradient fill."""
        self._lib.spectra_series_set_histogram_gradient(self._handle, 1 if enabled else 0)
        return self

    # ── Phase 1D: 3D / colormap ──────────────────────────────────────────

    def set_z(self, z) -> None:
        """Update the Z data for a 3D line/scatter series."""
        zp, zn = _to_float_ptr(z)
        self._lib.spectra_series_set_z(self._handle, zp, zn)

    def set_colormap(self, colormap) -> "EmbedSeries":
        """Set the colormap (name like 'viridis' or int code)."""
        self._lib.spectra_series_set_colormap(
            self._handle, _resolve(colormap, _COLORMAPS, "colormap"))
        return self

    def set_colormap_range(self, min_val: float, max_val: float) -> "EmbedSeries":
        """Set the colormap value range."""
        self._lib.spectra_series_set_colormap_range(self._handle, min_val, max_val)
        return self


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

    def auto_fit(self) -> None:
        """Reset axis limits to encompass all series data."""
        self._lib.spectra_axes_auto_fit(self._handle)

    def histogram(self, values, bins: int = 30, label: Optional[str] = None) -> EmbedSeries:
        """Add a histogram series.

        Args:
            values: Data values (list, tuple, or numpy array).
            bins: Number of histogram bins (default 30).
            label: Optional series label for legend.

        Returns:
            EmbedSeries handle.
        """
        vp, n = _to_float_ptr(values)
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes_histogram(self._handle, vp, n, bins, lbl)
        if not h:
            raise RuntimeError("Failed to create histogram series")
        return EmbedSeries(h)

    def bar(self, positions, heights, label: Optional[str] = None) -> EmbedSeries:
        """Add a bar series.

        Args:
            positions: X positions of bars (list, tuple, or numpy array).
            heights: Height of each bar (list, tuple, or numpy array).
            label: Optional series label for legend.

        Returns:
            EmbedSeries handle.
        """
        pp, pn = _to_float_ptr(positions)
        hp, hn = _to_float_ptr(heights)
        assert pn == hn, f"positions and heights must have same length ({pn} vs {hn})"
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes_bar(self._handle, pp, hp, pn, lbl)
        if not h:
            raise RuntimeError("Failed to create bar series")
        return EmbedSeries(h)

    # ── Phase 1D: 3D series ──────────────────────────────────────────────

    def line3d(self, x, y, z, label: Optional[str] = None) -> EmbedSeries:
        """Add a 3D line series (requires a 3D subplot)."""
        xp, n = _to_float_ptr(x)
        yp, yn = _to_float_ptr(y)
        zp, zn = _to_float_ptr(z)
        assert n == yn == zn, f"x, y, z must have same length ({n}, {yn}, {zn})"
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes3d_line(self._handle, xp, yp, zp, n, lbl)
        if not h:
            raise RuntimeError("Failed to create 3D line series")
        return EmbedSeries(h)

    def scatter3d(self, x, y, z, label: Optional[str] = None) -> EmbedSeries:
        """Add a 3D scatter series (requires a 3D subplot)."""
        xp, n = _to_float_ptr(x)
        yp, yn = _to_float_ptr(y)
        zp, zn = _to_float_ptr(z)
        assert n == yn == zn, f"x, y, z must have same length ({n}, {yn}, {zn})"
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes3d_scatter(self._handle, xp, yp, zp, n, lbl)
        if not h:
            raise RuntimeError("Failed to create 3D scatter series")
        return EmbedSeries(h)

    def surf(self, x_grid, y_grid, z_values, label: Optional[str] = None) -> EmbedSeries:
        """Add a 3D surface from grid vectors.

        ``z_values`` is row-major with ``len(x_grid) * len(y_grid)`` entries.
        """
        xp, nx = _to_float_ptr(x_grid)
        yp, ny = _to_float_ptr(y_grid)
        zp, nz = _to_float_ptr(z_values)
        assert nz == nx * ny, f"z_values must have nx*ny entries ({nz} vs {nx*ny})"
        lbl = label.encode("utf-8") if label else None
        h = self._lib.spectra_axes3d_surf(self._handle, xp, nx, yp, ny, zp, lbl)
        if not h:
            raise RuntimeError("Failed to create 3D surface series")
        return EmbedSeries(h)

    # ── Phase 1E: legend control ─────────────────────────────────────────

    def show_legend(self, visible: bool = True) -> None:
        """Show or hide the legend for this axes' figure."""
        self._lib.spectra_axes_show_legend(self._handle, 1 if visible else 0)

    def set_legend_position(self, position) -> None:
        """Set the legend position (name like 'top_right' or int code)."""
        self._lib.spectra_axes_set_legend_position(
            self._handle, _resolve(position, _LEGEND_POSITIONS, "legend position"))


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

    Extended creation::

        surface = EmbedSurface(800, 600, theme="dark", dpi_scale=2.0, msaa=4)
        surface = EmbedSurface(800, 600, background_alpha=0.0)  # transparent bg
    """

    def __init__(
        self,
        width: int = 800,
        height: int = 600,
        *,
        theme: Optional[str] = None,
        dpi_scale: float = 1.0,
        msaa: int = 1,
        background_alpha: float = 1.0,
    ) -> None:
        self._lib = _load_lib()
        if theme or dpi_scale != 1.0 or msaa != 1 or background_alpha != 1.0:
            theme_bytes = theme.encode("utf-8") if theme else None
            self._handle = self._lib.spectra_embed_create_ex(
                width, height, theme_bytes, dpi_scale, msaa, background_alpha
            )
        else:
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
        # Keep installed C callbacks alive (Phase 4 & Phase 3).
        self._frame_cb = None
        self._redraw_cb = None
        self._point_cb = None
        self._series_cb = None
        self._hover_cb = None
        self._view_cb = None

    def __del__(self) -> None:
        if hasattr(self, "_handle") and self._handle:
            self._lib.spectra_embed_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> "EmbedSurface":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        self.close()
        return False

    def close(self) -> None:
        """Destroy the underlying surface and release GPU resources."""
        if getattr(self, "_handle", None):
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

    # ── Phase 5C: rich output helpers ────────────────────────────────────

    def render_numpy(self):
        """Render one frame and return an (H, W, 4) uint8 RGBA numpy array."""
        import numpy as np
        w = self.width
        h = self.height
        buf_size = w * h * 4
        if len(self._buf) != buf_size:
            self._buf = (ctypes.c_uint8 * buf_size)()
        ok = self._lib.spectra_embed_render(self._handle, self._buf)
        if not ok:
            raise RuntimeError("render failed")
        arr = np.frombuffer(self._buf, dtype=np.uint8, count=buf_size)
        return arr.reshape(h, w, 4).copy()

    def render_pil(self):
        """Render one frame and return a PIL.Image (RGBA)."""
        from PIL import Image
        return Image.frombytes("RGBA", (self.width, self.height), self.render())

    def render_png(self, path) -> None:
        """Render the current frame directly to a PNG file."""
        ok = self._lib.spectra_embed_render_png(self._handle, str(path).encode("utf-8"))
        if not ok:
            raise RuntimeError(f"Failed to write PNG to {path!r}")

    def _repr_png_(self) -> Optional[bytes]:
        """Return PNG bytes for inline display in Jupyter notebooks."""
        try:
            import io
            img = self.render_pil()
            buf = io.BytesIO()
            img.save(buf, format="PNG")
            return buf.getvalue()
        except Exception:
            return None

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

    @property
    def background_alpha(self) -> float:
        """Get background alpha (1.0 = opaque, 0.0 = transparent)."""
        return self._lib.spectra_embed_get_background_alpha(self._handle)

    @background_alpha.setter
    def background_alpha(self, alpha: float) -> None:
        """Set background alpha (1.0 = opaque, 0.0 = transparent)."""
        self._lib.spectra_embed_set_background_alpha(self._handle, alpha)

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

    def set_show_legend(self, visible: bool) -> None:
        """Show/hide the plot legend."""
        self._lib.spectra_embed_set_show_legend(self._handle, 1 if visible else 0)

    def set_show_crosshair(self, visible: bool) -> None:
        """Show/hide the data crosshair overlay."""
        self._lib.spectra_embed_set_show_crosshair(self._handle, 1 if visible else 0)

    @property
    def command_bar_visible(self) -> bool:
        return bool(self._lib.spectra_embed_is_command_bar_visible(self._handle))

    @property
    def status_bar_visible(self) -> bool:
        return bool(self._lib.spectra_embed_is_status_bar_visible(self._handle))

    @property
    def nav_rail_visible(self) -> bool:
        return bool(self._lib.spectra_embed_is_nav_rail_visible(self._handle))

    @property
    def inspector_visible(self) -> bool:
        return bool(self._lib.spectra_embed_is_inspector_visible(self._handle))

    @property
    def legend_visible(self) -> bool:
        return bool(self._lib.spectra_embed_is_legend_visible(self._handle))

    @property
    def crosshair_visible(self) -> bool:
        return bool(self._lib.spectra_embed_is_crosshair_visible(self._handle))

    # ── Phase 4: animation & frame callbacks ─────────────────────────────

    def set_on_frame(self, callback) -> None:
        """Register a per-frame callback ``callback(surface, time_sec, dt_sec)``.

        Invoked from within :meth:`update` (and :meth:`animation_play`).
        Pass ``None`` to clear the callback.
        """
        if callback is None:
            self.clear_on_frame()
            return

        def _trampoline(_handle, time_sec, dt_sec, _user):
            callback(self, time_sec, dt_sec)

        self._frame_cb = SpectraFrameCb(_trampoline)
        self._lib.spectra_embed_set_on_frame(self._handle, self._frame_cb, None)

    def clear_on_frame(self) -> None:
        """Remove any installed per-frame callback."""
        self._lib.spectra_embed_clear_on_frame(self._handle)
        self._frame_cb = None

    def set_redraw_callback(self, callback) -> None:
        """Register a redraw callback ``callback()`` invoked on repaint requests."""
        if callback is None:
            self._redraw_cb = None
            self._lib.spectra_embed_set_redraw_callback(self._handle, SpectraRedrawCb(0), None)
            return

        def _trampoline(_user):
            callback()

        self._redraw_cb = SpectraRedrawCb(_trampoline)
        self._lib.spectra_embed_set_redraw_callback(self._handle, self._redraw_cb, None)

    def animation_play(self, fps: float = 60.0, duration: float = 0.0) -> int:
        """Drive the frame callback for ``duration`` seconds at ``fps``.

        Returns the number of frames stepped. Pass ``duration <= 0`` for a
        single step.
        """
        return int(self._lib.spectra_embed_animation_play(self._handle, fps, duration))

    def animation_stop(self) -> None:
        """Stop the animation loop and reset the elapsed timeline."""
        self._lib.spectra_embed_animation_stop(self._handle)

    # ── Phase 3: interactive event callbacks ─────────────────────────────

    def set_on_point_selected(self, callback) -> None:
        """Register ``callback(axes_index, series_index, point_index, x, y)``.

        Fired when the user selects a concrete data point. Requires the ImGui
        chrome build. Pass ``None`` to clear.
        """
        if callback is None:
            self._point_cb = None
            self._lib.spectra_embed_set_on_point_selected(
                self._handle, ctypes.cast(None, SpectraPointSelectedCb), None)
            return

        def _trampoline(ai, si, pi, x, y, _user):
            callback(ai, si, pi, x, y)

        self._point_cb = SpectraPointSelectedCb(_trampoline)
        self._lib.spectra_embed_set_on_point_selected(self._handle, self._point_cb, None)

    def set_on_series_selected(self, callback) -> None:
        """Register ``callback(axes_index, series_index)``.

        Fired when the user selects a series. Requires the ImGui chrome build.
        Pass ``None`` to clear.
        """
        if callback is None:
            self._series_cb = None
            self._lib.spectra_embed_set_on_series_selected(
                self._handle, ctypes.cast(None, SpectraSeriesSelectedCb), None)
            return

        def _trampoline(ai, si, _user):
            callback(ai, si)

        self._series_cb = SpectraSeriesSelectedCb(_trampoline)
        self._lib.spectra_embed_set_on_series_selected(self._handle, self._series_cb, None)

    def set_on_hover(self, callback) -> None:
        """Register ``callback(axes_index, series_index, point_index, x, y)``.

        Fired when the hovered/nearest data point changes; ``series_index < 0``
        means the cursor moved away from any series. Pass ``None`` to clear.
        """
        if callback is None:
            self._hover_cb = None
            self._lib.spectra_embed_set_on_hover(
                self._handle, ctypes.cast(None, SpectraHoverCb), None)
            return

        def _trampoline(ai, si, pi, x, y, _user):
            callback(ai, si, pi, x, y)

        self._hover_cb = SpectraHoverCb(_trampoline)
        self._lib.spectra_embed_set_on_hover(self._handle, self._hover_cb, None)

    def set_on_view_changed(self, callback) -> None:
        """Register ``callback(xmin, xmax, ymin, ymax)``.

        Fired when the visible data range of the active axes changes via
        pan/zoom/auto-fit. Pass ``None`` to clear.
        """
        if callback is None:
            self._view_cb = None
            self._lib.spectra_embed_set_on_view_changed(
                self._handle, ctypes.cast(None, SpectraViewChangedCb), None)
            return

        def _trampoline(xmin, xmax, ymin, ymax, _user):
            callback(xmin, xmax, ymin, ymax)

        self._view_cb = SpectraViewChangedCb(_trampoline)
        self._lib.spectra_embed_set_on_view_changed(self._handle, self._view_cb, None)

    # ── Phase 5E: builder ────────────────────────────────────────────────

    @staticmethod
    def builder() -> "EmbedSurfaceBuilder":
        """Return a fluent builder for configuring an :class:`EmbedSurface`."""
        return EmbedSurfaceBuilder()


class EmbedSurfaceBuilder:
    """Fluent builder for :class:`EmbedSurface`.

    Example::

        surface = (EmbedSurface.builder()
                   .size(1024, 768)
                   .theme("dark")
                   .with_inspector()
                   .build())
    """

    def __init__(self) -> None:
        self._width = 800
        self._height = 600
        self._theme: Optional[str] = None
        self._dpi_scale = 1.0
        self._msaa = 1
        self._background_alpha = 1.0
        self._chrome: dict = {}

    def size(self, width: int, height: int) -> "EmbedSurfaceBuilder":
        self._width = width
        self._height = height
        return self

    def theme(self, name: str) -> "EmbedSurfaceBuilder":
        self._theme = name
        return self

    def dpi_scale(self, scale: float) -> "EmbedSurfaceBuilder":
        self._dpi_scale = scale
        return self

    def msaa(self, samples: int) -> "EmbedSurfaceBuilder":
        self._msaa = samples
        return self

    def background_alpha(self, alpha: float) -> "EmbedSurfaceBuilder":
        self._background_alpha = alpha
        return self

    def transparent(self) -> "EmbedSurfaceBuilder":
        self._background_alpha = 0.0
        return self

    def with_inspector(self, visible: bool = True) -> "EmbedSurfaceBuilder":
        self._chrome["inspector"] = visible
        return self

    def with_command_bar(self, visible: bool = True) -> "EmbedSurfaceBuilder":
        self._chrome["command_bar"] = visible
        return self

    def with_status_bar(self, visible: bool = True) -> "EmbedSurfaceBuilder":
        self._chrome["status_bar"] = visible
        return self

    def with_nav_rail(self, visible: bool = True) -> "EmbedSurfaceBuilder":
        self._chrome["nav_rail"] = visible
        return self

    def without_legend(self) -> "EmbedSurfaceBuilder":
        self._chrome["legend"] = False
        return self

    def with_crosshair(self, visible: bool = True) -> "EmbedSurfaceBuilder":
        self._chrome["crosshair"] = visible
        return self

    def build(self) -> EmbedSurface:
        surface = EmbedSurface(
            self._width,
            self._height,
            theme=self._theme,
            dpi_scale=self._dpi_scale,
            msaa=self._msaa,
            background_alpha=self._background_alpha,
        )
        setters = {
            "inspector": surface.set_show_inspector,
            "command_bar": surface.set_show_command_bar,
            "status_bar": surface.set_show_status_bar,
            "nav_rail": surface.set_show_nav_rail,
            "legend": surface.set_show_legend,
            "crosshair": surface.set_show_crosshair,
        }
        for key, visible in self._chrome.items():
            setters[key](visible)
        return surface
