"""matplotlib.pyplot-style front end for Spectra.

Lets common matplotlib scripts run on Spectra by changing one import::

    import spectra.pyplot as plt

    fig, axs = plt.subplots(2, 1, figsize=(8, 6))
    axs[0].plot(x, y, "r--", label="signal")
    axs[0].legend()
    axs[1].hist(data, bins=40)
    plt.tight_layout()
    plt.savefig("out.png")
    plt.show()

Covered: plot, scatter, bar, barh, hist, errorbar, fill_between, stem,
semilogx/semilogy/loglog, figure, subplot, subplots, xlabel/ylabel/title,
legend, xlim/ylim, xscale/yscale, grid, savefig, show, close, gcf/gca and the
matching ``Axes`` methods.  Keyword arguments Spectra cannot honour (line
styles, markers on lines, ``bbox_inches`` ...) are ignored with one
``UserWarning`` per function and argument.  ``tight_layout`` is a no-op: the
layout is always computed automatically.
"""

from __future__ import annotations

import math
import warnings
from typing import Any, Dict, List, Optional, Sequence, Tuple

from . import _easy as _sp

__all__ = [
    "plot", "scatter", "bar", "barh", "hist", "errorbar", "fill_between", "stem",
    "semilogx", "semilogy", "loglog", "figure", "subplot", "subplots", "xlabel",
    "ylabel", "title", "suptitle", "legend", "xlim", "ylim", "xscale", "yscale",
    "grid", "savefig", "show", "close", "gcf", "gca", "tight_layout",
]

# Pixels per inch for figsize (matplotlib's default dpi).
_DEFAULT_DPI = 100.0

_warned: set = set()


def _ignore(func: str, kwargs: Dict[str, Any]) -> None:
    """Warn once per (function, keyword) that the keyword has no effect."""
    for key in kwargs:
        if (func, key) in _warned:
            continue
        _warned.add((func, key))
        warnings.warn(f"spectra.pyplot.{func}: keyword {key!r} is not supported and was ignored",
                      UserWarning, stacklevel=3)


def _pop_alias(kwargs: Dict[str, Any], *names: str, default=None):
    """Pop the first of ``names`` present in ``kwargs`` (all of them are removed)."""
    value = default
    found = False
    for n in names:
        if n in kwargs:
            v = kwargs.pop(n)
            if not found:
                value, found = v, True
    return value


# ─── Format strings ───────────────────────────────────────────────────────────

_COLOR_CHARS = "bgrcmykw"
_MARKER_CHARS = ".,ov^<>1234sp*hH+xXDd|_"
_LINESTYLES = ("--", "-.", "-", ":")


def _parse_fmt(fmt: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """matplotlib fmt "[color][marker][line]" -> (color letter, marker, linestyle)."""
    color = marker = line = None
    rest = fmt
    while rest:
        for ls in _LINESTYLES:
            if rest.startswith(ls):
                line, rest = ls, rest[len(ls):]
                break
        else:
            ch, rest = rest[0], rest[1:]
            if ch in _COLOR_CHARS and color is None:
                color = ch
            elif ch in _MARKER_CHARS and marker is None:
                marker = ch
            else:
                raise ValueError(f"unrecognised format string {fmt!r}")
    return color, marker, line


def _is_fmt(v: Any) -> bool:
    return isinstance(v, str)


def _split_plot_args(args: Sequence[Any]) -> List[Tuple[Any, Any, str]]:
    """plot() positional args -> [(x or None, y, fmt), ...]."""
    groups = []
    args = list(args)
    while args:
        if len(args) >= 3 and _is_fmt(args[2]):
            groups.append((args[0], args[1], args[2]))
            args = args[3:]
        elif len(args) >= 2 and _is_fmt(args[1]):
            groups.append((None, args[0], args[1]))
            args = args[2:]
        elif len(args) >= 2:
            groups.append((args[0], args[1], ""))
            args = args[2:]
        else:
            groups.append((None, args[0], ""))
            args = args[1:]
    return groups


# ─── Axes wrapper ─────────────────────────────────────────────────────────────

class Axes:
    """matplotlib-like wrapper around a Spectra axes (see :func:`subplots`)."""

    def __init__(self, figure: "Figure", axes: Any, key: Tuple[int, int, int]) -> None:
        self.figure = figure
        self._ax = axes
        self._key = key

    def _activate(self) -> None:
        st = _sp._state
        st._current_fig = self.figure._fig
        st._current_axes = self._ax
        st._current_axes3d = None
        st._current_axes_key = self._key

    def __getattr__(self, name: str):
        fn = _AXES_METHODS.get(name)
        if fn is None:
            raise AttributeError(f"spectra.pyplot.Axes has no attribute {name!r}")

        def call(*args, **kwargs):
            self._activate()
            return fn(*args, **kwargs)

        return call

    @property
    def spectra_axes(self):
        """The underlying :class:`spectra.Axes`."""
        return self._ax


class Figure:
    """matplotlib-like wrapper around a Spectra figure."""

    def __init__(self, fig: Any, figsize: Optional[Tuple[float, float]] = None) -> None:
        self._fig = fig
        self.figsize = figsize
        self.axes: List[Axes] = []

    def savefig(self, fname, **kwargs):
        prev = _sp._state._current_fig
        _sp._state._current_fig = self._fig
        try:
            return savefig(fname, **kwargs)
        finally:
            _sp._state._current_fig = prev

    def suptitle(self, t: str, **kwargs) -> None:
        _ignore("suptitle", kwargs)
        self._fig.title = str(t)

    def tight_layout(self, *args, **kwargs) -> None:
        """No-op: Spectra lays subplots out automatically."""

    def show(self) -> None:
        show()

    @property
    def spectra_figure(self):
        """The underlying :class:`spectra.Figure`."""
        return self._fig


_figures: Dict[int, Figure] = {}  # id(spectra figure) -> wrapper


def _wrap_current_figure() -> Figure:
    fig = _sp._state._ensure_figure()
    w = _figures.get(id(fig))
    if w is None:
        w = Figure(fig)
        _figures[id(fig)] = w
    return w


# ─── Figures and axes ────────────────────────────────────────────────────────

def figure(num=None, figsize: Optional[Tuple[float, float]] = None, dpi: Optional[float] = None,
           **kwargs) -> Figure:
    """Start a new figure.  ``figsize`` is in inches (at ``dpi``, default 100)."""
    _ignore("figure", {k: v for k, v in kwargs.items() if k not in ("facecolor", "clear")})
    title = num if isinstance(num, str) else ""
    width, height = _sp.DEFAULT_WINDOW_WIDTH, _sp.DEFAULT_WINDOW_HEIGHT
    if figsize is not None:
        d = float(dpi) if dpi else _DEFAULT_DPI
        width, height = int(round(figsize[0] * d)), int(round(figsize[1] * d))
    fig = _sp.figure(title=title, width=width, height=height)
    w = Figure(fig, tuple(figsize) if figsize is not None else None)
    _figures[id(fig)] = w
    return w


def subplot(*args, **kwargs) -> Axes:
    """subplot(nrows, ncols, index) or subplot(221)."""
    _ignore("subplot", kwargs)
    if len(args) == 1 and isinstance(args[0], int) and args[0] >= 111:
        n = args[0]
        args = (n // 100, (n // 10) % 10, n % 10)
    if len(args) != 3:
        raise TypeError("subplot() takes (nrows, ncols, index) or a 3-digit integer")
    rows, cols, index = (int(a) for a in args)
    ax = _sp.subplot(rows, cols, index)
    fig = _wrap_current_figure()
    w = Axes(fig, ax, (rows, cols, index))
    fig.axes.append(w)
    return w


def subplots(nrows: int = 1, ncols: int = 1, figsize=None, dpi=None, squeeze: bool = True,
             sharex=False, sharey=False, **kwargs):
    """New figure with a grid of axes: returns ``(fig, axs)`` like matplotlib.

    With ``squeeze`` a 1x1 grid gives one Axes, a single row/column a 1-D
    sequence and anything else a 2-D array (NumPy object array when NumPy is
    installed, else a list of rows).
    """
    if sharex or sharey:
        _ignore("subplots", {"sharex" if sharex else "sharey": True})
    _ignore("subplots", kwargs)
    fig = figure(figsize=figsize, dpi=dpi)
    grid = []
    for r in range(nrows):
        row = []
        for c in range(ncols):
            index = r * ncols + c + 1
            row.append(Axes(fig, _sp.subplot(nrows, ncols, index), (nrows, ncols, index)))
        grid.append(row)
    fig.axes = [a for row in grid for a in row]
    # The first axes is current, like matplotlib after subplots().
    grid[0][0]._activate()
    if squeeze and nrows == 1 and ncols == 1:
        return fig, grid[0][0]
    if squeeze and (nrows == 1 or ncols == 1):
        flat = fig.axes
        try:
            import numpy as np

            arr = np.empty(len(flat), dtype=object)
            arr[:] = flat
            return fig, arr
        except ImportError:
            return fig, flat
    try:
        import numpy as np

        arr = np.empty((nrows, ncols), dtype=object)
        for r in range(nrows):
            for c in range(ncols):
                arr[r, c] = grid[r][c]
        return fig, arr
    except ImportError:
        return fig, grid


def gcf() -> Figure:
    return _wrap_current_figure()


def gca() -> Axes:
    fig = _wrap_current_figure()
    ax = _sp._state._ensure_axes()
    for w in fig.axes:
        if w._ax is ax:
            return w
    w = Axes(fig, ax, _sp._state._current_axes_key)
    fig.axes.append(w)
    return w


def close(fig=None) -> None:
    """Close the current figure (or all with ``close("all")``)."""
    if fig == "all":
        _figures.clear()
        _sp.close_all()
        return
    if isinstance(fig, Figure):
        _sp._state._current_fig = fig._fig
    _sp.close()


def show(*args, **kwargs) -> None:
    _ignore("show", {k: v for k, v in kwargs.items() if k != "block"})
    _sp.show()


def tight_layout(*args, **kwargs) -> None:
    """No-op: Spectra lays subplots out automatically."""


def savefig(fname, dpi=None, format=None, **kwargs):
    """Save the current figure.  ``dpi`` scales a ``figsize`` figure like matplotlib.

    ``width`` / ``height`` (pixels) and ``theme`` are Spectra extensions.
    """
    width = int(kwargs.pop("width", 0))
    height = int(kwargs.pop("height", 0))
    theme = kwargs.pop("theme", "")
    kwargs.pop("bbox_inches", None)  # layout is always tight
    _ignore("savefig", kwargs)
    if dpi is not None and dpi != "figure" and not (width or height):
        w = _figures.get(id(_sp._state._current_fig))
        if w is not None and w.figsize is not None:
            width = int(round(w.figsize[0] * float(dpi)))
            height = int(round(w.figsize[1] * float(dpi)))
    return _sp.savefig(fname, width=width, height=height, format=format, theme=theme)


# ─── Labels and limits ───────────────────────────────────────────────────────

def xlabel(label: str, **kwargs) -> None:
    _ignore("xlabel", kwargs)
    _sp.xlabel(str(label))


def ylabel(label: str, **kwargs) -> None:
    _ignore("ylabel", kwargs)
    _sp.ylabel(str(label))


def title(label: str, **kwargs) -> None:
    _ignore("title", kwargs)
    _sp.title(str(label))


def suptitle(t: str, **kwargs) -> None:
    gcf().suptitle(t, **kwargs)


def legend(*args, **kwargs) -> None:
    """Show the legend (entries come from each series' ``label``)."""
    if args:
        _ignore("legend", {"handles/labels": True})
    _ignore("legend", kwargs)
    _sp.legend(True)


def grid(visible: Optional[bool] = True, **kwargs) -> None:
    _ignore("grid", {k: v for k, v in kwargs.items() if k not in ("which", "axis")})
    _sp.grid(True if visible is None else bool(visible))


def _limits(args, kwargs, lo_key: str, hi_key: str, current):
    if len(args) == 1:
        lo, hi = args[0]
    elif len(args) == 2:
        lo, hi = args
    else:
        lo, hi = kwargs.pop(lo_key, None), kwargs.pop(hi_key, None)
    if lo is None or hi is None:
        cur = current()
        lo = cur[0] if lo is None else lo
        hi = cur[1] if hi is None else hi
    return float(lo), float(hi)


def _bounds(which: int):
    ax = _sp._state._ensure_axes()
    b = _sp._state._axes_bounds.get((ax._figure_id, ax._index))
    if b is None:
        return (0.0, 1.0)
    return (b[0], b[1]) if which == 0 else (b[2], b[3])


def xlim(*args, **kwargs):
    """Set (or with no arguments, return) the x limits of the current axes."""
    if not args and "left" not in kwargs and "right" not in kwargs:
        return _bounds(0)
    lo, hi = _limits(args, kwargs, "left", "right", lambda: _bounds(0))
    _ignore("xlim", kwargs)
    _sp.xlim(lo, hi)
    return lo, hi


def ylim(*args, **kwargs):
    """Set (or with no arguments, return) the y limits of the current axes."""
    if not args and "bottom" not in kwargs and "top" not in kwargs:
        return _bounds(1)
    lo, hi = _limits(args, kwargs, "bottom", "top", lambda: _bounds(1))
    _ignore("ylim", kwargs)
    _sp.ylim(lo, hi)
    return lo, hi


def xscale(value: str, **kwargs) -> None:
    _ignore("xscale", kwargs)
    _sp.xscale(value)


def yscale(value: str, **kwargs) -> None:
    _ignore("yscale", kwargs)
    _sp.yscale(value)


# ─── Plotting ────────────────────────────────────────────────────────────────

def _style(kwargs: Dict[str, Any]) -> Dict[str, Any]:
    """Pop the common matplotlib style keywords into easy-API keywords."""
    out: Dict[str, Any] = {}
    color = _pop_alias(kwargs, "color", "c")
    if color is not None:
        out["color"] = color
    out["label"] = str(kwargs.pop("label", "") or "")
    return out


def plot(*args, **kwargs):
    """plot([x], y, [fmt], ...): lines; a marker-only fmt ("o", ".") draws points."""
    style = _style(kwargs)
    width = _pop_alias(kwargs, "linewidth", "lw")
    alpha = kwargs.pop("alpha", None)
    linestyle = _pop_alias(kwargs, "linestyle", "ls")
    marker = kwargs.pop("marker", None)
    markersize = _pop_alias(kwargs, "markersize", "ms")
    _ignore("plot", kwargs)
    out = []
    for x, y, fmt in _split_plot_args(args):
        fcolor, fmarker, fline = _parse_fmt(fmt) if fmt else (None, None, None)
        color = style.get("color", fcolor)
        mk = marker or fmarker
        ls = linestyle or fline
        pos = (y,) if x is None else (x, y)
        points_only = mk is not None and ls is None and mk not in (",",)
        if points_only:
            size = float(markersize) if markersize is not None else (3.0 if mk == "." else 6.0)
            s = _sp.scatter(*pos, color=color, size=size, label=style["label"], opacity=alpha)
        else:
            if ls not in (None, "-", "solid"):
                _ignore("plot", {f"linestyle {ls!r}": True})
            if mk is not None:
                _ignore("plot", {f"marker {mk!r} on a line": True})
            s = _sp.plot(*pos, color=color, width=width, label=style["label"], opacity=alpha)
        out.append(s)
    return out


def semilogx(*args, **kwargs):
    lines = plot(*args, **kwargs)
    _sp.xscale("log")
    return lines


def semilogy(*args, **kwargs):
    lines = plot(*args, **kwargs)
    _sp.yscale("log")
    return lines


def loglog(*args, **kwargs):
    lines = plot(*args, **kwargs)
    _sp.xscale("log")
    _sp.yscale("log")
    return lines


def scatter(x, y, s=None, c=None, marker=None, alpha=None, **kwargs):
    """Scatter plot; ``s`` is the marker area in points² (scalar only)."""
    style = _style(kwargs)
    if c is not None and "color" not in style:
        if isinstance(c, (str, tuple)):
            style["color"] = c
        else:
            _ignore("scatter", {"c (per-point colours)": True})
    size = None
    if s is not None:
        if isinstance(s, (int, float)):
            size = max(1.0, math.sqrt(float(s)))
        else:
            _ignore("scatter", {"s (per-point sizes)": True})
    if marker not in (None, "o", "."):
        _ignore("scatter", {"marker": marker})
    _ignore("scatter", kwargs)
    return _sp.scatter(x, y, size=size, opacity=alpha, **style)


def bar(x, height, width=0.8, bottom=None, align="center", **kwargs):
    style = _style(kwargs)
    if align != "center":
        _ignore("bar", {"align": align})
    _ignore("bar", {k: v for k, v in kwargs.items() if k not in ("edgecolor", "linewidth")})
    return _sp.bar(x, height, bar_width=float(width),
                   bottom=float(bottom) if bottom is not None else 0.0, **style)


def barh(y, width, height=0.8, left=None, align="center", **kwargs):
    style = _style(kwargs)
    if align != "center":
        _ignore("barh", {"align": align})
    _ignore("barh", {k: v for k, v in kwargs.items() if k not in ("edgecolor", "linewidth")})
    return _sp.bar(y, width, bar_width=float(height),
                   bottom=float(left) if left is not None else 0.0, horizontal=True, **style)


def hist(x, bins=10, density=False, cumulative=False, **kwargs):
    """Histogram; returns ``(counts, bin_edges, series)`` like matplotlib."""
    style = _style(kwargs)
    kwargs.pop("histtype", None)
    kwargs.pop("alpha", None)
    _ignore("hist", kwargs)
    if not isinstance(bins, int) or isinstance(bins, bool):
        raise TypeError("spectra.pyplot.hist: bins must be an int (equal-width bins)")
    series = _sp.hist(x, bins=bins, density=bool(density), cumulative=bool(cumulative), **style)
    values = [v for v in _sp._to_list(x) if math.isfinite(v)]
    if not values:
        return [], [], series
    lo, hi, counts = _sp._histogram_counts(values, bins, bool(density), bool(cumulative))
    step = (hi - lo) / bins
    edges = [lo + i * step for i in range(bins)] + [hi]
    return counts, edges, series


def errorbar(x, y, yerr=None, xerr=None, fmt="", **kwargs):
    style = _style(kwargs)
    ecolor = kwargs.pop("ecolor", None)
    capsize = float(kwargs.pop("capsize", 0.0) or 0.0)
    width = _pop_alias(kwargs, "linewidth", "lw", "elinewidth")
    _ignore("errorbar", kwargs)
    fcolor, fmarker, fline = _parse_fmt(fmt) if fmt and fmt != "none" else (None, None, None)
    color = style.get("color", fcolor if fcolor is not None else ecolor)
    if fmt == "none":
        efmt = "none"
    elif fmarker is not None and fline is None:
        efmt = "o" if fmarker != "." else "."
    else:
        efmt = "-"
    # matplotlib capsize is in points; Spectra caps are in data units, so
    # draw a small cap relative to the x spacing.
    cap = 0.0
    if capsize > 0:
        xs = sorted(set(_sp._to_list(x)))
        spacing = min((b - a for a, b in zip(xs, xs[1:])), default=1.0) or 1.0
        cap = spacing * 0.3
    return _sp.errorbar(x, y, yerr=yerr, xerr=xerr, fmt=efmt, color=color, capsize=cap,
                        label=style["label"], width=width)


def fill_between(x, y1, y2=0, alpha=None, **kwargs):
    style = _style(kwargs)
    for k in ("interpolate", "linewidth", "lw"):
        kwargs.pop(k, None)
    _ignore("fill_between", kwargs)  # where=, step=, ...
    return _sp.fill_between(x, y1, y2, alpha=0.25 if alpha is None else float(alpha), **style)


def stem(*args, linefmt=None, markerfmt=None, basefmt=None, bottom=0, **kwargs):
    style = _style(kwargs)
    _ignore("stem", kwargs)
    if "color" not in style:
        for f in (linefmt, markerfmt):
            if f and f[0] in _COLOR_CHARS:
                style["color"] = f[0]
                break
    return _sp.stem(*args, baseline=float(bottom), **style)


def _set_title(label, **kw):
    title(label, **kw)


_AXES_METHODS = {
    "plot": plot,
    "scatter": scatter,
    "bar": bar,
    "barh": barh,
    "hist": hist,
    "errorbar": errorbar,
    "fill_between": fill_between,
    "stem": stem,
    "semilogx": semilogx,
    "semilogy": semilogy,
    "loglog": loglog,
    "set_xlabel": xlabel,
    "set_ylabel": ylabel,
    "set_title": _set_title,
    "legend": legend,
    "grid": grid,
    "set_xlim": xlim,
    "set_ylim": ylim,
    "set_xscale": xscale,
    "set_yscale": yscale,
}
