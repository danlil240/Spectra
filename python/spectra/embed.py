"""Easy offscreen rendering — one call to render a plot to pixels or file.

No daemon, no IPC, no windows. Uses the Vulkan GPU directly in-process.
Requires ``libspectra_embed.so`` (build with ``-DSPECTRA_BUILD_EMBED_SHARED=ON``).

Usage::

    import spectra.embed as spe

    # Render to pixels (bytes, RGBA)
    img = spe.render(x, y)
    img.width, img.height, img.data  # 800, 600, bytes(800*600*4)

    # Save to PNG
    spe.render(x, y, save="plot.png")

    # Scatter
    spe.scatter(x, y, save="scatter.png")

    # Custom size + title
    spe.render(x, y, width=1920, height=1080, title="My Plot", save="hd.png")

    # Multiple series
    spe.render_multi([
        (x, y1, "sin"),
        (x, y2, "cos"),
    ], title="Comparison", save="multi.png")

    # Histogram
    spe.histogram(values, bins=50, save="hist.png")
"""

from __future__ import annotations

import ctypes
from typing import List, Optional, Sequence, Tuple, Union

# Re-use the library loader from the low-level embed module
from ._embed import _load_lib, _to_float_ptr

# Type alias
ArrayLike = Union[List[float], Tuple[float, ...], Sequence[float]]


# ─── Rendered Image ──────────────────────────────────────────────────────────


class Image:
    """Container for rendered RGBA pixel data."""

    __slots__ = ("data", "width", "height")

    def __init__(self, data: bytes, width: int, height: int) -> None:
        self.data = data
        self.width = width
        self.height = height

    @property
    def stride(self) -> int:
        return self.width * 4

    @property
    def size_bytes(self) -> int:
        return len(self.data)

    def __len__(self) -> int:
        return len(self.data)

    def __bool__(self) -> bool:
        return len(self.data) > 0

    def save(self, path: str) -> bool:
        """Save to PNG file. Returns True on success."""
        return _save_png_raw(self.data, self.width, self.height, path)


def _save_png_raw(data: bytes, width: int, height: int, path: str) -> bool:
    """Save raw RGBA bytes to PNG using the C API."""
    # We use spectra_render_line_png indirectly; but for raw pixels we need
    # a different approach. Let's use the low-level embed surface.
    # Actually, we can write PNG ourselves using the C library's internal
    # ImageExporter, but that's not exposed through the C API for raw pixels.
    # Instead, use PIL if available, or stb_image_write via ctypes.
    try:
        from PIL import Image as PILImage
        img = PILImage.frombytes("RGBA", (width, height), data)
        img.save(path)
        return True
    except ImportError:
        pass

    # Fallback: write raw RGBA as a simple PPM (no alpha)
    # Not ideal, but works without dependencies
    try:
        with open(path, "wb") as f:
            f.write(f"P6\n{width} {height}\n255\n".encode())
            for i in range(0, len(data), 4):
                f.write(bytes([data[i], data[i + 1], data[i + 2]]))
        return True
    except Exception:
        return False


# ─── Library function declarations ───────────────────────────────────────────


_easy_funcs_declared = False


def _ensure_easy_funcs():
    """Declare the easy render C API functions once."""
    global _easy_funcs_declared
    if _easy_funcs_declared:
        return
    lib = _load_lib()

    # spectra_render_line
    lib.spectra_render_line.argtypes = [
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.spectra_render_line.restype = ctypes.POINTER(ctypes.c_uint8)

    # spectra_render_scatter
    lib.spectra_render_scatter.argtypes = [
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.spectra_render_scatter.restype = ctypes.POINTER(ctypes.c_uint8)

    # spectra_render_line_png
    lib.spectra_render_line_png.argtypes = [
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_char_p,
    ]
    lib.spectra_render_line_png.restype = ctypes.c_int

    # spectra_render_scatter_png
    lib.spectra_render_scatter_png.argtypes = [
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_char_p,
    ]
    lib.spectra_render_scatter_png.restype = ctypes.c_int

    # spectra_free_pixels
    lib.spectra_free_pixels.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
    lib.spectra_free_pixels.restype = None

    _easy_funcs_declared = True


# ─── Internal render helper ──────────────────────────────────────────────────


def _render_impl(
    x,
    y,
    width: int,
    height: int,
    save: Optional[str],
    scatter: bool,
) -> Image:
    """Core render implementation shared by render() and scatter()."""
    _ensure_easy_funcs()
    lib = _load_lib()

    xp, xn = _to_float_ptr(x)
    yp, yn = _to_float_ptr(y)
    count = min(xn, yn)

    if save:
        # Direct-to-PNG path (more efficient, no Python-side copy)
        path_bytes = save.encode("utf-8")
        if scatter:
            ok = lib.spectra_render_scatter_png(xp, yp, count, width, height, path_bytes)
        else:
            ok = lib.spectra_render_line_png(xp, yp, count, width, height, path_bytes)
        if not ok:
            raise RuntimeError(f"Failed to render and save to {save}")

        # Also render to pixels so the caller gets an Image back
        # (For efficiency, if they only want the file, they still get the image)

    # Render to pixels
    out_w = ctypes.c_uint32(0)
    out_h = ctypes.c_uint32(0)

    if scatter:
        ptr = lib.spectra_render_scatter(xp, yp, count, width, height,
                                          ctypes.byref(out_w), ctypes.byref(out_h))
    else:
        ptr = lib.spectra_render_line(xp, yp, count, width, height,
                                       ctypes.byref(out_w), ctypes.byref(out_h))

    if not ptr:
        raise RuntimeError(
            "Failed to render plot. Is Vulkan available? "
            "(headless rendering requires a Vulkan driver)"
        )

    w = out_w.value
    h = out_h.value
    nbytes = w * h * 4

    # Copy pixels to Python bytes, then free the C buffer
    data = ctypes.string_at(ptr, nbytes)
    lib.spectra_free_pixels(ptr)

    return Image(data, w, h)


# ─── Public API ──────────────────────────────────────────────────────────────


def render(
    x,
    y,
    *,
    width: int = 800,
    height: int = 600,
    save: Optional[str] = None,
    title: Optional[str] = None,
    xlabel: Optional[str] = None,
    ylabel: Optional[str] = None,
    theme: Optional[str] = None,
    fmt: str = "-",
    grid: bool = True,
) -> Image:
    """Render a line plot to pixels.

    Args:
        x: X data (list, tuple, or numpy array).
        y: Y data (list, tuple, or numpy array).
        width: Image width in pixels (default 800).
        height: Image height in pixels (default 600).
        save: If provided, save PNG to this path.
        title: Plot title.
        xlabel: X axis label.
        ylabel: Y axis label.
        theme: Theme name ("dark", "night", or "light").
        fmt: MATLAB-style format string (default "-").
        grid: Whether to show the grid (default True).

    Returns:
        Image with .data (bytes), .width, .height attributes.

    Example::

        import spectra.embed as spe
        img = spe.render([0,1,2,3], [0,1,4,9])
        spe.render([0,1,2,3], [0,1,4,9], save="plot.png", title="My Plot")
    """
    if title or xlabel or ylabel or theme or fmt != "-" or not grid:
        return _render_with_options(x, y, width=width, height=height, save=save,
                                     title=title, xlabel=xlabel, ylabel=ylabel,
                                     theme=theme, fmt=fmt, grid=grid, scatter=False)
    return _render_impl(x, y, width, height, save, scatter=False)


def scatter(
    x,
    y,
    *,
    width: int = 800,
    height: int = 600,
    save: Optional[str] = None,
    title: Optional[str] = None,
    xlabel: Optional[str] = None,
    ylabel: Optional[str] = None,
    theme: Optional[str] = None,
    grid: bool = True,
) -> Image:
    """Render a scatter plot to pixels.

    Same arguments as render() (except no ``fmt``). Returns Image.

    Example::

        import spectra.embed as spe
        spe.scatter([1,2,3,4], [1,4,2,8], save="scatter.png")
    """
    if title or xlabel or ylabel or theme or not grid:
        return _render_with_options(x, y, width=width, height=height, save=save,
                                     title=title, xlabel=xlabel, ylabel=ylabel,
                                     theme=theme, fmt="-", grid=grid, scatter=True)
    return _render_impl(x, y, width, height, save, scatter=True)


def render_multi(
    series_list: List[Tuple],
    *,
    width: int = 800,
    height: int = 600,
    save: Optional[str] = None,
    title: Optional[str] = None,
    xlabel: Optional[str] = None,
    ylabel: Optional[str] = None,
    theme: Optional[str] = None,
    grid: bool = True,
) -> Image:
    """Render multiple series on a single plot.

    Args:
        series_list: List of (x, y) or (x, y, label) tuples.
        width: Image width.
        height: Image height.
        save: Save path (PNG).
        title: Plot title.
        xlabel: X axis label.
        ylabel: Y axis label.
        theme: Theme name ("dark", "night", or "light").
        grid: Whether to show the grid (default True).

    Example::

        spe.render_multi([
            ([0,1,2,3], [0,1,4,9], "quadratic"),
            ([0,1,2,3], [0,1,2,3], "linear"),
        ], title="Comparison", save="multi.png")
    """
    return _render_with_options_multi(series_list, width=width, height=height,
                                      save=save, title=title, xlabel=xlabel,
                                      ylabel=ylabel, theme=theme, grid=grid)


def histogram(
    values,
    *,
    bins: int = 30,
    width: int = 800,
    height: int = 600,
    save: Optional[str] = None,
    title: Optional[str] = None,
    xlabel: Optional[str] = None,
    ylabel: Optional[str] = None,
    theme: Optional[str] = None,
) -> Image:
    """Render a histogram to pixels.

    Uses the native Axes.histogram() series for accurate rendering.

    Example::

        import numpy as np
        spe.histogram(np.random.randn(10000), bins=50, save="hist.png")
    """
    return _render_histogram_impl(values, bins=bins, width=width, height=height,
                                   save=save, title=title, xlabel=xlabel,
                                   ylabel=ylabel, theme=theme)


def render_bar(
    positions,
    heights,
    *,
    width: int = 800,
    height: int = 600,
    save: Optional[str] = None,
    title: Optional[str] = None,
    xlabel: Optional[str] = None,
    ylabel: Optional[str] = None,
    theme: Optional[str] = None,
    label: Optional[str] = None,
    grid: bool = True,
) -> Image:
    """Render a bar chart to pixels.

    Args:
        positions: X positions of the bars.
        heights: Height of each bar.
        width: Image width in pixels (default 800).
        height: Image height in pixels (default 600).
        save: Save path (PNG).
        title: Plot title.
        xlabel: X axis label.
        ylabel: Y axis label.
        theme: Theme name ("dark", "night", or "light").
        label: Series label for legend.
        grid: Whether to show the grid (default True).

    Returns:
        Image with .data (bytes), .width, .height attributes.

    Example::

        spe.render_bar([1,2,3,4,5], [10,25,15,30,20], title="Sales", save="bar.png")
    """
    return _render_bar_impl(positions, heights, width=width, height=height,
                             save=save, title=title, xlabel=xlabel,
                             ylabel=ylabel, theme=theme, label=label, grid=grid)


# ─── Full-surface path (supports titles, labels, multi-series) ───────────────

def _render_with_options(
    x, y, *, width, height, save, title, xlabel, ylabel,
    theme=None, fmt="-", grid=True, scatter_mode=False, scatter=False
) -> Image:
    """Use the full EmbedSurface for richer rendering with titles/labels/theme.

    dpi_scale and msaa intentionally use defaults (1.0 and 1) since the
    high-level render API targets pixel-exact offscreen output.
    """
    from ._embed import EmbedSurface

    surface = EmbedSurface(width, height, theme=theme)
    fig = surface.figure()
    ax = fig.subplot(1, 1, 1)

    if scatter:
        ax.scatter(x, y)
    else:
        ax.line(x, y)

    if title:
        ax.set_title(title)
    if xlabel:
        ax.set_xlabel(xlabel)
    if ylabel:
        ax.set_ylabel(ylabel)
    ax.set_grid(grid)
    ax.auto_fit()

    pixels = surface.render()
    img = Image(pixels, surface.width, surface.height)

    if save:
        img.save(save)

    return img


def _render_with_options_multi(
    series_list, *, width, height, save, title, xlabel, ylabel, theme=None, grid=True
) -> Image:
    """Multi-series render using full EmbedSurface."""
    from ._embed import EmbedSurface

    surface = EmbedSurface(width, height, theme=theme)
    fig = surface.figure()
    ax = fig.subplot(1, 1, 1)

    for entry in series_list:
        if len(entry) >= 3:
            x, y, label = entry[0], entry[1], entry[2]
            ax.line(x, y, label=label)
        else:
            x, y = entry[0], entry[1]
            ax.line(x, y)

    if title:
        ax.set_title(title)
    if xlabel:
        ax.set_xlabel(xlabel)
    if ylabel:
        ax.set_ylabel(ylabel)
    ax.set_grid(grid)
    ax.auto_fit()

    pixels = surface.render()
    img = Image(pixels, surface.width, surface.height)

    if save:
        img.save(save)

    return img


def _render_histogram_impl(values, *, bins, width, height, save, title,
                             xlabel=None, ylabel=None, theme=None) -> Image:
    """Histogram render using native Axes.histogram() series."""
    from ._embed import EmbedSurface

    surface = EmbedSurface(width, height, theme=theme)
    fig = surface.figure()
    ax = fig.subplot(1, 1, 1)
    ax.histogram(values, bins=bins)

    if title:
        ax.set_title(title)
    if xlabel:
        ax.set_xlabel(xlabel)
    if ylabel:
        ax.set_ylabel(ylabel)
    ax.auto_fit()

    pixels = surface.render()
    img = Image(pixels, surface.width, surface.height)

    if save:
        img.save(save)

    return img


def _render_bar_impl(positions, heights, *, width, height, save, title,
                      xlabel=None, ylabel=None, theme=None, label=None, grid=True) -> Image:
    """Bar chart render using native Axes.bar() series."""
    from ._embed import EmbedSurface

    surface = EmbedSurface(width, height, theme=theme)
    fig = surface.figure()
    ax = fig.subplot(1, 1, 1)
    ax.bar(positions, heights, label=label)

    if title:
        ax.set_title(title)
    if xlabel:
        ax.set_xlabel(xlabel)
    if ylabel:
        ax.set_ylabel(ylabel)
    ax.set_grid(grid)
    ax.auto_fit()

    pixels = surface.render()
    img = Image(pixels, surface.width, surface.height)

    if save:
        img.save(save)

    return img
