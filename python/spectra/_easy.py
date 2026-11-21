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

    # Wait for the windows to close (optional: they stay open after the
    # script exits anyway, but live updates stop with the Python process)
    sp.show()

Everything runs in the background. Windows appear instantly.
No Session(), no figure(), no subplot(), no show() needed.
"""

from __future__ import annotations

import atexit
import math
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

from . import _colors
from ._protocol import DEFAULT_WINDOW_HEIGHT, DEFAULT_WINDOW_WIDTH

# Type alias for data that can be list, tuple, or numpy array
ArrayLike = Union[List[float], Tuple[float, ...], Sequence[float], Any]


def _is_dataframe(data: Any) -> bool:
    """True for pandas/polars-style 2-D tables (have columns and an index or schema)."""
    return hasattr(data, "columns") and (hasattr(data, "index") or hasattr(data, "schema")) \
        and hasattr(data, "__getitem__") and not isinstance(data, (list, tuple, dict))


def _numpy_to_array(np: Any, arr: Any) -> Any:
    """Flatten a NumPy array to float64; datetimes become Unix epoch seconds."""
    kind = arr.dtype.kind
    if kind == "M":  # datetime64 -> epoch seconds (NaT -> NaN)
        ns = arr.astype("datetime64[ns]")
        out = ns.astype(np.int64).astype(np.float64) / 1e9
        out[np.isnat(ns)] = np.nan
        return out.ravel()
    if kind == "m":  # timedelta64 -> seconds
        td = arr.astype("timedelta64[ns]")
        out = td.astype(np.int64).astype(np.float64) / 1e9
        out[np.isnat(td)] = np.nan
        return out.ravel()
    if kind == "O":
        # Object arrays (e.g. pandas nullable ints with pd.NA, Timestamps).
        return np.array([_scalar_to_float(v) for v in arr.ravel().tolist()], dtype=np.float64)
    return np.asarray(arr, dtype=np.float64).ravel()


def _numpy_to_list(np: Any, arr: Any) -> List[float]:
    """Flatten a NumPy array to floats; datetimes become Unix epoch seconds."""
    return _numpy_to_array(np, arr).tolist()


def _scalar_to_float(v: Any) -> float:
    if v is None:
        return float("nan")
    ts = getattr(v, "timestamp", None)
    if callable(ts):  # datetime / pandas.Timestamp
        try:
            return float(ts())
        except (ValueError, OverflowError):  # NaT
            return float("nan")
    try:
        return float(v)
    except (TypeError, ValueError):
        if str(v) in ("<NA>", "NaT", "nan"):
            return float("nan")
        raise


def _to_list(data: ArrayLike) -> List[float]:
    """Convert any array-like to list of floats.

    Accepts lists, tuples, generators, NumPy arrays, pandas Series/Index,
    polars Series and anything implementing ``__array__`` (xarray, CPU
    tensors).  Datetime values become Unix epoch seconds.
    """
    if isinstance(data, list):
        try:
            return [float(v) for v in data]
        except (TypeError, ValueError):
            return [_scalar_to_float(v) for v in data]
    if _is_dataframe(data):
        raise TypeError(
            "pass one column (df['name']) or call sp.plot(df) to plot every column"
        )
    try:
        import numpy as np
    except ImportError:
        np = None
    if np is not None:
        if isinstance(data, np.ndarray):
            return _numpy_to_list(np, data)
        to_numpy = getattr(data, "to_numpy", None)
        if callable(to_numpy):  # pandas Series/Index, polars Series
            try:
                return _numpy_to_list(np, np.asarray(to_numpy()))
            except (TypeError, ValueError):
                pass
        if hasattr(data, "__array__"):
            return _numpy_to_list(np, np.asarray(data))
    values = list(data)
    try:
        return [float(v) for v in values]
    except (TypeError, ValueError):
        return [_scalar_to_float(v) for v in values]


def _index_to_list(index: Any, n: int) -> List[float]:
    """x values from a pandas index: numeric or datetime as-is, else 0..n-1."""
    try:
        x = _to_list(index)
        if len(x) == n:
            return x
    except (TypeError, ValueError):
        pass
    return [float(i) for i in range(n)]


def _to_values(data: ArrayLike) -> Any:
    """Like :func:`_to_list` but returns a float64 NumPy array when NumPy is installed.

    Used on the plot()/scatter() hot path: large arrays stay vectorised all the
    way to the IPC payload instead of becoming millions of Python floats.
    Falls back to a list when NumPy is missing.
    """
    try:
        import numpy as np
    except ImportError:
        return _to_list(data)
    if _is_dataframe(data):
        raise TypeError(
            "pass one column (df['name']) or call sp.plot(df) to plot every column"
        )
    if isinstance(data, np.ndarray):
        return _numpy_to_array(np, data)
    to_numpy = getattr(data, "to_numpy", None)
    if callable(to_numpy):  # pandas Series/Index, polars Series
        try:
            return _numpy_to_array(np, np.asarray(to_numpy()))
        except (TypeError, ValueError):
            pass
    if hasattr(data, "__array__"):
        return _numpy_to_array(np, np.asarray(data))
    if isinstance(data, (list, tuple)):
        try:
            return np.asarray(data, dtype=np.float64).ravel()
        except (TypeError, ValueError):
            pass  # None, datetimes, pd.NA ... handled value by value below
    return np.asarray(_to_list(data), dtype=np.float64)


def _index_values(index: Any, n: int) -> Any:
    """x values from a pandas index (array form of :func:`_index_to_list`)."""
    try:
        x = _to_values(index)
        if len(x) == n:
            return x
    except (TypeError, ValueError):
        pass
    return _arange(n)


def _arange(n: int) -> Any:
    """0, 1, ..., n-1 as float64 (NumPy array when available)."""
    try:
        import numpy as np
    except ImportError:
        return [float(i) for i in range(n)]
    return np.arange(n, dtype=np.float64)


def _is_datetime_like(data: Any) -> bool:
    """True when ``data`` holds dates/times (converted to epoch seconds by _to_list)."""
    if data is None or isinstance(data, (str, bytes, int, float)):
        return False
    dtype = getattr(data, "dtype", None)
    if dtype is not None:
        return getattr(dtype, "kind", "") == "M"
    if isinstance(data, (list, tuple)):
        import datetime as _dt

        for v in data:
            if v is None:
                continue
            return isinstance(v, _dt.datetime) or type(v).__name__ in ("Timestamp", "datetime64")
    return False


def _xy_args_datetime(args) -> bool:
    """True when the x values that _parse_xy_args() derives from ``args`` are datetimes."""
    if len(args) >= 2:
        return _is_datetime_like(args[0])
    if len(args) == 1 and hasattr(args[0], "to_numpy"):
        index = getattr(args[0], "index", None)
        if index is not None and not callable(index):
            return _is_datetime_like(index)
    return False


def _mark_datetime_x(ax, is_datetime: bool) -> None:
    """Switch the axes to date-time x tick labels the first time datetime x data arrive."""
    if not is_datetime:
        return
    key = (getattr(ax, "_figure_id", 0), getattr(ax, "_index", 0))
    if key in _state._datetime_axes:
        return
    setter = getattr(ax, "set_xaxis_datetime", None)
    if setter is not None:
        setter(True)
    _state._datetime_axes.add(key)


def _parse_color(color: Union[str, Tuple, List, None]) -> Optional[Tuple[float, float, float, float]]:
    """Parse a colour (name, ``C<n>``, ``tab:*``, hex, CSS4, grey level or tuple) to RGBA.

    ``None`` stays ``None``; an unknown or malformed colour raises ValueError
    with a "did you mean" suggestion.  See spectra._colors for the full list.
    """
    return _colors.to_rgba(color)


# Named colours that map to a MATLAB colour letter.  These are sent as the
# letter so the window draws the shade tuned for its theme ('k' stays visible
# on a dark plot, 'y' on a light one) instead of a fixed RGB.
_LETTER_NAMES = {
    "r": "r", "red": "r",
    "g": "g", "green": "g",
    "b": "b", "blue": "b",
    "c": "c", "cyan": "c",
    "m": "m", "magenta": "m",
    "y": "y", "yellow": "y",
    "k": "k", "black": "k",
    "w": "w", "white": "w",
}


def _color_letter(color: Union[str, Tuple, List, None]) -> Optional[str]:
    """Return the theme-aware colour letter for a named colour, else None."""
    if isinstance(color, str):
        return _LETTER_NAMES.get(color.lower().strip())
    return None


def _edge_rgba(color: Union[str, Tuple, List, None]) -> Optional[Tuple[float, ...]]:
    """RGBA of an edge colour argument (None keeps the theme-aware default)."""
    if color is None:
        return None
    c = _parse_color(color)
    if not c:
        raise ValueError(f"unknown edge colour {color!r}")
    return tuple(c)


def _stat_style_kwargs(fill_alpha: Optional[float], edge_width: Optional[float],
                       edge_color: Union[str, Tuple, List, None]) -> dict:
    """Fill-style keywords for Axes.bar / Axes.histogram (only those given)."""
    out: dict = {}
    if fill_alpha is not None:
        out["fill_alpha"] = float(fill_alpha)
    if edge_width is not None:
        out["edge_width"] = float(edge_width)
    if edge_color is not None:
        out["edge_color"] = _edge_rgba(edge_color)
    return out


def _check_color(color: Any) -> None:
    """Raise ValueError for an invalid colour before anything is created."""
    if color is not None and _color_letter(color) is None:
        _parse_color(color)


def _apply_color(series: Any, color: Union[str, Tuple, List, None]) -> None:
    """Colour a series: named letter colours stay theme-aware, others are RGB."""
    if series is None or color is None:
        return
    letter = _color_letter(color)
    if letter is not None and hasattr(series, "set_color_letter"):
        series.set_color_letter(letter)
        return
    slot = _colors.palette_index(color)
    if slot is not None and hasattr(series, "set_palette_index"):
        # "C<n>": the window resolves palette slot n for its theme and palette.
        series.set_palette_index(slot)
        return
    c = _parse_color(color)
    if c:
        series.set_color(*c)


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
        # (fig, axes) -> [sx_lo, sx_hi, sy_lo, sy_hi]: sticky edges (bar baselines)
        self._axes_sticky: dict = {}
        self._errorbar_counts: dict = {}  # (figure_id, axes_index) -> errorbar calls
        self._axes_scales: dict = {}  # (figure_id, axes_index) -> [xscale, yscale]
        self._axes_bars: dict = {}  # (figure_id, axes_index) -> [(x, h, bottom, horiz, width)]
        self._datetime_axes: set = set()  # (figure_id, axes_index) with date-time x ticks
        self._subplot_cache: dict = {}  # (figure_id, rows, cols, index) -> Axes
        # sharex / sharey groups: (figure_id, axes_index) -> {"x": key, "y": key}
        # and key -> member Axes proxies (limits are fitted to the whole group).
        self._share_groups: dict = {}
        self._share_members: dict = {}
        self._pending_knobs: List[dict] = []
        self._interactive_bindings: List = []
        self._interactive_started = False
        self._knob_values: dict = {}

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
        if fig is None:
            return
        from . import _inline

        if _inline.is_inline():
            # Jupyter inline mode: no window; the figure is rendered as a PNG
            # under the cell (post_run_cell hook) or by show().
            _inline.track(fig)
            return
        if id(fig) in self._pending_show:
            self._flush_pending_knobs()
            self._pending_show.discard(id(fig))
            fig.show()
            _ensure_interactive_loop()

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

    def _flush_pending_knobs(self) -> None:
        """Push queued knob definitions to the backend before the window opens."""
        if not self._pending_knobs:
            return
        from . import _protocol as P
        from . import _codec as codec

        session = self._ensure_session()
        for spec in self._pending_knobs:
            payload = codec.encode_req_update_property(
                figure_id=spec["figure_id"],
                axes_index=0,
                series_index=0,
                prop="add_knob",
                f1=spec["value"],
                f2=spec["min"],
                f3=spec["max"],
                str_val=spec["name"],
            )
            try:
                session._request(P.REQ_UPDATE_PROPERTY, payload)
            except Exception:
                pass
            self._knob_values[spec["name"]] = spec["value"]
        self._pending_knobs.clear()

    def shutdown(self):
        """Clean shutdown — stop live threads, close session."""
        self._shutting_down = True
        for evt in self._live_stop_events:
            evt.set()
        for t in self._live_threads:
            t.join(timeout=2.0)
        self._live_threads.clear()
        self._live_stop_events.clear()
        self._pending_knobs.clear()
        self._interactive_bindings.clear()
        self._interactive_started = False
        from . import _inline

        _inline._settings.pending.clear()
        _inline._settings.cell_touched = False
        self._knob_values.clear()
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
        self._axes_sticky.clear()
        self._errorbar_counts.clear()
        self._axes_scales.clear()
        self._axes_bars.clear()
        self._datetime_axes.clear()
        self._subplot_cache.clear()
        self._share_groups.clear()
        self._share_members.clear()
        self._shutting_down = False


_state = _EasyState()
atexit.register(_state.shutdown)


def _detach_current_figure() -> None:
    """Make the next plot call start a new figure (inline mode, after each cell).

    The figure stays in the backend and its handle keeps working.
    """
    _state._current_fig = None
    _state._current_axes = None
    _state._current_axes3d = None
    _state._current_axes_key = (1, 1, 1)


# ─── Figure / Axes Management ────────────────────────────────────────────────

def figure(title: str = "", width: int = DEFAULT_WINDOW_WIDTH, height: int = DEFAULT_WINDOW_HEIGHT):
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

    from . import _inline

    if _inline.is_inline():
        _inline.track(fig)  # no windows (or tabs) inline: shown as its own image
    # If previous figure has a window_id, show new figure in that window (as tab)
    elif prev_fig is not None and prev_fig._window_id != 0:
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
    cache_key = (fig.id, rows, cols, index)
    cached = _state._subplot_cache.get(cache_key)
    if cached is not None:
        _state._current_axes_key = (rows, cols, index)
        _state._current_axes = cached
        _state._current_axes3d = None
        return cached
    _state._current_axes_key = (rows, cols, index)
    _state._current_axes = fig.subplot(rows, cols, index)
    _state._current_axes3d = None
    _state._subplot_cache[cache_key] = _state._current_axes
    return _state._current_axes


def subplot3d(rows: int = 1, cols: int = 1, index: int = 1):
    """Select a 3D subplot. Creates the figure if needed.

    Uses 1-based indexing: subplot3d(2, 1, 1) = top of 2-row layout.
    """
    fig = _state._ensure_figure()
    cache_key = (fig.id, rows, cols, index, '3d')
    cached = _state._subplot_cache.get(cache_key)
    if cached is not None:
        _state._current_axes3d = cached
        _state._current_axes = None
        return cached
    _state._current_axes3d = fig.subplot3d(rows, cols, index)
    _state._current_axes = None
    _state._subplot_cache[cache_key] = _state._current_axes3d
    return _state._current_axes3d


def gcf():
    """Get current figure."""
    return _state._ensure_figure()


def gca():
    """Get current axes."""
    return _state._ensure_axes()


def twinx(ax=None):
    """A second y axis on the right of ``ax`` (default: the current axes).

    The new axes shares the x axis (limits, pan and zoom) and plot area of
    ``ax`` and becomes the current axes, so the next ``plot()`` uses the
    right-hand y axis.  Its series join the legend of ``ax``.

    Usage::

        sp.plot(t, temperature, label="temperature")
        sp.ylabel("temperature (C)")
        sp.twinx()
        sp.plot(t, rainfall, "--", label="rainfall")
        sp.ylabel("rainfall (mm)")
    """
    host = ax if ax is not None else _state._ensure_axes()
    if getattr(host, "_twin_host", None) is not None:
        host = host._twin_host
    twin = host.twinx()
    _link_twin_x(host, twin)
    _state._current_axes = twin
    _state._current_axes3d = None
    _apply_fit(host)
    return twin


def _link_twin_x(host, twin) -> None:
    """Fit the x axis of a twin and its host (and the host's share group) together."""
    host_key = (host._figure_id, host._index)
    groups = _state._share_groups.setdefault(host_key, {})
    gk = groups.get("x")
    if gk is None:
        gk = ("twin", host._figure_id, host._index)
        groups["x"] = gk
        _state._share_members.setdefault(gk, []).append(host)
    members = _state._share_members.setdefault(gk, [])
    if not any(m is twin for m in members):
        members.append(twin)
    _state._share_groups.setdefault((twin._figure_id, twin._index), {})["x"] = gk


# ─── 2D Plotting ─────────────────────────────────────────────────────────────

def _split_fmt(args: tuple) -> Tuple[tuple, Optional[str]]:
    """Split a trailing MATLAB/matplotlib format string off plot()'s positional args."""
    if len(args) in (2, 3) and isinstance(args[-1], str):
        return args[:-1], args[-1]
    return args, None


def plot(
    *args,
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
    linestyle: Optional[str] = None,
    marker: Optional[str] = None,
    **kwargs,
):
    """Plot a line. The simplest possible API.

    Usage::

        sp.plot([1, 4, 9, 16])           # y only, x = 0,1,2,...
        sp.plot(x, y)                     # x and y
        sp.plot(x, y, "r--o")             # format string: red, dashed, circles
        sp.plot(y, "g^")                  # green triangles, no line
        sp.plot(x, y, color="red")        # with color
        sp.plot(x, y, linestyle=":", marker="s")
        sp.plot(x, y, label="sin(x)")     # with legend label
        sp.plot(x, y, width=2)            # line width

    Format strings follow MATLAB/matplotlib: ``[colour][marker][line]`` with
    colours ``b g r c m y k w``, markers ``. o + x * s d D ^ v < > p h`` and
    line styles ``- -- : -.``.  ``color=``, ``linestyle=`` (``ls=``) and
    ``marker=`` override the format string.  NumPy arrays are sent as one
    float32 block, so millions of points take well under a second.

    Returns the Series object for further customization.
    """
    from ._style import resolve_style

    args, fmt = _split_fmt(args)
    if linestyle is None and "ls" in kwargs:
        linestyle = kwargs.pop("ls")
    fmt_color, line_token, marker_token = resolve_style(fmt, linestyle, marker)
    if color is None:
        color = fmt_color
    _check_color(color)
    if len(args) == 1 and _is_dataframe(args[0]):
        return _plot_dataframe(args[0], color=color, width=width, label=label,
                               linestyle=line_token, marker=marker_token, **kwargs)
    x, y = _parse_xy_args(args, as_array=True)
    if not label and len(args) == 1:
        label = _series_name(args[0])
    ax = _state._ensure_axes()
    series = ax.line(x, y, label=label)
    _apply_series_style(series, color=color, width=width, linestyle=line_token,
                        marker=marker_token, **kwargs)
    _mark_datetime_x(ax, _xy_args_datetime(args))
    _auto_fit_axes(ax, x, y)
    _state._show_if_pending()
    return series


def step(
    *args,
    where: str = "pre",
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
    **kwargs,
):
    """Step plot, like ``matplotlib.pyplot.step``: the value changes in steps.

    ``where`` is ``"pre"`` (default: each y holds over the interval ending at
    its x), ``"post"`` (each y holds until the next x) or ``"mid"``.

    Usage::

        sp.step(t, mode, where="post", label="flight mode")
    """
    kwargs.pop("drawstyle", None)
    kwargs.pop("ds", None)
    return plot(*args, color=color, width=width, label=label, where=where, **kwargs)


def stairs(
    values,
    edges=None,
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
    **kwargs,
):
    """Stairs, like ``matplotlib.pyplot.stairs``: ``values[i]`` holds from
    ``edges[i]`` to ``edges[i + 1]`` (``edges`` defaults to ``0..len(values)``).

    Usage::

        counts = [3, 5, 2]
        sp.stairs(counts, [0.0, 1.0, 2.0, 3.0])
    """
    vals = _to_list(values)
    if not vals:
        raise ValueError("stairs() needs at least one value")
    e = list(range(len(vals) + 1)) if edges is None else _to_list(edges)
    if len(e) != len(vals) + 1:
        raise ValueError("stairs() needs len(edges) == len(values) + 1")
    x = [float(v) for v in e]
    y = [float(v) for v in vals] + [float(vals[-1])]
    return plot(x, y, color=color, width=width, label=label, drawstyle="steps-post", **kwargs)


def _series_name(data: Any) -> str:
    """The ``name`` of a pandas/polars Series, used as the default legend label."""
    if isinstance(data, (list, tuple)) or not hasattr(data, "to_numpy"):
        return ""
    name = getattr(data, "name", None)
    return "" if name is None else str(name)


def _dataframe_columns(df: Any) -> List[Any]:
    cols = list(df.columns)
    numeric = []
    for c in cols:
        try:
            _to_list(df[c])
        except (TypeError, ValueError):
            continue  # skip text / category columns
        numeric.append(c)
    return numeric


def _plot_dataframe(df: Any, color=None, width=None, label: str = "", **kwargs):
    """One line per numeric column against the index (pandas) or row number."""
    cols = _dataframe_columns(df)
    if not cols:
        raise ValueError("DataFrame has no numeric columns to plot")
    n = len(df)
    index = getattr(df, "index", None)
    x = _index_values(index, n) if index is not None else _arange(n)
    ax = _state._ensure_axes()
    _mark_datetime_x(ax, index is not None and _is_datetime_like(index))
    series_list = []
    for c in cols:
        y = _to_values(df[c])
        lbl = f"{label}{c}" if label else str(c)
        s = ax.line(x, y, label=lbl)
        _apply_series_style(s, color=color, width=width, **kwargs)
        _auto_fit_axes(ax, x, y)
        series_list.append(s)
    _state._show_if_pending()
    return series_list


def scatter(
    *args,
    color: Union[str, Tuple, List, None] = None,
    size: Optional[float] = None,
    label: str = "",
    marker: Optional[str] = None,
    c: Any = None,
    cmap: Any = None,
    vmin: Optional[float] = None,
    vmax: Optional[float] = None,
    **kwargs,
):
    """Plot scatter points.

    Usage::

        sp.scatter([1, 4, 9, 16])        # y only
        sp.scatter(x, y)                  # x and y
        sp.scatter(x, y, color="blue")    # with color
        sp.scatter(x, y, size=5)          # marker size
        sp.scatter(x, y, marker="s")      # marker shape (also "r^" format)
        sp.scatter(x, y, c=z, cmap="plasma")  # colour each point by value

    ``c`` gives one value per point, coloured through ``cmap`` (default
    "viridis"); ``vmin`` / ``vmax`` fix the values mapped onto its ends.

    Returns the Series object.
    """
    from ._style import parse_fmt

    if len(args) in (2, 3) and isinstance(args[-1], str):
        fcolor, fmarker, _ = parse_fmt(args[-1])
        args = args[:-1]
        color = fcolor if color is None else color
        marker = fmarker if marker is None else marker
    _check_color(color)
    if (vmin is None) != (vmax is None):
        raise ValueError("give both vmin and vmax, or neither")
    x, y = _parse_xy_args(args, as_array=True)
    values = None
    if c is not None:
        values = _to_list(c)
        if len(values) != len(x):
            raise ValueError(f"c has {len(values)} values but there are {len(x)} points")
    ax = _state._ensure_axes()
    series = ax.scatter(x, y, label=label)
    _apply_series_style(series, color=color, size=size, marker=marker, **kwargs)
    if values is not None:
        series.set_color_values(values)
        series.set_colormap("viridis" if cmap is None else cmap)
        if vmin is not None:
            series.set_colormap_range(vmin, vmax)
    _mark_datetime_x(ax, _xy_args_datetime(args))
    _auto_fit_axes(ax, x, y)
    _state._show_if_pending()
    return series


def stem(
    *args,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    baseline: float = 0.0,
    show_heads: bool = True,
    width: Optional[float] = None,
):
    """Stem plot: a vertical line from ``baseline`` to each point, with a head.

    Drawn by the native stem series (one legend entry, round heads).

    Usage::

        sp.stem(y)
        sp.stem(x, y, color="r", baseline=0.5)
    """
    _check_color(color)
    x, y = _parse_xy_args(args)
    ax = _state._ensure_axes()
    series = ax.stem(x, y, baseline=float(baseline),
                     stem_width=1.5 if width is None else float(width),
                     show_heads=show_heads, label=label)
    _apply_color(series, color)
    _mark_datetime_x(ax, _xy_args_datetime(args))
    _auto_fit_axes(ax, x, y + [float(baseline)], sticky_y=[float(baseline)])
    _state._show_if_pending()
    return series


_ERRORBAR_COLOR_CYCLE = ("b", "r", "g", "m", "c", "y")


def _expand_err(err: Any, n: int, name: str) -> Tuple[List[float], List[float]]:
    """Normalise an error spec to (lower, upper) lists of length n.

    Accepts a scalar, a length-n array (symmetric) or a 2 x n pair
    ``(lower, upper)`` like matplotlib.
    """
    if isinstance(err, (int, float)):
        v = abs(float(err))
        return [v] * n, [v] * n
    shape = getattr(err, "shape", None)
    if shape is not None and len(shape) == 2:
        if shape[0] != 2:
            raise ValueError(f"{name} must have shape (n,) or (2, n), got {tuple(shape)}")
        lower, upper = _to_list(err[0]), _to_list(err[1])
    elif isinstance(err, (list, tuple)) and len(err) == 2 \
            and all(hasattr(e, "__len__") for e in err):
        lower, upper = _to_list(err[0]), _to_list(err[1])
    else:
        lower = _to_list(err)
        upper = lower
    if len(lower) != n or len(upper) != n:
        raise ValueError(f"{name} has {len(lower)} values but the data has {n}")
    return [abs(v) for v in lower], [abs(v) for v in upper]


def errorbar(
    x: ArrayLike,
    y: ArrayLike,
    yerr: Any = None,
    xerr: Any = None,
    fmt: str = "-",
    color: Union[str, Tuple, List, None] = None,
    capsize: float = 0.0,
    label: str = "",
    width: Optional[float] = None,
    size: Optional[float] = None,
):
    """Line or markers with error bars, like ``matplotlib.pyplot.errorbar``.

    ``yerr`` / ``xerr`` may be a scalar, an array (symmetric) or a pair
    ``(lower, upper)``.  ``fmt`` is ``"-"`` (line, default), ``"o"`` or
    ``"."`` (markers) or ``"none"`` (error bars only).  ``capsize`` is the
    full cap width in data units (0 = no caps, the matplotlib default).

    Usage::

        sp.errorbar(x, y, yerr=0.1)
        sp.errorbar(x, y, yerr=(lo, hi), fmt="o", capsize=0.2)

    Returns ``(data_series, bar_series)``; ``data_series`` is ``None`` when
    ``fmt="none"``.
    """
    _check_color(color)
    xv = _to_list(x)
    yv = _to_list(y)
    if len(xv) != len(yv):
        raise ValueError(f"x has {len(xv)} values but y has {len(yv)}")
    if fmt not in ("-", "o", ".", "none", ""):
        raise ValueError("fmt must be '-', 'o', '.' or 'none'")
    n = len(xv)
    nan = float("nan")
    bx: List[float] = []
    by: List[float] = []
    half_cap = abs(float(capsize)) / 2.0
    if yerr is not None:
        lo, hi = _expand_err(yerr, n, "yerr")
        for xi, yi, l, h in zip(xv, yv, lo, hi):
            bx.extend([xi, xi, nan])
            by.extend([yi - l, yi + h, nan])
            if half_cap > 0:
                for yc in (yi - l, yi + h):
                    bx.extend([xi - half_cap, xi + half_cap, nan])
                    by.extend([yc, yc, nan])
    if xerr is not None:
        lo, hi = _expand_err(xerr, n, "xerr")
        for xi, yi, l, h in zip(xv, yv, lo, hi):
            bx.extend([xi - l, xi + h, nan])
            by.extend([yi, yi, nan])
            if half_cap > 0:
                for xc in (xi - l, xi + h):
                    bx.extend([xc, xc, nan])
                    by.extend([yi - half_cap, yi + half_cap, nan])

    ax = _state._ensure_axes()
    if color is None:
        # Bars and data share one colour; the IPC API cannot copy a palette
        # slot between series, so pick a theme-aware letter per call.
        key = (getattr(ax, "_figure_id", 0), getattr(ax, "_index", 0))
        count = _state._errorbar_counts.get(key, 0)
        _state._errorbar_counts[key] = count + 1
        color = _ERRORBAR_COLOR_CYCLE[count % len(_ERRORBAR_COLOR_CYCLE)]

    data_series = None
    if fmt == "-":
        data_series = ax.line(xv, yv, label=label)
        _apply_series_style(data_series, color=color, width=width)
    elif fmt in ("o", "."):
        data_series = ax.scatter(xv, yv, label=label)
        marker = size if size is not None else (6.0 if fmt == "o" else 3.0)
        _apply_series_style(data_series, color=color, size=marker)

    bar_series = None
    if bx:
        bar_series = ax.line(bx, by, label="" if data_series is not None else label)
        _apply_color(bar_series, color)
        if width is not None:
            bar_series.set_line_width(float(width))
    _mark_datetime_x(ax, _is_datetime_like(x))
    _auto_fit_axes(ax, xv + bx, yv + by)
    _state._show_if_pending()
    return data_series, bar_series


def _histogram_counts(values: List[float], bins: int, density: bool,
                      cumulative: bool) -> Tuple[float, float, List[float]]:
    """(lo, hi, per-bin heights) as the native HistogramSeries computes them."""
    lo, hi = min(values), max(values)
    if lo == hi:
        hi = lo + max(1.0, abs(lo) * 1e-6)
    bin_width = (hi - lo) / bins
    counts = [0.0] * bins
    for v in values:
        t = (v - lo) / bin_width
        idx = bins - 1 if not (t < bins) else (0 if t <= 0 else int(t))
        counts[min(idx, bins - 1)] += 1.0
    if cumulative:
        for i in range(1, bins):
            counts[i] += counts[i - 1]
    if density:
        divisor = len(values) if cumulative else len(values) * bin_width
        counts = [c / divisor for c in counts]
    return lo, hi, counts


def hist(
    data: ArrayLike,
    bins: int = 30,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    density: bool = False,
    cumulative: bool = False,
    fill_alpha: Optional[float] = None,
    edge_width: Optional[float] = None,
    edge_color: Union[str, Tuple, List, None] = None,
):
    """Histogram of ``data`` with ``bins`` equal-width bins over its range.

    Drawn by the native histogram series (flat filled bins with thin edges,
    legend swatch).  ``density=True`` normalises to a probability density;
    ``cumulative=True`` accumulates the bins (a CDF when combined with
    ``density``).  ``fill_alpha`` (default 0.88), ``edge_width`` (pixels,
    default 1) and ``edge_color`` (default: a shade of the fill) style the bins.

    Usage::

        sp.hist(data)
        sp.hist(data, bins=50, color="orange", density=True)
    """
    _check_color(color)
    if not isinstance(bins, int) or isinstance(bins, bool):
        raise TypeError("bins must be an int (number of equal-width bins)")
    if bins < 1:
        raise ValueError("bins must be >= 1")
    values = [v for v in _to_list(data) if math.isfinite(v)]
    if not values:
        return None
    lo, hi, counts = _histogram_counts(values, bins, density, cumulative)

    ax = _state._ensure_axes()
    series = ax.histogram(values, bins=bins, density=density, cumulative=cumulative,
                          label=label, **_stat_style_kwargs(fill_alpha, edge_width, edge_color))
    _apply_color(series, color)
    _mark_datetime_x(ax, _is_datetime_like(data))
    _auto_fit_axes(ax, [lo, hi], [0.0, max(counts)], sticky_y=[0.0])
    _state._show_if_pending()
    return series


def bar(
    x: ArrayLike,
    heights: ArrayLike,
    bar_width: float = 0.8,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    bottom: float = 0.0,
    horizontal: bool = False,
    width: Optional[float] = None,
    fill_alpha: Optional[float] = None,
    edge_width: Optional[float] = None,
    edge_color: Union[str, Tuple, List, None] = None,
    stacked: Optional[bool] = None,
):
    """Bar chart drawn by the native bar series (filled bars, legend swatch).

    ``x`` holds bar centres, or category names (strings): bar ``i`` then sits
    at x = i and the axis shows the names.  ``width`` is an alias of
    ``bar_width`` (matplotlib's name); ``bottom`` is the bars' baseline and
    ``horizontal=True`` draws bars along x (like ``barh``).  ``fill_alpha``
    (default 0.88), ``edge_width`` (pixels, default 1) and ``edge_color``
    (default: a shade of the fill) style the bars.

    Several bar series at the same positions are drawn side by side
    (grouped).  ``stacked=True`` stacks them on top of each other instead
    (negative values stack downwards); it applies to the whole axes.

    Usage::

        sp.bar([1, 2, 3], [10, 20, 15])
        sp.bar(["a", "b", "c"], [3, 5, 2], color="g")
        sp.bar(["a", "b", "c"], [1, 2, 1], stacked=True)
    """
    _check_color(color)
    bw = float(width) if width is not None else float(bar_width)
    names = None
    if not isinstance(x, str):
        items = list(x.tolist()) if hasattr(x, "tolist") else list(x)
        if items and all(isinstance(v, str) for v in items):
            names = [str(v) for v in items]
    xv = [float(i) for i in range(len(names))] if names is not None else _to_list(x)
    hv = _to_list(heights)
    if len(xv) != len(hv):
        raise ValueError(f"x has {len(xv)} values but heights has {len(hv)}")

    ax = _state._ensure_axes()
    series = ax.bar(names if names is not None else xv, hv, width=bw, bottom=float(bottom),
                    horizontal=horizontal, label=label, stacked=stacked,
                    **_stat_style_kwargs(fill_alpha, edge_width, edge_color))
    _apply_color(series, color)
    ax_key = (ax._figure_id, ax._index)
    _state._axes_bars.setdefault(ax_key, []).append(
        (xv, hv, float(bottom), bool(horizontal), bw))
    if not horizontal:
        _mark_datetime_x(ax, names is None and _is_datetime_like(x))
    _refit_bars(ax)
    _state._show_if_pending()
    return series


def _bar_value_extent(same, mode: str):
    """Values the bars of one orientation must show, and their baselines.

    Mirrors Axes::sync_bar_layout(): stacked bars (two or more series of one
    orientation) stack from the first series' bottom, positives and
    negatives separately; otherwise each bar spans bottom .. height.
    """
    if mode != "stacked" or len(same) < 2:
        ext = [v for b in same for v in b[1]] + [b[2] for b in same]
        return ext, sorted({b[2] for b in same})
    origin = same[0][2]
    tops: dict = {}
    ext = [origin]
    for xv, hv, *_ in same:
        for p, h in zip(xv, hv):
            if not (math.isfinite(p) and math.isfinite(h)):
                continue
            pos_top, neg_top = tops.get(p, (origin, origin))
            if h >= 0:
                pos_top += h
                ext.append(pos_top)
            else:
                neg_top += h
                ext.append(neg_top)
            tops[p] = (pos_top, neg_top)
    return ext, [origin]


def _refit_bars(ax) -> None:
    """Fit the axes limits to its bars (both orientations) in its bar mode.

    Called after each bar() and whenever the axes' bar mode changes, since
    the mode is axes-wide: switching to stacked grows the stacks of every
    orientation, not only the new bar's.  Limits only ever grow (bounds
    accumulate across calls).  Bar baselines become sticky edges.
    """
    ax_key = (ax._figure_id, ax._index)
    bars = _state._axes_bars.get(ax_key)
    if not bars:
        return
    mode = getattr(ax, "bar_mode", "grouped")
    for horizontal in (False, True):
        same = [b for b in bars if b[3] == horizontal]
        if not same:
            continue
        ext, origins = _bar_value_extent(same, mode)
        pos = [p + d * b[4] / 2.0 for b in same for p in b[0] for d in (-1.0, 1.0)]
        if horizontal:
            _auto_fit_axes(ax, ext, pos, sticky_x=origins)
        else:
            _auto_fit_axes(ax, pos, ext, sticky_y=origins)


def barh(
    y: ArrayLike,
    widths: ArrayLike,
    height: float = 0.8,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    left: float = 0.0,
    stacked: Optional[bool] = None,
    **style,
):
    """Horizontal bar chart (``bar(..., horizontal=True)``).

    ``style`` takes ``fill_alpha``, ``edge_width`` and ``edge_color`` as in
    :func:`bar`.
    """
    return bar(y, widths, bar_width=height, color=color, label=label, bottom=left,
               horizontal=True, stacked=stacked, **style)


def fill_between(
    x: ArrayLike,
    y1: ArrayLike,
    y2: Union[ArrayLike, float] = 0.0,
    color: Union[str, Tuple, List, None] = None,
    alpha: float = 0.25,
    label: str = "",
    edges: bool = True,
    edge_width: float = 1.0,
):
    """Fill the area between ``y1`` and ``y2`` (a scalar or an array) over ``x``.

    Drawn by the native band series: a translucent fill with optional edge
    lines.  ``alpha`` is the fill opacity.

    Usage::

        sp.fill_between(x, lower, upper, alpha=0.3)
        sp.fill_between(x, y)          # down to 0
    """
    _check_color(color)
    xv = _to_list(x)
    a = _to_list(y1)
    if isinstance(y2, (int, float)):
        b = [float(y2)] * len(xv)
    else:
        b = _to_list(y2)
    if not len(xv) == len(a) == len(b):
        raise ValueError(f"x, y1 and y2 must have the same length ({len(xv)}, {len(a)}, {len(b)})")
    lower = [min(p, q) for p, q in zip(a, b)]
    upper = [max(p, q) for p, q in zip(a, b)]
    ax = _state._ensure_axes()
    series = ax.band(xv, lower, upper, fill_opacity=float(alpha), edge_width=float(edge_width),
                     show_edges=edges, label=label)
    _apply_color(series, color)
    _mark_datetime_x(ax, _is_datetime_like(x))
    # A constant edge (fill down to a baseline) is sticky, like a bar baseline.
    sticky = [e[0] for e in (lower, upper) if e and all(v == e[0] for v in e)]
    _auto_fit_axes(ax, xv + xv, lower + upper, sticky_y=sticky)
    _state._show_if_pending()
    return series


def boxplot(
    datasets: List[ArrayLike],
    positions: Optional[List[float]] = None,
    widths: float = 0.6,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    show_outliers: bool = True,
):
    """Box-and-whisker plot from one or more datasets.

    Usage::

        sp.boxplot([data1, data2, data3])
        sp.boxplot([data1, data2], positions=[1, 3], color="blue")

    Each dataset is a list/array of values. Statistics (median, Q1, Q3,
    whiskers, outliers) are computed automatically.
    """
    _check_color(color)
    import math

    if positions is None:
        positions = [float(i + 1) for i in range(len(datasets))]

    hw = widths / 2.0
    all_x: List[float] = []
    all_y: List[float] = []
    out_x: List[float] = []
    out_y: List[float] = []

    for pos, data in zip(positions, datasets):
        values = sorted([float(v) for v in _to_list(data) if not math.isnan(float(v))])
        if not values:
            continue

        def _percentile(sorted_vals, p):
            idx = p * (len(sorted_vals) - 1)
            lo = int(math.floor(idx))
            hi = int(math.ceil(idx))
            if lo == hi:
                return sorted_vals[lo]
            frac = idx - lo
            return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac

        q1 = _percentile(values, 0.25)
        median = _percentile(values, 0.5)
        q3 = _percentile(values, 0.75)
        iqr = q3 - q1
        low_fence = q1 - 1.5 * iqr
        high_fence = q3 + 1.5 * iqr

        wlo = values[0]
        whi = values[-1]
        for v in values:
            if v >= low_fence:
                wlo = v
                break
        for v in reversed(values):
            if v <= high_fence:
                whi = v
                break

        nan = float("nan")
        cap_hw = hw * 0.5

        # Box
        all_x.extend([pos - hw, pos - hw, pos + hw, pos + hw, pos - hw, nan])
        all_y.extend([q1, q3, q3, q1, q1, nan])
        # Median
        all_x.extend([pos - hw, pos + hw, nan])
        all_y.extend([median, median, nan])
        # Lower whisker
        all_x.extend([pos, pos, nan])
        all_y.extend([q1, wlo, nan])
        all_x.extend([pos - cap_hw, pos + cap_hw, nan])
        all_y.extend([wlo, wlo, nan])
        # Upper whisker
        all_x.extend([pos, pos, nan])
        all_y.extend([q3, whi, nan])
        all_x.extend([pos - cap_hw, pos + cap_hw, nan])
        all_y.extend([whi, whi, nan])

        # Outliers
        if show_outliers:
            for v in values:
                if v < low_fence or v > high_fence:
                    out_x.append(pos)
                    out_y.append(v)

    ax = _state._ensure_axes()
    series = ax.line(all_x, all_y, label=label)
    _apply_color(series, color)
    series.set_line_width(2.0)

    if out_x:
        dots = ax.scatter(out_x, out_y, label="")
        _apply_color(dots, color)

    _auto_fit_axes(ax, all_x, all_y)
    if out_y:
        _auto_fit_axes(ax, out_x, out_y)
    _state._show_if_pending()
    return series


def violin_plot(
    datasets: List[ArrayLike],
    positions: Optional[List[float]] = None,
    widths: float = 0.8,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    show_box: bool = True,
    resolution: int = 50,
):
    """Violin plot from one or more datasets.

    Usage::

        sp.violin_plot([data1, data2, data3])
        sp.violin_plot([data1, data2], positions=[1, 3])

    Draws a mirrored kernel density estimate for each dataset.
    """
    _check_color(color)
    import math

    if positions is None:
        positions = [float(i + 1) for i in range(len(datasets))]

    hw = widths / 2.0
    all_x: List[float] = []
    all_y: List[float] = []

    for pos, data in zip(positions, datasets):
        values = sorted([float(v) for v in _to_list(data) if not math.isnan(float(v))])
        if not values:
            continue
        n = len(values)
        data_min, data_max = values[0], values[-1]
        data_range = data_max - data_min
        if data_range == 0:
            data_range = 1.0

        # Silverman's rule
        mean = sum(values) / n
        std_dev = math.sqrt(sum((v - mean) ** 2 for v in values) / n)
        if std_dev == 0:
            std_dev = 1.0
        bandwidth = 1.06 * std_dev * (n ** -0.2)

        def _kde(x):
            s = 0.0
            for d in values:
                z = (x - d) / bandwidth
                s += math.exp(-0.5 * z * z)
            return s * 0.3989422804014327 / (n * bandwidth)

        y_vals = [data_min + (i / (resolution - 1)) * data_range for i in range(resolution)]
        kde_vals = [_kde(y) for y in y_vals]
        max_kde = max(kde_vals) if kde_vals else 1.0
        if max_kde > 0:
            kde_vals = [k / max_kde for k in kde_vals]

        nan = float("nan")

        # Right half (going up)
        for i in range(resolution):
            all_x.append(pos + kde_vals[i] * hw)
            all_y.append(y_vals[i])
        # Left half (going down)
        for i in range(resolution - 1, -1, -1):
            all_x.append(pos - kde_vals[i] * hw)
            all_y.append(y_vals[i])
        # Close
        all_x.append(pos + kde_vals[0] * hw)
        all_y.append(y_vals[0])
        all_x.append(nan)
        all_y.append(nan)

        # Inner box
        if show_box:
            def _percentile(sorted_vals, p):
                idx = p * (len(sorted_vals) - 1)
                lo = int(math.floor(idx))
                hi = int(math.ceil(idx))
                if lo == hi:
                    return sorted_vals[lo]
                frac = idx - lo
                return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac

            q1 = _percentile(values, 0.25)
            median = _percentile(values, 0.5)
            q3 = _percentile(values, 0.75)
            bw = hw * 0.15
            all_x.extend([pos - bw, pos - bw, pos + bw, pos + bw, pos - bw, nan])
            all_y.extend([q1, q3, q3, q1, q1, nan])
            all_x.extend([pos - bw, pos + bw, nan])
            all_y.extend([median, median, nan])

    ax = _state._ensure_axes()
    series = ax.line(all_x, all_y, label=label)
    _apply_color(series, color)
    _auto_fit_axes(ax, all_x, all_y)
    _state._show_if_pending()
    return series


def hline(
    y: float,
    color: Union[str, Tuple, List, None] = "gray",
    label: str = "",
    *,
    interactive: bool = False,
    ymin: float = -10.0,
    ymax: float = 10.0,
    knob_name: str = "Y line",
):
    """Draw a horizontal reference line at y (e.g. ``hline(0)`` for the x-axis).

    Set ``interactive=True`` (or use ``ihline``) to drag a slider that moves the line.
    """
    _check_color(color)
    if interactive:
        return ihline(y, ymin, ymax, color=color, label=label, knob_name=knob_name)
    ax = _state._ensure_axes()
    series = ax.line([-1e12, 1e12], [y, y], label=label)
    _apply_color(series, color)
    series.set_line_width(1.0)
    return series


def vline(
    x: float,
    color: Union[str, Tuple, List, None] = "gray",
    label: str = "",
    *,
    interactive: bool = False,
    xmin: float = -10.0,
    xmax: float = 10.0,
    knob_name: str = "X line",
):
    """Draw a vertical reference line at x (e.g. ``vline(0)`` for the y-axis).

    Set ``interactive=True`` (or use ``ivline``) to drag a slider that moves the line.
    """
    _check_color(color)
    if interactive:
        return ivline(x, xmin, xmax, color=color, label=label, knob_name=knob_name)
    ax = _state._ensure_axes()
    series = ax.line([x, x], [-1e12, 1e12], label=label)
    _apply_color(series, color)
    series.set_line_width(1.0)
    return series


def fplot(
    func,
    xmin: Optional[float] = None,
    xmax: Optional[float] = None,
    n: int = 200,
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
    *,
    interactive: bool = False,
    params: Optional[List[Tuple[str, float, float, float]]] = None,
    **kwargs,
):
    """Plot y = func(x) over an x range.

    Usage::

        sp.fplot(lambda x: x ** 2, -2, 2)
        sp.fplot(math.sin)                    # uses current axes x-range or [-1, 1]
        sp.fplot(lambda x: x ** 2, color="red", label="parabola")
        sp.ifplot(lambda x, a, b: a * x * x + b, -3, 3, params=[("a", 0.2, -1, 1)])

  If *xmin* / *xmax* are omitted, uses accumulated axes bounds from prior plots
  on the same axes, otherwise ``[-1, 1]``.

  Set ``interactive=True`` with *params* ``[(name, value, min, max), ...]`` to
  expose sliders for each coefficient (or use ``ifplot``).
    """
    _check_color(color)
    if interactive:
        return ifplot(func, xmin, xmax, n=n, color=color, width=width, label=label,
                      params=params or [], **kwargs)

    ax = _state._ensure_axes()

    if xmin is None or xmax is None:
        ax_key = (ax._figure_id, ax._index)
        if ax_key in _state._axes_bounds:
            bounds = _state._axes_bounds[ax_key]
            if xmin is None:
                xmin = bounds[0]
            if xmax is None:
                xmax = bounds[1]
        else:
            if xmin is None:
                xmin = -1.0
            if xmax is None:
                xmax = 1.0

    if xmin == xmax:
        xmax = xmin + 1.0

    n = max(int(n), 2)
    step = (xmax - xmin) / (n - 1)
    xs = [xmin + i * step for i in range(n)]
    ys = [float(func(x)) for x in xs]

    series = ax.line(xs, ys, label=label)
    _apply_series_style(series, color=color, width=width, **kwargs)
    _auto_fit_axes(ax, xs, ys)
    _state._show_if_pending()
    return series


# ─── Interactive reference lines & function plots ─────────────────────────────

_REF_SPAN = 1e12


class _InteractiveHLine:
    __slots__ = ("series", "knob_name")

    def __init__(self, series, knob_name: str) -> None:
        self.series = series
        self.knob_name = knob_name

    def apply(self, knobs: dict) -> None:
        if self.knob_name not in knobs:
            return
        y = float(knobs[self.knob_name])
        self.series.set_data([-_REF_SPAN, _REF_SPAN], [y, y])


class _InteractiveVLine:
    __slots__ = ("series", "knob_name")

    def __init__(self, series, knob_name: str) -> None:
        self.series = series
        self.knob_name = knob_name

    def apply(self, knobs: dict) -> None:
        if self.knob_name not in knobs:
            return
        x = float(knobs[self.knob_name])
        self.series.set_data([x, x], [-_REF_SPAN, _REF_SPAN])


class _InteractiveFplot:
    __slots__ = ("series", "func", "xs", "param_names")

    def __init__(self, series, func, xs: List[float], param_names: List[str]) -> None:
        self.series = series
        self.func = func
        self.xs = xs
        self.param_names = param_names

    def apply(self, knobs: dict) -> None:
        pvals = [float(knobs.get(n, 0.0)) for n in self.param_names]
        ys = [float(self.func(x, *pvals)) for x in self.xs]
        self.series.set_data(self.xs, ys)


def _queue_knob(ax, name: str, value: float, vmin: float, vmax: float) -> None:
    spec = {
        "figure_id": ax._figure_id,
        "name": name,
        "value": float(value),
        "min": float(vmin),
        "max": float(vmax),
    }
    _state._knob_values[name] = float(value)
    fig = _state._current_fig
    if fig is not None and id(fig) not in _state._pending_show:
        from . import _protocol as P
        from . import _codec as codec

        session = _state._ensure_session()
        payload = codec.encode_req_update_property(
            figure_id=spec["figure_id"],
            axes_index=0,
            series_index=0,
            prop="add_knob",
            f1=spec["value"],
            f2=spec["min"],
            f3=spec["max"],
            str_val=spec["name"],
        )
        try:
            session._request(P.REQ_UPDATE_PROPERTY, payload)
        except Exception:
            _state._pending_knobs.append(spec)
    else:
        _state._pending_knobs.append(spec)


def _poll_knob_values(session) -> dict:
    from . import _protocol as P
    from . import _codec as codec

    try:
        resp = session._request(P.REQ_GET_SNAPSHOT, b"")
        payload = resp["payload"]
        if payload and payload[0] == P.PAYLOAD_FORMAT_FLATBUFFERS:
            from . import _codec_fb as fb_codec
            return fb_codec.decode_fb_snapshot_knobs(payload)
    except Exception:
        pass
    return dict(_state._knob_values)


def _ensure_interactive_loop() -> None:
    if _state._interactive_started or not _state._interactive_bindings:
        return
    _state._interactive_started = True
    from ._animation import ipc_sleep
    from ._log import log

    stop_event = threading.Event()
    _state._live_stop_events.append(stop_event)

    def _loop() -> None:
        session = _state._ensure_session()
        session._live_thread_count += 1
        try:
            while not stop_event.is_set():
                ipc_sleep(0.05, session)
                knobs = _poll_knob_values(session)
                _state._knob_values.update(knobs)
                for binding in _state._interactive_bindings:
                    try:
                        binding.apply(knobs)
                    except Exception as e:
                        log.warning("interactive update error: %s", e)
        finally:
            session._live_thread_count -= 1

    thread = threading.Thread(target=_loop, daemon=True, name="spectra-interactive")
    thread.start()
    _state._live_threads.append(thread)


def ihline(
    y: float,
    ymin: float = -10.0,
    ymax: float = 10.0,
    color: Union[str, Tuple, List, None] = "gray",
    label: str = "",
    knob_name: str = "Y line",
):
    """Interactive horizontal line — drag the PARAMETERS slider to move y."""
    series = hline(y, color=color, label=label)
    ax = _state._ensure_axes()
    _queue_knob(ax, knob_name, y, ymin, ymax)
    _state._interactive_bindings.append(_InteractiveHLine(series, knob_name))
    _state._show_if_pending()
    return series


def ivline(
    x: float,
    xmin: float = -10.0,
    xmax: float = 10.0,
    color: Union[str, Tuple, List, None] = "gray",
    label: str = "",
    knob_name: str = "X line",
):
    """Interactive vertical line — drag the PARAMETERS slider to move x."""
    series = vline(x, color=color, label=label)
    ax = _state._ensure_axes()
    _queue_knob(ax, knob_name, x, xmin, xmax)
    _state._interactive_bindings.append(_InteractiveVLine(series, knob_name))
    _state._show_if_pending()
    return series


def ifplot(
    func,
    xmin: Optional[float] = None,
    xmax: Optional[float] = None,
    n: int = 200,
    color: Union[str, Tuple, List, None] = None,
    width: Optional[float] = None,
    label: str = "",
    params: Optional[List[Tuple[str, float, float, float]]] = None,
    **kwargs,
):
    """Interactive function plot — sliders retune each entry in *params*.

    *func* signature: ``func(x, *param_values)`` matching *params* order::

        sp.ifplot(lambda x, a, b: a * x * x + b, -3, 3,
                  params=[("a", 0.2, -1, 1), ("b", 0, -2, 2)])
    """
    _check_color(color)
    param_specs = params or []
    ax = _state._ensure_axes()

    if xmin is None or xmax is None:
        ax_key = (ax._figure_id, ax._index)
        if ax_key in _state._axes_bounds:
            bounds = _state._axes_bounds[ax_key]
            xmin = xmin if xmin is not None else bounds[0]
            xmax = xmax if xmax is not None else bounds[1]
        else:
            xmin = xmin if xmin is not None else -1.0
            xmax = xmax if xmax is not None else 1.0
    if xmin == xmax:
        xmax = xmin + 1.0

    n = max(int(n), 2)
    step = (xmax - xmin) / (n - 1)
    xs = [xmin + i * step for i in range(n)]
    pvals = [p[1] for p in param_specs]
    ys = [float(func(x, *pvals)) for x in xs]

    series = ax.line(xs, ys, label=label)
    _apply_series_style(series, color=color, width=width, **kwargs)
    _auto_fit_axes(ax, xs, ys)

    param_names: List[str] = []
    for name, value, vmin, vmax in param_specs:
        _queue_knob(ax, name, value, vmin, vmax)
        param_names.append(name)

    _state._interactive_bindings.append(_InteractiveFplot(series, func, xs, param_names))
    _state._show_if_pending()
    return series


# ─── Auto-fit helper ─────────────────────────────────────────────────────────

def _fit_range(lo: float, hi: float, pos_lo: float, scale: str):
    """Padded (min, max) limits for data spanning [lo, hi] on a ``scale`` axis.

    Linear axes get 5% padding.  Log axes pad by 5% of the decades spanned and
    start at the smallest positive value (``pos_lo``); None when there is none.
    Sqrt axes never go below 0.
    """
    if scale in ("log", "log10", "log2"):
        if pos_lo is None or not hi > 0:
            return None
        lo = pos_lo
        span = math.log10(hi) - math.log10(lo)
        f = 10 ** (span * 0.05) if span > 0 else 2.0
        return lo / f, hi * f
    pad = (hi - lo) * 0.05 if hi != lo else 0.5
    lo_out, hi_out = lo - pad, hi + pad
    if scale == "sqrt":
        lo_out = max(0.0, lo_out)
    return lo_out, hi_out


def _shared_bounds(ax_key, axis: str):
    """(lo, hi, pos_lo) of ``axis`` over the share group of ``ax_key`` and its members.

    Returns (None, ()) when the axes is not shared on that axis.
    """
    gk = _state._share_groups.get(ax_key, {}).get(axis)
    if gk is None:
        return None, ()
    members = _state._share_members.get(gk, ())
    i0, i1, ip = (0, 1, 4) if axis == "x" else (2, 3, 5)
    lo = hi = pos = None
    for m in members:
        b = _state._axes_bounds.get((m._figure_id, m._index))
        if b is None:
            continue
        lo = b[i0] if lo is None else min(lo, b[i0])
        hi = b[i1] if hi is None else max(hi, b[i1])
        if b[ip] is not None:
            pos = b[ip] if pos is None else min(pos, b[ip])
    if lo is None:
        return None, members
    return (lo, hi, pos), members


def _shared_sticky(targets, i: int):
    """(lo, hi) of the sticky edges on one axis (i=0: x, 2: y) over ``targets``."""
    lo = hi = None
    for t in targets:
        st = _state._axes_sticky.get((t._figure_id, t._index))
        if st is None or st[i] is None:
            continue
        lo = st[i] if lo is None else min(lo, st[i])
        hi = st[i + 1] if hi is None else max(hi, st[i + 1])
    return lo, hi


def _stick(rng, lo: float, hi: float, s_lo, s_hi, scale: str):
    """Stop linear padding at a sticky edge (bar baseline) that bounds the data.

    Matches the native autoscale (src/core/axes.cpp): bars and histograms
    stand on their baseline instead of floating above it.
    """
    if rng is None or scale != "linear" or s_lo is None:
        return rng
    r_lo, r_hi = rng
    if lo >= s_lo and r_lo < s_lo:
        r_lo = s_lo
    # Data constant at the sticky value (e.g. all-zero bars): sticking both
    # sides would collapse the range, so only the lower side sticks.
    if hi <= s_hi and r_hi > s_hi and s_hi > r_lo:
        r_hi = s_hi
    return r_lo, r_hi


def _apply_fit(ax) -> None:
    """Set the axes limits from the accumulated data bounds and its scales.

    Shared axes (``subplots(sharex=...)``) fit the data of their whole group
    and the limits go to every member.
    """
    ax_key = (ax._figure_id, ax._index)
    b = _state._axes_bounds.get(ax_key)
    if b is None:
        return
    xscale, yscale = _state._axes_scales.get(ax_key, ("linear", "linear"))
    xb, x_targets = (b[0], b[1], b[4]), (ax,)
    yb, y_targets = (b[2], b[3], b[5]), (ax,)
    gx, mx = _shared_bounds(ax_key, "x")
    if gx is not None:
        xb, x_targets = gx, mx
    gy, my = _shared_bounds(ax_key, "y")
    if gy is not None:
        yb, y_targets = gy, my
    xr = _fit_range(xb[0], xb[1], xb[2], xscale)
    yr = _fit_range(yb[0], yb[1], yb[2], yscale)
    sx = _shared_sticky(x_targets, 0)
    sy = _shared_sticky(y_targets, 2)
    xr = _stick(xr, xb[0], xb[1], sx[0], sx[1], xscale)
    yr = _stick(yr, yb[0], yb[1], sy[0], sy[1], yscale)
    try:
        if xr is not None:
            for t in x_targets:
                t.set_xlim(*xr)
        if yr is not None:
            for t in y_targets:
                t.set_ylim(*yr)
    except Exception:
        pass


def _share_axes(fig, axes_list, cols: int, sharex=False, sharey=False) -> None:
    """Link the limits of a subplot grid like matplotlib's ``subplots(sharex=...)``.

    ``axes_list`` is the grid in row-major order.  Modes: True/"all", "row",
    "col", False/"none".  The windows link the axes too (pan / zoom one, all
    follow) and hide the inner tick labels.
    """
    from ._figure import share_mode

    modes = (("x", share_mode(sharex)), ("y", share_mode(sharey)))
    fig.set_layout(sharex=sharex, sharey=sharey)
    cols = max(1, int(cols))
    for axis, mode in modes:
        for i, ax in enumerate(axes_list):
            ax_key = (ax._figure_id, ax._index)
            groups = _state._share_groups.setdefault(ax_key, {})
            old = groups.pop(axis, None)
            if old is not None and ax in _state._share_members.get(old, []):
                _state._share_members[old].remove(ax)
            if mode == 0:
                continue
            r, c = divmod(i, cols)
            part = 0 if mode == 1 else (r if mode == 2 else c)
            gk = (id(fig), axis, mode, part)
            groups[axis] = gk
            _state._share_members.setdefault(gk, []).append(ax)
    for ax in axes_list:
        _apply_fit(ax)


def suptitle(text: str):
    """Set the title of the current figure, centred above all its subplots."""
    _state._ensure_figure().suptitle(text)


def tight_layout():
    """Size the subplot spacing from the labels (constrained layout, on by default)."""
    _state._ensure_figure().tight_layout()


def _finite_bounds(values: Any):
    """(min, max, smallest positive or None) of the finite values, or None.

    NaN and +/-inf are skipped; any finite magnitude counts (hline/vline do not
    feed their +/-1e12 spans into the fit).  Uses NumPy (vectorised) when installed.
    """
    try:
        import numpy as np
    except ImportError:
        np = None
    if np is not None:
        arr = np.asarray(values, dtype=np.float64).ravel()
        arr = arr[np.isfinite(arr)]
        if arr.size == 0:
            return None
        pos = arr[arr > 0]
        return float(arr.min()), float(arr.max()), (float(pos.min()) if pos.size else None)
    finite = [float(v) for v in values if math.isfinite(v)]
    if not finite:
        return None
    pos = [v for v in finite if v > 0]
    return min(finite), max(finite), (min(pos) if pos else None)


def _auto_fit_axes(ax, x: Any, y: Any,
                   sticky_x: Optional[List[float]] = None,
                   sticky_y: Optional[List[float]] = None) -> None:
    """Set axis limits to fit ALL series data with 5% padding, ignoring NaN values.

    Accumulates bounds across multiple plot() calls on the same axes so that
    earlier series are not pushed out of frame by later ones.  Log-scaled
    axes fit the positive data only.  ``sticky_x`` / ``sticky_y`` are sticky
    edges (bar baselines, which are part of the data): padding never grows the
    limits past one that is also the edge of all the data.
    """
    ax_key = (ax._figure_id, ax._index)
    if sticky_x or sticky_y:
        st = _state._axes_sticky.setdefault(ax_key, [None, None, None, None])
        for vals, i in ((sticky_x, 0), (sticky_y, 2)):
            vals = [float(v) for v in (vals or []) if math.isfinite(v)]
            if vals:
                st[i] = min(vals) if st[i] is None else min(st[i], min(vals))
                st[i + 1] = max(vals) if st[i + 1] is None else max(st[i + 1], max(vals))
    bx = _finite_bounds(x)
    by = _finite_bounds(y)
    if bx is None or by is None:
        return
    new = [bx[0], bx[1], by[0], by[1], bx[2], by[2]]

    # Accumulate with existing bounds for this axes
    prev = _state._axes_bounds.get(ax_key)
    if prev is not None:
        def _min_opt(a, b):
            return b if a is None else (a if b is None else min(a, b))

        new = [min(prev[0], new[0]), max(prev[1], new[1]), min(prev[2], new[2]),
               max(prev[3], new[3]), _min_opt(prev[4], new[4]), _min_opt(prev[5], new[5])]
    _state._axes_bounds[ax_key] = new
    _apply_fit(ax)


def _auto_fit_axes3d(ax, x: List[float], y: List[float], z: List[float]) -> None:
    """Set 3D axis limits to fit data with 5% padding, ignoring NaN values."""
    def _fit(vals):
        b = _finite_bounds(vals)
        if b is None:
            return None, None
        lo, hi = b[0], b[1]
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
    depthshade: Optional[bool] = None,
):
    """3D line plot — rendered with GPU-accelerated Axes3D.

    ``depthshade=True`` fades the far parts of the line towards the background.

    Usage::

        sp.plot3(x, y, z)
        sp.plot3(x, y, z, color="red", label="helix", depthshade=True)
    """
    _check_color(color)
    xv = _to_list(x)
    yv = _to_list(y)
    zv = _to_list(z)
    ax = _state._ensure_axes3d()
    series = ax._add_series_3d("line3d", xv, yv, zv, label=label)
    _apply_series_style(series, color=color, width=width)
    if depthshade is not None:
        series.set_depth_shade(depthshade)
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
    depthshade: Optional[bool] = None,
):
    """3D scatter plot — rendered with GPU-accelerated Axes3D.

    Points fade with depth by default (matplotlib's ``depthshade``);
    ``depthshade=False`` draws them all in full colour.

    Usage::

        sp.scatter3(x, y, z)
        sp.scatter3(x, y, z, color="blue", size=3, depthshade=False)
    """
    _check_color(color)
    xv = _to_list(x)
    yv = _to_list(y)
    zv = _to_list(z)
    ax = _state._ensure_axes3d()
    series = ax._add_series_3d("scatter3d", xv, yv, zv, label=label)
    _apply_series_style(series, color=color, size=size)
    if depthshade is not None:
        series.set_depth_shade(depthshade)
    _auto_fit_axes3d(ax, xv, yv, zv)
    _state._show_if_pending()
    return series


def surf(
    x: ArrayLike,
    y: ArrayLike,
    z: ArrayLike,
    color: Union[str, Tuple, List, None] = None,
    label: str = "",
    edges: bool = False,
    contours: Union[bool, int] = False,
    cmap: Any = None,
    vmin: Optional[float] = None,
    vmax: Optional[float] = None,
):
    """Surface plot — rendered with GPU-accelerated Axes3D.

    ``edges=True`` draws the grid lines over the faces; ``contours=True`` (or
    a level count) projects contour lines onto the floor of the box, like
    MATLAB's ``surfc``.  ``cmap`` colours the faces by height through a
    colormap (a name such as "viridis" or "RdBu_r", or a list of colours);
    ``vmin`` / ``vmax`` fix the heights mapped onto its ends.

    Usage::

        X, Y = np.meshgrid(np.linspace(-3, 3, 50), np.linspace(-3, 3, 50))
        Z = np.sin(X) * np.cos(Y)
        sp.surf(X, Y, Z)
        sp.surf(X, Y, Z, edges=True, contours=12)
        sp.surf(X, Y, Z, cmap="plasma", vmin=-1, vmax=1)
    """
    _check_color(color)
    if (vmin is None) != (vmax is None):
        raise ValueError("give both vmin and vmax, or neither")
    xv = _to_list(x)
    yv = _to_list(y)
    zv = _to_list(z)
    ax = _state._ensure_axes3d()
    series = ax._add_series_3d("surface", xv, yv, zv, label=label or "surface")
    _apply_color(series, color)
    if edges:
        series.set_show_edges(True)
    if contours is not False and contours is not None:
        series.set_contour_projection(True, 10 if contours is True else int(contours))
    if cmap is not None:
        series.set_colormap(cmap)
    if vmin is not None:
        series.set_colormap_range(vmin, vmax)
    _auto_fit_axes3d(ax, xv, yv, zv)
    _state._show_if_pending()
    return series


# ─── Axes Configuration ──────────────────────────────────────────────────────

def _current_axes_any():
    """Return the current axes (2D or 3D), preferring whichever was last set."""
    if _state._current_axes3d is not None:
        return _state._current_axes3d
    return _state._ensure_axes()


def xticks(ticks=None, labels=None):
    """Fixed x ticks of the current axes at ``ticks`` with optional
    ``labels``; ``[]`` hides them and ``None`` restores automatic ticks."""
    _state._ensure_axes().set_xticks(ticks, labels)


def yticks(ticks=None, labels=None):
    """Fixed y ticks of the current axes; see :func:`xticks`."""
    _state._ensure_axes().set_yticks(ticks, labels)


def colorbar(visible: bool = True):
    """Show (or hide) a colorbar for the current axes' colormapped series,
    e.g. after ``scatter(x, y, c=z)`` or ``surf(..., cmap=...)``."""
    _current_axes_any().colorbar(visible)


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


def xscale(scale: str):
    """Set the x axis scale of the current axes: "linear", "log", "log2" or "sqrt"."""
    _set_scale(0, scale)


def yscale(scale: str):
    """Set the y axis scale of the current axes: "linear", "log", "log2" or "sqrt"."""
    _set_scale(1, scale)


def _set_scale(which: int, scale: str) -> None:
    from ._axes import scale_to_wire

    scale_to_wire(scale)  # validate before touching the backend
    ax = _state._ensure_axes()
    (ax.set_xscale if which == 0 else ax.set_yscale)(scale)
    key = (getattr(ax, "_figure_id", 0), getattr(ax, "_index", 0))
    scales = list(_state._axes_scales.get(key, ("linear", "linear")))
    scales[which] = str(scale).lower()
    _state._axes_scales[key] = tuple(scales)
    _apply_fit(ax)  # e.g. log axes must not start at 0


def semilogx(*args, **kwargs):
    """``plot()`` with a log10 x axis."""
    series = plot(*args, **kwargs)
    xscale("log")
    return series


def semilogy(*args, **kwargs):
    """``plot()`` with a log10 y axis."""
    series = plot(*args, **kwargs)
    yscale("log")
    return series


def loglog(*args, **kwargs):
    """``plot()`` with log10 x and y axes."""
    series = plot(*args, **kwargs)
    xscale("log")
    yscale("log")
    return series


def savefig(path, width: int = 0, height: int = 0, format: Optional[str] = None,
            theme: str = "", dpi=None):
    """Save the current figure to a PNG, SVG or PDF file; returns the absolute path.

    The backend renders it offscreen (works before or without a window).
    ``width`` / ``height`` set the PNG size in pixels (0 = figure size); the
    format follows the extension unless ``format`` is given.

    Usage::

        sp.plot(x, y)
        sp.savefig("plot.png")
        sp.savefig("plot.svg")
        sp.savefig("big.png", width=3000, height=1800)
    """
    fig = _state._current_fig
    if fig is None:
        raise RuntimeError("no figure to save: plot something first")
    _state._flush_pending_knobs()
    return fig.savefig(path, width=width, height=height, format=format, theme=theme, dpi=dpi)


def legend(visible: bool = True):
    """Toggle legend visibility."""
    _current_axes_any().legend(visible)


def text(
    x: float,
    y: float,
    s: str,
    color: Union[str, Tuple, List, None] = None,
    fontsize: float = 0.0,
    ha: str = "left",
    va: str = "bottom",
    bbox=None,
    halo=None,
    rotation: float = 0.0,
):
    """Place text at data coordinates (x, y) on the current 2D axes.

    ``bbox=True`` draws a background box (theme colours) so the text stays
    readable over data; a dict sets ``pad`` (pixels), ``facecolor``,
    ``edgecolor`` and ``linewidth``.  ``halo=True`` (or a colour) outlines the
    glyphs instead.  ``rotation`` is in degrees, counter-clockwise.

    Usage::

        sp.text(2.0, 0.5, "peak", color="red", fontsize=16)
        sp.text(2.0, 0.5, "peak", bbox=True, rotation=30)
    """
    ax = _state._ensure_axes()
    ann = ax.text(x, y, s, color=_parse_color(color), fontsize=fontsize, ha=ha, va=va,
                  bbox=bbox, halo=halo, rotation=rotation)
    _state._show_if_pending()
    return ann


def annotate(
    s: str,
    xy: Tuple[float, float],
    xytext: Optional[Tuple[float, float]] = None,
    color: Union[str, Tuple, List, None] = None,
    fontsize: float = 0.0,
    bbox=None,
    halo=None,
    rotation: float = 0.0,
):
    """Label the point ``xy``; with ``xytext`` the text sits there with an arrow to ``xy``.

    ``bbox``, ``halo`` and ``rotation`` are as in :func:`text`.

    Usage::

        sp.annotate("local max", xy=(3.1, 0.9), xytext=(4.0, 1.2), bbox=True)
    """
    ax = _state._ensure_axes()
    ann = ax.annotate(s, xy, xytext, color=_parse_color(color), fontsize=fontsize, bbox=bbox,
                      halo=halo, rotation=rotation)
    _state._show_if_pending()
    return ann


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

    from . import _inline

    if _inline.is_inline():
        import warnings

        warnings.warn(
            "sp.live() streams into a window, but figures are shown as inline images in "
            "Jupyter; call sp.use_window() first (or use spectra.notebook for an "
            "interactive canvas)",
            UserWarning,
            stacklevel=2,
        )
    fig_obj = _state._ensure_figure()
    ax = _state._ensure_axes()
    _state._show_if_pending()

    # Notify the backend that this figure is live-streaming at the
    # requested FPS so the render loop runs continuously.
    from . import _protocol as P
    from . import _codec as codec

    session = _state._ensure_session()
    try:
        payload = codec.encode_req_anim_start(fig_obj._id, fps=fps, duration=duration)
        session._request(P.REQ_ANIM_START, payload)
    except Exception:
        pass  # Best-effort; Python-side pacing still works as fallback

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

    Optional. Windows are drawn by separate Spectra processes, so without
    ``show()`` they stay open after your script (and the Python process)
    exits, until you close them. What stops with the Python process is
    everything Python drives: :func:`live` callbacks, streaming appends and
    other updates. Call ``show()`` at the end of a script that keeps the plot
    changing, or that should wait for the user before it continues.

    In Jupyter inline mode (see :func:`use_inline`) it displays the pending
    figures as images under the cell and returns immediately.
    """
    from . import _inline

    # Flush any pending figure shows before blocking
    _state._show_if_pending()
    if _inline.is_inline():
        _inline.flush()
        return
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
    from . import _inline

    _inline.forget(fig)
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
    """Clear the current axes (remove all series).

    The axes forgets the data extent it fitted its limits to, so the next
    plot fits its own data only.
    """
    ax = _state._ensure_axes()
    ax.clear()
    _forget_axes_extent(ax)


def _forget_axes_extent(ax) -> None:
    ax_key = (ax._figure_id, ax._index)
    for d in (_state._axes_bounds, _state._axes_sticky, _state._errorbar_counts,
              _state._axes_bars):
        d.pop(ax_key, None)


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


def subplots(rows: int, cols: int, sharex=False, sharey=False):
    """Create a grid of subplots and return axes list.

    ``sharex`` / ``sharey`` link the axis limits like matplotlib: True/"all",
    "row", "col" or False.

    Usage::

        axes = sp.subplots(2, 2, sharex="col")
        # axes[0], axes[1], axes[2], axes[3]
    """
    fig = _state._ensure_figure()
    axes_list = []
    for i in range(1, rows * cols + 1):
        ax = fig.subplot(rows, cols, i)
        axes_list.append(ax)
    if sharex or sharey:
        _share_axes(fig, axes_list, cols, sharex, sharey)
    _state._current_axes = axes_list[0] if axes_list else None
    _state._current_axes_key = (rows, cols, 1)
    return axes_list


# ─── Internal Helpers ─────────────────────────────────────────────────────────

def _parse_xy_args(args, as_array: bool = False) -> Tuple[Any, Any]:
    """Parse positional args into (x, y) lists.

    Supports:
        (y,)        → x = [0, 1, 2, ...], y = y
        (x, y)      → x, y as given

    With ``as_array`` the values come back as float64 NumPy arrays when NumPy
    is installed (lists otherwise).
    """
    conv = _to_values if as_array else _to_list
    if len(args) == 0:
        return [], []
    elif len(args) == 1:
        y = conv(args[0])
        index = getattr(args[0], "index", None)
        if index is not None and not callable(index) and hasattr(args[0], "to_numpy"):
            # pandas Series: plot against its index
            x = _index_values(index, len(y)) if as_array else _index_to_list(index, len(y))
        else:
            x = _arange(len(y)) if as_array else [float(i) for i in range(len(y))]
        return x, y
    else:
        x = conv(args[0])
        y = conv(args[1])
        return x, y


def _apply_series_style(
    series,
    color=None,
    width=None,
    size=None,
    opacity=None,
    linestyle=None,
    marker=None,
    **kwargs,
):
    """Apply optional styling to a series.

    Line series also take ``drawstyle`` (alias ``ds``: ``"default"``,
    ``"steps-pre"``/``"steps"``, ``"steps-mid"``, ``"steps-post"``), ``where``
    (``"pre"``/``"mid"``/``"post"``, as in matplotlib's ``step``) and
    ``dense`` (``"auto"``, ``"exact"`` or ``"density"``).

    ``linestyle`` / ``marker`` are backend tokens or names (see _style.py);
    ``ls`` is accepted as an alias of ``linestyle``.
    """
    if linestyle is None and "ls" in kwargs:
        linestyle = kwargs.pop("ls")
    _apply_color(series, color)
    if linestyle is not None:
        series.set_line_style(linestyle)
    if marker is not None:
        series.set_marker_style(marker)
    if width is not None:
        series.set_line_width(float(width))
    if size is not None:
        series.set_marker_size(float(size))
    if opacity is not None:
        series.set_opacity(float(opacity))
    drawstyle = kwargs.pop("drawstyle", None) or kwargs.pop("ds", None)
    where = kwargs.pop("where", None)
    if drawstyle is None and where is not None:
        drawstyle = f"steps-{where}"
    dense = kwargs.pop("dense", None)
    if (drawstyle is not None or dense is not None) and getattr(series, "_type", "") == "line":
        from ._series import parse_dense_mode, parse_drawstyle

        if drawstyle is not None:
            series._draw_style = parse_drawstyle(drawstyle)
        if dense is not None:
            series._dense_mode = parse_dense_mode(dense)
        series._send_draw_style()
