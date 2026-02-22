"""Spectra — GPU-accelerated scientific plotting via IPC.

The easiest plotting library. One line to plot.

Usage::

    import spectra as sp

    sp.plot([1, 4, 9, 16])                    # that's it. window opens.
    sp.plot(x, y, color="red", label="data")   # with style
    sp.scatter(x, y)                           # scatter plot
    sp.hist(data, bins=50)                     # histogram
    sp.plot3(x, y, z)                          # 3D line
    sp.live(lambda t: math.sin(t))             # live streaming

    sp.show()  # optional — block until windows closed

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
from ._axes import Axes
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

# ─── Easy API (one-liners, everything in background) ─────────────────────────
from ._easy import (
    plot,
    scatter,
    stem,
    hist,
    bar,
    boxplot,
    violin_plot,
    hline,
    vline,
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
    title,
    xlabel,
    ylabel,
    xlim,
    ylim,
    grid,
    legend,
    live,
    stop_live,
    append,
    show,
    close,
    close_all,
    clear,
)

# ─── Backward compatibility ───────────────────────────────────────────────────
# Old API used sp.line(x, y) — keep as alias for sp.plot(x, y)
line = plot

# Old tests reference these module-level attributes
_default_session = None
_current_figure = None
_current_axes = None

try:
    from importlib.metadata import version as _pkg_version
    __version__ = _pkg_version("spectra-plot")
except Exception:
    __version__ = "0.1.0"

__all__ = [
    # Easy API — one-liners
    "plot",
    "scatter",
    "stem",
    "hist",
    "bar",
    "boxplot",
    "violin_plot",
    "hline",
    "vline",
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
    "title",
    "xlabel",
    "ylabel",
    "xlim",
    "ylim",
    "grid",
    "legend",
    "live",
    "stop_live",
    "append",
    "show",
    "close",
    "close_all",
    "clear",
    # Backward compat
    "line",
    # Advanced API — full control
    "Session",
    "Figure",
    "Axes",
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
]
