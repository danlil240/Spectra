"""Spectra — GPU-accelerated scientific plotting via IPC.

The easiest plotting library. One line to plot.

Usage::

    import spectra as sp

    sp.plot([1, 4, 9, 16])                    # that's it. window opens.
    sp.plot(x, y, color="red", label="data")   # with style
    sp.hline(0)                                # horizontal reference line
    sp.fplot(lambda x: x**2, -2, 2)            # plot a function
    sp.scatter(x, y)                           # scatter plot
    sp.hist(data, bins=50)                     # histogram
    sp.plot3(x, y, z)                          # 3D line
    sp.live(lambda t: math.sin(t))             # live streaming

    sp.show()  # optional — block until windows closed

In Jupyter, figures are drawn inline as images under each cell instead
(``sp.use_window()`` switches to native windows, ``sp.use_inline()`` back).

Advanced usage (full control)::

    s = sp.Session()
    fig = s.figure("My Plot")
    ax = fig.subplot(1, 1, 1)
    line = ax.line(x, y, label="data")
    fig.show()
    s.show()
"""

from ._session import Session
from ._figure import Figure
from ._axes import Axes, TextAnnotation
from ._series import Series
from ._errors import (
    SpectraError,
    ConnectionError,
    ProtocolError,
    TimeoutError,
    FigureNotFoundError,
    BackendError,
)
from ._animation import ipc_sleep, FramePacer, BackendAnimator
from ._log import set_log_level, get_log_level
from .topic import Publisher
from . import style
from ._inline import use_inline, use_window, display_mode

# ─── Easy API (one-liners, everything in background) ─────────────────────────
from ._easy import (
    plot,
    step,
    stairs,
    scatter,
    stem,
    errorbar,
    hist,
    bar,
    barh,
    fill_between,
    boxplot,
    violin_plot,
    hline,
    vline,
    fplot,
    ihline,
    ivline,
    ifplot,
    plot3,
    scatter3,
    surf,
    plotn,
    subplots,
    figure,
    tab,
    subplot,
    subplot3d,
    gcf,
    gca,
    twinx,
    title,
    suptitle,
    tight_layout,
    xlabel,
    ylabel,
    xlim,
    ylim,
    grid,
    legend,
    colorbar,
    xticks,
    yticks,
    text,
    annotate,
    live,
    stop_live,
    append,
    show,
    close,
    close_all,
    clear,
    savefig,
    xscale,
    yscale,
    semilogx,
    semilogy,
    loglog,
)

# ─── Backward compatibility ───────────────────────────────────────────────────
# Old API used sp.line(x, y) — keep as alias for sp.plot(x, y)
line = plot

# Old tests reference these module-level attributes
_default_session = None
_current_figure = None
_current_axes = None

def _resolve_version() -> str:
    """Installed distribution metadata first; version.txt for source checkouts."""
    try:
        from importlib.metadata import PackageNotFoundError, version as _pkg_version
    except ImportError:  # pragma: no cover - Python < 3.8
        PackageNotFoundError, _pkg_version = Exception, None
    if _pkg_version is not None:
        try:
            return _pkg_version("spectra-plot")
        except PackageNotFoundError:
            pass
    # Running from a source checkout without an install: <repo>/python/spectra/
    import os as _os

    _vfile = _os.path.join(_os.path.dirname(__file__), "..", "..", "version.txt")
    try:
        with open(_vfile, encoding="utf-8") as _f:
            return _f.read().strip()
    except OSError:
        return "0+unknown"


__version__ = _resolve_version()

__all__ = [
    # Easy API — one-liners
    "plot",
    "step",
    "stairs",
    "scatter",
    "stem",
    "errorbar",
    "hist",
    "bar",
    "barh",
    "fill_between",
    "boxplot",
    "violin_plot",
    "hline",
    "vline",
    "fplot",
    "ihline",
    "ivline",
    "ifplot",
    "plot3",
    "scatter3",
    "surf",
    "plotn",
    "subplots",
    "figure",
    "tab",
    "subplot",
    "subplot3d",
    "gcf",
    "gca",
    "twinx",
    "title",
    "suptitle",
    "tight_layout",
    "xlabel",
    "ylabel",
    "xlim",
    "ylim",
    "grid",
    "legend",
    "colorbar",
    "xticks",
    "yticks",
    "text",
    "annotate",
    "live",
    "stop_live",
    "append",
    "show",
    "close",
    "close_all",
    "clear",
    "savefig",
    "xscale",
    "yscale",
    "semilogx",
    "semilogy",
    "loglog",
    # Plot style presets (spectra.style.use / spectra.style.context)
    "style",
    # Backward compat
    "line",
    # Advanced API — full control
    "Session",
    "Figure",
    "Axes",
    "TextAnnotation",
    "Series",
    "SpectraError",
    "ConnectionError",
    "ProtocolError",
    "TimeoutError",
    "FigureNotFoundError",
    "BackendError",
    "ipc_sleep",
    "FramePacer",
    "BackendAnimator",
    "set_log_level",
    "get_log_level",
    "Publisher",
    "use_inline",
    "use_window",
    "display_mode",
]
