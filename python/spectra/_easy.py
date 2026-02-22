"""Ultra-simple plotting API — one line to plot, everything in background.

Usage::

    import spectra as sp

    # One-liner: just pass data
    sp.plot([1, 2, 3, 4])
    sp.plot([0, 1, 2, 3], [1, 4, 9, 16])
    sp.scatter(x, y)

    # Styling with chaining
    sp.plot(x, y, color="red", width=2, label="my data")

    # Titles and labels
    sp.title("My Plot")
    sp.xlabel("X axis")
    sp.ylabel("Y axis")

    # Subplots
    sp.subplot(2, 1, 1)
    sp.plot(x, y1)
    sp.subplot(2, 1, 2)
    sp.plot(x, y2)

    # New window
    sp.figure()
    sp.plot(x, y3)

    # Live streaming
    sp.live(lambda t, dt: sp.append(math.sin(t)), fps=30)

    # 3D
    sp.plot3(x, y, z)
    sp.scatter3(x, y, z)

    # Block at end of script (optional — windows stay open anyway)
    sp.show()

Everything runs in the background. Windows appear instantly.
No Session(), no figure(), no subplot(), no show() needed.
"""

from __future__ import annotations

import atexit
import threading
from typing import (
    Any,
    Callable,
    List,
    Optional,
    Sequence,
    Tuple,
    Union,
)

# Type alias for data that can be list, tuple, or numpy array
ArrayLike = Union[List[float], Tuple[float, ...], Sequence[float], Any]


def _to_list(data: ArrayLike) -> List[float]:
    """Convert any array-like to list of floats."""
    if isinstance(data, list):
        return [float(v) for v in data]
    try:
        import numpy as np
        if isinstance(data, np.ndarray):
            return data.ravel().astype(np.float64).tolist()
    except ImportError:
        pass
    return [float(v) for v in data]


def _parse_color(color: Union[str, Tuple, List, None]) -> Optional[Tuple[float, float, float, float]]:
    """Parse color from name, hex, or tuple."""
    if color is None:
        return None
    if isinstance(color, (tuple, list)):
        if len(color) == 3:
            return (float(color[0]), float(color[1]), float(color[2]), 1.0)
        elif len(color) == 4:
            return (float(color[0]), float(color[1]), float(color[2]), float(color[3]))
    if isinstance(color, str):
        _NAMED = {
            "red": (1.0, 0.2, 0.2, 1.0),
            "r": (1.0, 0.2, 0.2, 1.0),
            "green": (0.2, 0.8, 0.2, 1.0),
            "g": (0.2, 0.8, 0.2, 1.0),
            "blue": (0.2, 0.4, 1.0, 1.0),
            "b": (0.2, 0.4, 1.0, 1.0),
            "yellow": (1.0, 0.9, 0.1, 1.0),
            "y": (1.0, 0.9, 0.1, 1.0),
            "cyan": (0.0, 0.9, 0.9, 1.0),
            "c": (0.0, 0.9, 0.9, 1.0),
            "magenta": (0.9, 0.1, 0.9, 1.0),
            "m": (0.9, 0.1, 0.9, 1.0),
            "white": (1.0, 1.0, 1.0, 1.0),
            "w": (1.0, 1.0, 1.0, 1.0),
            "black": (0.0, 0.0, 0.0, 1.0),
            "k": (0.0, 0.0, 0.0, 1.0),
            "orange": (1.0, 0.5, 0.0, 1.0),
            "purple": (0.6, 0.2, 0.9, 1.0),
            "pink": (1.0, 0.4, 0.7, 1.0),
            "gray": (0.5, 0.5, 0.5, 1.0),
            "grey": (0.5, 0.5, 0.5, 1.0),
        }
        lower = color.lower().strip()
        if lower in _NAMED:
            return _NAMED[lower]
        # Hex color
        if lower.startswith("#") and len(lower) in (7, 9):
            r = int(lower[1:3], 16) / 255.0
            g = int(lower[3:5], 16) / 255.0
            b = int(lower[5:7], 16) / 255.0
            a = int(lower[7:9], 16) / 255.0 if len(lower) == 9 else 1.0
            return (r, g, b, a)
    return None


# ─── Global State ─────────────────────────────────────────────────────────────

class _EasyState:
    """Singleton managing the background session and current figure/axes."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._session = None  # type: Optional[Session]
        self._current_fig = None
        self._current_axes = None
        self._current_axes3d = None
        self._current_axes_key = (1, 1, 1)  # (rows, cols, index)
        self._figures: List = []
        self._pending_show: set = set()  # figure ids needing show()
        self._live_threads: List[threading.Thread] = []
        self._live_stop_events: List[threading.Event] = []
        self._shutting_down = False
        self._axes_bounds: dict = {}  # id(axes) -> [xmin, xmax, ymin, ymax]

    def _ensure_session(self):
        """Lazily create the backend session."""
        if self._session is None:
            from ._session import Session
            self._session = Session()
        return self._session

    def _ensure_figure(self):
        """Lazily create a figure (show is deferred until data is ready)."""
        if self._current_fig is None:
            s = self._ensure_session()
            self._current_fig = s.figure()
            self._current_axes = None
            self._current_axes_key = (1, 1, 1)
            self._figures.append(self._current_fig)
            self._pending_show.add(id(self._current_fig))
        return self._current_fig

    def _show_if_pending(self):
        """Show the current figure if it hasn't been shown yet."""
        fig = self._current_fig
        if fig is not None and id(fig) in self._pending_show:
            self._pending_show.discard(id(fig))
            fig.show()

    def _ensure_axes(self):
        """Lazily create axes on the current figure."""
        if self._current_axes is None:
            fig = self._ensure_figure()
            r, c, i = self._current_axes_key
            self._current_axes = fig.subplot(r, c, i)
        return self._current_axes

    def _ensure_axes3d(self):
        """Lazily create a 3D axes on the current figure."""
        if self._current_axes3d is None:
            fig = self._ensure_figure()
            self._current_axes3d = fig.subplot3d(1, 1, 1)
        return self._current_axes3d

    def shutdown(self):
        """Clean shutdown — stop live threads, close session."""
        self._shutting_down = True
        for evt in self._live_stop_events:
            evt.set()
        for t in self._live_threads:
            t.join(timeout=2.0)
        self._live_threads.clear()
        self._live_stop_events.clear()
        if self._session is not None:
            try:
                self._session.close()
            except Exception:
                pass
            self._session = None
        self._current_fig = None
        self._current_axes = None
        self._current_axes3d = None
        self._figures.clear()
        self._axes_bounds.clear()


_state = _EasyState()
atexit.register(_state.shutdown)


# ─── Figure / Axes Management ────────────────────────────────────────────────

def figure(title: str = "", width: int = 1280, height: int = 720):
    """Create a new figure window. All subsequent plots go here.

    Returns the Figure object for advanced use.
    """
    # Show any pending previous figure before creating a new one
    _state._show_if_pending()
    s = _state._ensure_session()
    fig = s.figure(title=title, width=width, height=height)
    _state._current_fig = fig
    _state._current_axes = None
    _state._current_axes3d = None
    _state._current_axes_key = (1, 1, 1)
    _state._figures.append(fig)
    _state._pending_show.add(id(fig))
    return fig


def tab(title: str = ""):
    """Create a new tab in the current window. All subsequent plots go here.

    Unlike ``figure()``, which creates a new OS window, ``tab()`` adds a new
    figure as a tab inside the same window.

    Usage::

        sp.plot(x, y1)           # first tab (auto-created)
        sp.tab()                 # new tab in same window
        sp.plot(x, y2)           # plots in second tab
        sp.tab("Analysis")       # named tab
        sp.plot(x, y3)

    Returns the new Figure object.
    """
    # Need at least one figure shown to have a window to add tabs to
    prev_fig = _state._current_fig
    _state._show_if_pending()

    s = _state._ensure_session()
    fig = s.figure(title=title)

    # If previous figure has a window_id, show new figure in that window (as tab)
    if prev_fig is not None and prev_fig._window_id != 0:
        fig.show(window_id=prev_fig._window_id)
    else:
        fig.show()

    _state._current_fig = fig
    _state._current_axes = None
    _state._current_axes3d = None
    _state._current_axes_key = (1, 1, 1)
    _state._figures.append(fig)
    # Already shown — don't add to pending
    return fig


def subplot(rows: int, cols: int, index: int):
    """Select a subplot. Creates the figure if needed.

    Uses 1-based indexing: subplot(2, 1, 1) = top of 2-row layout.
    """
    fig = _state._ensure_figure()
    _state._current_axes_key = (rows, cols, index)
    _state._current_axes = fig.subplot(rows, cols, index)
    _state._current_axes3d = None
    return _state._current_axes


def subplot3d(rows: int = 1, cols: int = 1, index: int = 1):
    """Select a 3D subplot. Creates the figure if needed.

    Uses 1-based indexing: subplot3d(2, 1, 1) = top of 2-row layout.
    """
    fig = _state._ensure_figure()
    _state._current_axes3d = fig.subplot3d(rows, cols, index)
    _state._current_axes = None
    return _state._current_axes3d


def gcf():
    """Get current figure."""
    return _state._ensure_figure()


def gca():
    """Get current axes."""
    return _state._ensure_axes()


# ─── 2D Plotting ─────────────────────────────────────────────────────────────

def plot(
    *args,
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
    **kwargs,
):
    """Plot a line. The simplest possible API.

    Usage::

        sp.plot([1, 4, 9, 16])           # y only, x = 0,1,2,...
        sp.plot(x, y)                     # x and y
        sp.plot(x, y, color="red")        # with color
        sp.plot(x, y, label="sin(x)")     # with legend label
        sp.plot(x, y, width=2)            # line width

    Returns the Series object for further customization.
    """
    x, y = _parse_xy_args(args)
    ax = _state._ensure_axes()
    series = ax.line(x, y, label=label)
    _apply_series_style(series, color=color, width=width, **kwargs)
    _auto_fit_axes(ax, x, y)
    _state._show_if_pending()
    return series


def scatter(
    *args,
    color: Union[str, Tuple, List, None] = None,
    size: Optional[float] = None,
    label: str = "",
    **kwargs,
):
    """Plot scatter points.

    Usage::

        sp.scatter([1, 4, 9, 16])        # y only
        sp.scatter(x, y)                  # x and y
        sp.scatter(x, y, color="blue")    # with color
        sp.scatter(x, y, size=5)          # marker size

    Returns the Series object.
    """
    x, y = _parse_xy_args(args)
    ax = _state._ensure_axes()
    series = ax.scatter(x, y, label=label)
    _apply_series_style(series, color=color, size=size, **kwargs)
    _auto_fit_axes(ax, x, y)
    _state._show_if_pending()
    return series


def stem(
    *args,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
):
    """Stem plot (vertical lines from baseline to data points).

    Implemented as scatter + thin lines to baseline.
    """
    x, y = _parse_xy_args(args)
    ax = _state._ensure_axes()
    # Draw vertical lines as a line series with NaN gaps
    stem_x = []
    stem_y = []
    for xi, yi in zip(x, y):
        stem_x.extend([xi, xi, float("nan")])
        stem_y.extend([0.0, yi, float("nan")])
    line = ax.line(stem_x, stem_y, label=label)
    dots = ax.scatter(x, y, label="")
    c = _parse_color(color)
    if c:
        line.set_color(*c)
        dots.set_color(*c)
    _state._show_if_pending()
    return line


def hist(
    data: ArrayLike,
    bins: int = 30,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
):
    """Histogram. Computes bins from data and plots as a step line.

    Usage::

        sp.hist(data)
        sp.hist(data, bins=50, color="orange")
    """
    values = _to_list(data)
    if not values:
        return None
    lo = min(values)
    hi = max(values)
    if lo == hi:
        hi = lo + 1.0
    bin_width = (hi - lo) / bins
    counts = [0] * bins
    for v in values:
        idx = min(int((v - lo) / bin_width), bins - 1)
        counts[idx] += 1

    # Build step-function x/y
    step_x = []
    step_y = []
    for i in range(bins):
        edge_l = lo + i * bin_width
        edge_r = edge_l + bin_width
        step_x.extend([edge_l, edge_r])
        step_y.extend([counts[i], counts[i]])

    ax = _state._ensure_axes()
    series = ax.line(step_x, step_y, label=label)
    c = _parse_color(color)
    if c:
        series.set_color(*c)
    _state._show_if_pending()
    return series


def bar(
    x: ArrayLike,
    heights: ArrayLike,
    bar_width: float = 0.8,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
):
    """Bar chart. Draws rectangles as line segments.

    Usage::

        sp.bar([1, 2, 3], [10, 20, 15])
    """
    xv = _to_list(x)
    hv = _to_list(heights)
    hw = bar_width / 2.0
    bx = []
    by = []
    for xi, hi in zip(xv, hv):
        # Rectangle: 5 points + NaN separator
        bx.extend([xi - hw, xi - hw, xi + hw, xi + hw, xi - hw, float("nan")])
        by.extend([0.0, hi, hi, 0.0, 0.0, float("nan")])

    ax = _state._ensure_axes()
    series = ax.line(bx, by, label=label)
    c = _parse_color(color)
    if c:
        series.set_color(*c)
    series.set_line_width(2.0)
    _state._show_if_pending()
    return series


def hline(y: float, color: Union[str, Tuple, List, None] = "gray", label: str = ""):
    """Draw a horizontal line across the plot."""
    ax = _state._ensure_axes()
    series = ax.line([-1e12, 1e12], [y, y], label=label)
    c = _parse_color(color)
    if c:
        series.set_color(*c)
    series.set_line_width(1.0)
    return series


def vline(x: float, color: Union[str, Tuple, List, None] = "gray", label: str = ""):
    """Draw a vertical line across the plot."""
    ax = _state._ensure_axes()
    series = ax.line([x, x], [-1e12, 1e12], label=label)
    c = _parse_color(color)
    if c:
        series.set_color(*c)
    series.set_line_width(1.0)
    return series


# ─── Auto-fit helper ─────────────────────────────────────────────────────────

def _auto_fit_axes(ax, x: List[float], y: List[float]) -> None:
    """Set axis limits to fit ALL series data with 5% padding, ignoring NaN values.

    Accumulates bounds across multiple plot() calls on the same axes so that
    earlier series are not pushed out of frame by later ones.
    """
    finite_x = [v for v in x if v == v and abs(v) < 1e11]  # filter NaN and sentinel hlines/vlines
    finite_y = [v for v in y if v == v and abs(v) < 1e11]
    if not finite_x or not finite_y:
        return
    new_xmin, new_xmax = min(finite_x), max(finite_x)
    new_ymin, new_ymax = min(finite_y), max(finite_y)

    # Accumulate with existing bounds for this axes
    ax_key = id(ax)
    if ax_key in _state._axes_bounds:
        prev = _state._axes_bounds[ax_key]
        xmin = min(prev[0], new_xmin)
        xmax = max(prev[1], new_xmax)
        ymin = min(prev[2], new_ymin)
        ymax = max(prev[3], new_ymax)
    else:
        xmin, xmax = new_xmin, new_xmax
        ymin, ymax = new_ymin, new_ymax
    _state._axes_bounds[ax_key] = [xmin, xmax, ymin, ymax]

    # Add 5% padding
    xpad = (xmax - xmin) * 0.05 if xmax != xmin else 0.5
    ypad = (ymax - ymin) * 0.05 if ymax != ymin else 0.5
    try:
        ax.set_xlim(xmin - xpad, xmax + xpad)
        ax.set_ylim(ymin - ypad, ymax + ypad)
    except Exception:
        pass


def _auto_fit_axes3d(ax, x: List[float], y: List[float], z: List[float]) -> None:
    """Set 3D axis limits to fit data with 5% padding, ignoring NaN values."""
    def _fit(vals):
        finite = [v for v in vals if v == v and abs(v) < 1e11]
        if not finite:
            return None, None
        lo, hi = min(finite), max(finite)
        pad = (hi - lo) * 0.05 if hi != lo else 0.5
        return lo - pad, hi + pad

    xl, xh = _fit(x)
    yl, yh = _fit(y)
    zl, zh = _fit(z)
    try:
        if xl is not None:
            ax.set_xlim(xl, xh)
        if yl is not None:
            ax.set_ylim(yl, yh)
        if zl is not None:
            ax.set_zlim(zl, zh)
    except Exception:
        pass


# ─── 3D Plotting ─────────────────────────────────────────────────────────────

def plot3(
    x: ArrayLike,
    y: ArrayLike,
    z: ArrayLike,
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
):
    """3D line plot — rendered with GPU-accelerated Axes3D.

    Usage::

        sp.plot3(x, y, z)
        sp.plot3(x, y, z, color="red", label="helix")
    """
    xv = _to_list(x)
    yv = _to_list(y)
    zv = _to_list(z)
    ax = _state._ensure_axes3d()
    series = ax._add_series_3d("line3d", xv, yv, zv, label=label)
    _apply_series_style(series, color=color, width=width)
    _auto_fit_axes3d(ax, xv, yv, zv)
    _state._show_if_pending()
    return series


def scatter3(
    x: ArrayLike,
    y: ArrayLike,
    z: ArrayLike,
    color: Union[str, Tuple, List, None] = None,
    size: Optional[float] = None,
    label: str = "",
):
    """3D scatter plot — rendered with GPU-accelerated Axes3D.

    Usage::

        sp.scatter3(x, y, z)
        sp.scatter3(x, y, z, color="blue", size=3)
    """
    xv = _to_list(x)
    yv = _to_list(y)
    zv = _to_list(z)
    ax = _state._ensure_axes3d()
    series = ax._add_series_3d("scatter3d", xv, yv, zv, label=label)
    _apply_series_style(series, color=color, size=size)
    _auto_fit_axes3d(ax, xv, yv, zv)
    _state._show_if_pending()
    return series


def surf(
    x: ArrayLike,
    y: ArrayLike,
    z: ArrayLike,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
):
    """Surface plot — rendered with GPU-accelerated Axes3D.

    Usage::

        X, Y = np.meshgrid(np.linspace(-3, 3, 50), np.linspace(-3, 3, 50))
        Z = np.sin(X) * np.cos(Y)
        sp.surf(X, Y, Z)
    """
    xv = _to_list(x)
    yv = _to_list(y)
    zv = _to_list(z)
    ax = _state._ensure_axes3d()
    series = ax._add_series_3d("surface", xv, yv, zv, label=label or "surface")
    c = _parse_color(color)
    if c:
        series.set_color(*c)
    _auto_fit_axes3d(ax, xv, yv, zv)
    _state._show_if_pending()
    return series


# ─── Axes Configuration ──────────────────────────────────────────────────────

def _current_axes_any():
    """Return the current axes (2D or 3D), preferring whichever was last set."""
    if _state._current_axes3d is not None:
        return _state._current_axes3d
    return _state._ensure_axes()


def title(text: str):
    """Set the title of the current axes."""
    _current_axes_any().set_title(text)


def xlabel(text: str):
    """Set the x-axis label."""
    _current_axes_any().set_xlabel(text)


def ylabel(text: str):
    """Set the y-axis label."""
    _current_axes_any().set_ylabel(text)


def xlim(xmin: float, xmax: float):
    """Set x-axis limits."""
    _current_axes_any().set_xlim(xmin, xmax)


def ylim(ymin: float, ymax: float):
    """Set y-axis limits."""
    _current_axes_any().set_ylim(ymin, ymax)


def grid(visible: bool = True):
    """Toggle grid visibility."""
    _current_axes_any().grid(visible)


def legend(visible: bool = True):
    """Toggle legend visibility."""
    _current_axes_any().legend(visible)


# ─── Live Streaming / Animation ──────────────────────────────────────────────

def live(
    callback: Callable,
    fps: float = 30.0,
    duration: float = 0.0,
    title: str = "Live",
):
    """Start a live-updating plot. Callback runs in background.

    The callback signature can be:
        callback(t)           — receives elapsed time
        callback(t, dt)       — receives time and delta
        callback(t, dt, ax)   — receives time, delta, and axes for appending

    Usage::

        # Simplest: update a global series
        line = sp.plot([], [])
        sp.live(lambda t: line.set_data([t], [math.sin(t)]))

        # With axes access for appending
        def update(t, dt, ax):
            ax.series[0].append([t], [math.sin(t)])
        sp.live(update, fps=60)

        # Auto-creates a streaming plot
        sp.live(lambda t: math.sin(t), fps=30, title="Sine Wave")

    If callback returns a single number, it's auto-appended to a line series.
    """
    import inspect

    fig_obj = _state._ensure_figure()
    ax = _state._ensure_axes()
    _state._show_if_pending()

    # Detect callback signature
    sig = inspect.signature(callback)
    nparams = len(sig.parameters)

    # Check if callback returns a value (auto-append mode)
    # We'll detect this on first call
    auto_series = None
    auto_t_data = []
    auto_y_data = []
    window_size = int(fps * 10)  # 10 seconds of visible data

    stop_event = threading.Event()
    _state._live_stop_events.append(stop_event)

    def _loop():
        nonlocal auto_series, auto_t_data, auto_y_data
        from ._animation import FramePacer
        from ._log import log

        pacer = FramePacer(fps=fps)
        t = 0.0
        dt = 1.0 / fps
        session = _state._ensure_session()

        log.info("live thread running (count=%d, fps=%.0f)",
                 session._live_thread_count, fps)
        try:
            while not stop_event.is_set():
                if _state._shutting_down:
                    log.debug("live thread: shutting down")
                    break
                if not fig_obj._visible:
                    log.debug("live thread: figure no longer visible")
                    break

                if duration > 0 and t >= duration:
                    log.debug("live thread: duration reached")
                    break

                try:
                    # Call with appropriate number of args
                    if nparams >= 3:
                        result = callback(t, dt, ax)
                    elif nparams >= 2:
                        result = callback(t, dt)
                    else:
                        result = callback(t)

                    # Auto-append mode: if callback returns a number
                    if result is not None and isinstance(result, (int, float)):
                        if auto_series is None:
                            auto_series = ax.line([], [], label=title if title != "Live" else "")
                            try:
                                ax.set_ylim(-2.0, 2.0)
                            except Exception:
                                pass
                        auto_t_data.append(t)
                        auto_y_data.append(float(result))
                        # Sliding window
                        if len(auto_t_data) > window_size:
                            auto_t_data = auto_t_data[-window_size:]
                            auto_y_data = auto_y_data[-window_size:]
                        auto_series.set_data(auto_t_data, auto_y_data)
                        if len(auto_t_data) > 1:
                            try:
                                ax.set_xlim(auto_t_data[0], auto_t_data[-1])
                                ymin = min(auto_y_data)
                                ymax = max(auto_y_data)
                                margin = max(abs(ymax - ymin) * 0.1, 0.1)
                                ax.set_ylim(ymin - margin, ymax + margin)
                            except Exception:
                                pass

                    # Auto-append mode: if callback returns a tuple/list of numbers
                    elif result is not None and isinstance(result, (tuple, list)):
                        if len(result) == 2:
                            xv, yv = result
                            if auto_series is None:
                                auto_series = ax.line([], [], label=title if title != "Live" else "")
                            if isinstance(xv, (int, float)):
                                auto_t_data.append(float(xv))
                                auto_y_data.append(float(yv))
                            else:
                                auto_t_data.extend(_to_list(xv))
                                auto_y_data.extend(_to_list(yv))
                            if len(auto_t_data) > window_size:
                                auto_t_data = auto_t_data[-window_size:]
                                auto_y_data = auto_y_data[-window_size:]
                            auto_series.set_data(auto_t_data, auto_y_data)
                            if len(auto_t_data) > 1:
                                try:
                                    ax.set_xlim(auto_t_data[0], auto_t_data[-1])
                                    ymin = min(auto_y_data)
                                    ymax = max(auto_y_data)
                                    margin = max(abs(ymax - ymin) * 0.1, 0.1)
                                    ax.set_ylim(ymin - margin, ymax + margin)
                                except Exception:
                                    pass

                except Exception as e:
                    log.warning("live callback error: %s", e)

                t += dt
                pacer.pace(session)
        finally:
            session._live_thread_count -= 1
            log.info("live thread stopped (count=%d)", session._live_thread_count)

    # Increment before starting thread so show() sees it immediately
    session = _state._ensure_session()
    session._live_thread_count += 1

    thread = threading.Thread(target=_loop, daemon=True, name="spectra-live")
    thread.start()
    _state._live_threads.append(thread)
    return stop_event


def stop_live():
    """Stop all live streaming threads."""
    for evt in _state._live_stop_events:
        evt.set()


# ─── Convenience: append to existing series ──────────────────────────────────

def append(series, x_or_y, y=None):
    """Append data to an existing series.

    Usage::

        line = sp.plot([], [])
        sp.append(line, [1.0], [2.0])    # append x, y
        sp.append(line, 2.0)             # append single y (x auto-incremented)
    """
    if y is None:
        # Single value mode — auto-increment x
        if isinstance(x_or_y, (int, float)):
            series.append([0.0], [float(x_or_y)])
        else:
            yv = _to_list(x_or_y)
            xv = list(range(len(yv)))
            series.append(xv, yv)
    else:
        if isinstance(x_or_y, (int, float)):
            series.append([float(x_or_y)], [float(y)])
        else:
            series.append(_to_list(x_or_y), _to_list(y))


# ─── Show / Close ────────────────────────────────────────────────────────────

def show():
    """Block until all windows are closed.

    Call this at the end of your script to keep windows open.
    If you don't call it, windows stay open until the Python process exits.
    """
    # Flush any pending figure shows before blocking
    _state._show_if_pending()
    if _state._session is not None:
        _state._session.show()


def close():
    """Close the current figure's window.

    The figure is removed from the backend model. If there are other
    figures open, the most recent one becomes current.

    Usage::

        sp.plot(x, y1)
        sp.figure()
        sp.plot(x, y2)
        sp.close()       # closes the second window, first stays open
    """
    fig = _state._current_fig
    if fig is None:
        return
    try:
        fig.close()
    except Exception:
        pass
    # Remove from tracking
    if fig in _state._figures:
        _state._figures.remove(fig)
    _state._pending_show.discard(id(fig))
    # Switch current to the most recent remaining figure, or None
    if _state._figures:
        _state._current_fig = _state._figures[-1]
    else:
        _state._current_fig = None
    _state._current_axes = None
    _state._current_axes3d = None
    _state._current_axes_key = (1, 1, 1)


def close_all():
    """Close all windows and clean up the session.

    Stops live threads, closes every figure, and disconnects from the
    backend. After this call you can still create new plots — the
    session will be lazily re-created.

    Usage::

        sp.plot(x, y1)
        sp.figure()
        sp.plot(x, y2)
        sp.close_all()   # everything gone
    """
    _state.shutdown()


def clear():
    """Clear the current axes (remove all series)."""
    ax = _state._ensure_axes()
    ax.clear()


# ─── Multi-plot helpers ──────────────────────────────────────────────────────

def plotn(*ys, labels: Optional[List[str]] = None):
    """Plot multiple y-arrays on the same axes.

    Usage::

        sp.plotn(y1, y2, y3)
        sp.plotn(y1, y2, labels=["sin", "cos"])
    """
    series_list = []
    for i, y_data in enumerate(ys):
        lbl = labels[i] if labels and i < len(labels) else ""
        s = plot(y_data, label=lbl)
        series_list.append(s)
    return series_list


def subplots(rows: int, cols: int):
    """Create a grid of subplots and return axes list.

    Usage::

        axes = sp.subplots(2, 2)
        # axes[0], axes[1], axes[2], axes[3]
    """
    fig = _state._ensure_figure()
    axes_list = []
    for i in range(1, rows * cols + 1):
        ax = fig.subplot(rows, cols, i)
        axes_list.append(ax)
    _state._current_axes = axes_list[0] if axes_list else None
    _state._current_axes_key = (rows, cols, 1)
    return axes_list


# ─── Internal Helpers ─────────────────────────────────────────────────────────

def _parse_xy_args(args) -> Tuple[List[float], List[float]]:
    """Parse positional args into (x, y) lists.

    Supports:
        (y,)        → x = [0, 1, 2, ...], y = y
        (x, y)      → x, y as given
    """
    if len(args) == 0:
        return [], []
    elif len(args) == 1:
        y = _to_list(args[0])
        x = [float(i) for i in range(len(y))]
        return x, y
    else:
        x = _to_list(args[0])
        y = _to_list(args[1])
        return x, y


def _apply_series_style(
    series,
    color=None,
    width=None,
    size=None,
    opacity=None,
    **kwargs,
):
    """Apply optional styling to a series."""
    c = _parse_color(color)
    if c:
        series.set_color(*c)
    if width is not None:
        series.set_line_width(float(width))
    if size is not None:
        series.set_marker_size(float(size))
    if opacity is not None:
        series.set_opacity(float(opacity))
