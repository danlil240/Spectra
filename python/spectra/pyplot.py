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
legend, xlim/ylim, xscale/yscale, grid, axhline/axvline, cla/clf,
ion/ioff/draw/pause, colorbar, xticks/yticks, savefig, show, close, gcf/gca and the matching ``Axes``
methods.  Format strings ("r--o"), ``linestyle=`` and
``marker=`` are honoured.  Keyword arguments Spectra cannot honour (dash
tuples, per-point colours, ``bbox_inches`` ...) are ignored with one
``UserWarning`` per function and argument.  Subplot spacing is fitted to the
labels (constrained layout, on by default); ``tight_layout`` turns it back on.
``subplots(sharex=..., sharey=...)`` links axis limits and ``suptitle`` draws
a title above the subplot grid.
"""

from __future__ import annotations

import math
import re
import warnings
from typing import Any, Dict, List, Optional, Sequence, Tuple

from . import _easy as _sp
from . import style  # plt.style.use("paper") / plt.style.context("talk")

__all__ = [
    "plot", "scatter", "bar", "barh", "hist", "errorbar", "fill_between", "stem",
    "semilogx", "semilogy", "loglog", "figure", "subplot", "subplots", "xlabel",
    "ylabel", "title", "suptitle", "legend", "xlim", "ylim", "xscale", "yscale",
    "grid", "savefig", "show", "close", "gcf", "gca", "twinx", "tight_layout", "style", "step",
    "stairs", "text", "annotate", "axhline", "axvline", "cla", "clf", "ion", "ioff",
    "isinteractive", "draw", "pause", "colorbar", "xticks", "yticks",
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
    """matplotlib fmt "[color][marker][line]" -> (colour letter or "C<n>", marker, linestyle)."""
    color = marker = line = None
    rest = fmt
    while rest:
        for ls in _LINESTYLES:
            if rest.startswith(ls):
                line, rest = ls, rest[len(ls):]
                break
        else:
            m = re.match(r"C\d+", rest)
            if m and color is None:
                # "C1--": palette slot colour, as in matplotlib.
                color, rest = m.group(0), rest[m.end():]
                continue
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

    def twinx(self) -> "Axes":
        """A second y axis on the right, sharing this axes' x axis (matplotlib's twinx)."""
        return twinx(self)

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
        """Title centred above all subplots of the figure."""
        _ignore("suptitle", kwargs)
        self._fig.suptitle(str(t))

    def colorbar(self, mappable=None, cax=None, ax=None, **kwargs) -> "Colorbar":
        return colorbar(mappable, cax=cax, ax=ax, **kwargs)

    def tight_layout(self, *args, **kwargs) -> None:
        """Fit the subplot spacing to the labels (constrained layout)."""
        _ignore("tight_layout", kwargs)
        self._fig.tight_layout()

    def show(self) -> None:
        show()

    def _repr_png_(self):
        """Jupyter rich display: the figure as an inline PNG image."""
        return self._fig._repr_png_()

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
    installed, else a list of rows).  ``sharex`` / ``sharey`` (True/"all",
    "row", "col", False/"none") link axis limits and keep tick labels on the
    outer axes only; ``layout="constrained"`` / ``constrained_layout`` choose
    whether the spacing is fitted to the labels (on by default).
    """
    layout = kwargs.pop("layout", None)
    constrained = kwargs.pop("constrained_layout", None)
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
    if sharex or sharey:
        _sp._share_axes(fig._fig, [a._ax for a in fig.axes], ncols, sharex, sharey)
    if layout is not None or constrained is not None:
        on = bool(constrained) if constrained is not None else layout in ("constrained", "tight")
        fig._fig.set_layout(constrained=on)
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


def twinx(ax: Optional[Axes] = None) -> Axes:
    """A second y axis on the right of ``ax`` (default: the current axes).

    It shares the x axis and plot area of ``ax`` and becomes the current
    axes.  Returns the new axes (the same one on later calls).
    """
    host = ax if ax is not None else gca()
    host._activate()
    twin = _sp.twinx(host._ax)
    for w in host.figure.axes:
        if w._ax is twin:
            return w
    w = Axes(host.figure, twin, host._key)
    host.figure.axes.append(w)
    return w


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
    """Show the figures: block until windows close, or (Jupyter inline mode)
    display them under the cell and return at once."""
    _ignore("show", {k: v for k, v in kwargs.items() if k != "block"})
    _sp.show()


def tight_layout(*args, **kwargs) -> None:
    """Fit the subplot spacing of the current figure to its labels (constrained layout)."""
    gcf().tight_layout(*args, **kwargs)


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


def _style_token(func: str, kind: str, value: Any) -> Optional[str]:
    """matplotlib line style / marker -> Spectra token; warn once and drop unsupported ones."""
    if value is None:
        return None
    from ._style import normalize_linestyle, normalize_marker

    try:
        return normalize_linestyle(value) if kind == "linestyle" else normalize_marker(value)
    except ValueError:
        _ignore(func, {f"{kind} {value!r}": True})
        return None


def plot(*args, **kwargs):
    """plot([x], y, [fmt], ...): lines; a marker-only fmt ("o", ".") draws points.

    ``fmt`` colours, line styles ("-", "--", ":", "-.") and markers ("o", "s",
    "^", ...) are drawn like matplotlib, as are ``linestyle=``/``ls=`` and
    ``marker=``.
    """
    style = _style(kwargs)
    width = _pop_alias(kwargs, "linewidth", "lw")
    alpha = kwargs.pop("alpha", None)
    linestyle = _pop_alias(kwargs, "linestyle", "ls")
    marker = kwargs.pop("marker", None)
    markersize = _pop_alias(kwargs, "markersize", "ms")
    drawstyle = _pop_alias(kwargs, "drawstyle", "ds")
    where = kwargs.pop("where", None)
    _ignore("plot", kwargs)
    line_kw: Dict[str, Any] = {}
    if drawstyle is not None:
        line_kw["drawstyle"] = drawstyle
    elif where is not None:
        line_kw["where"] = where
    out = []
    for x, y, fmt in _split_plot_args(args):
        fcolor, fmarker, fline = _parse_fmt(fmt) if fmt else (None, None, None)
        color = style.get("color", fcolor)
        mk = _style_token("plot", "marker", marker if marker is not None else fmarker)
        ls = _style_token("plot", "linestyle", linestyle if linestyle is not None else fline)
        pos = (y,) if x is None else (x, y)
        points_only = mk not in (None, "none") and ls is None
        size = float(markersize) if markersize is not None else None
        if points_only:
            if size is None:
                size = 3.0 if mk == "." else 6.0
            s = _sp.scatter(*pos, color=color, size=size, label=style["label"], opacity=alpha,
                            marker=mk)
        else:
            s = _sp.plot(*pos, color=color, width=width, label=style["label"], opacity=alpha,
                         linestyle=ls, marker=mk, size=size, **line_kw)
        out.append(s)
    return out


def step(*args, where: str = "pre", **kwargs):
    """step([x], y, [fmt], where="pre"|"mid"|"post"): a stepwise constant line."""
    kwargs.pop("drawstyle", None)
    kwargs.pop("ds", None)
    return plot(*args, where=where, **kwargs)


def stairs(values, edges=None, *, baseline=None, fill: bool = False, **kwargs):
    """stairs(values, edges): values[i] holds from edges[i] to edges[i + 1]."""
    if baseline not in (None, 0, 0.0):
        _ignore("stairs", {"baseline": baseline})
    if fill:
        _ignore("stairs", {"fill": fill})
    style = _style(kwargs)
    width = _pop_alias(kwargs, "linewidth", "lw")
    alpha = kwargs.pop("alpha", None)
    _ignore("stairs", kwargs)
    return _sp.stairs(values, edges, color=style.get("color"), width=width,
                      label=style["label"], opacity=alpha)


def _text_style(kwargs: Dict[str, Any], fontsize: float) -> Dict[str, Any]:
    """matplotlib text keywords -> easy-API text keywords (bbox, rotation, ...)."""
    out: Dict[str, Any] = {}
    rotation = kwargs.pop("rotation", None)
    if rotation is not None:
        out["rotation"] = (90.0 if rotation == "vertical"
                           else 0.0 if rotation == "horizontal" else float(rotation))
    bbox = kwargs.pop("bbox", None)
    if isinstance(bbox, dict):
        # matplotlib's pad is a fraction of the font size (default 0.3).
        box = dict(bbox)
        box.pop("boxstyle", None)
        box.pop("alpha", None)
        box["pad"] = float(box.get("pad", 0.3)) * (fontsize or 13.0)
        out["bbox"] = box
    elif bbox is not None:
        out["bbox"] = bool(bbox)
    # Spectra extension: a halo around the glyphs (path_effects in matplotlib).
    halo = kwargs.pop("halo", None)
    if halo is not None:
        out["halo"] = halo
    return out


def text(x, y, s, fontdict=None, **kwargs):
    """text(x, y, s): text at data coordinates; bbox=, rotation=, ha=, va=, color=."""
    if fontdict:
        kwargs = {**fontdict, **kwargs}
    color = _pop_alias(kwargs, "color", "c")
    fontsize = float(_pop_alias(kwargs, "fontsize", "size", default=0.0) or 0.0)
    ha = _pop_alias(kwargs, "ha", "horizontalalignment", default="left")
    va = _pop_alias(kwargs, "va", "verticalalignment", default="baseline")
    extra = _text_style(kwargs, fontsize)
    _ignore("text", kwargs)
    return _sp.text(x, y, s, color=color, fontsize=fontsize, ha=ha, va=va, **extra)


def annotate(text, xy, xytext=None, **kwargs):
    """annotate(text, xy, xytext=None): label a point, with an arrow from xytext."""
    color = _pop_alias(kwargs, "color", "c")
    fontsize = float(_pop_alias(kwargs, "fontsize", "size", default=0.0) or 0.0)
    kwargs.pop("arrowprops", None)   # an arrow is always drawn when xytext is given
    extra = _text_style(kwargs, fontsize)
    _ignore("annotate", kwargs)
    return _sp.annotate(text, xy, xytext, color=color, fontsize=fontsize, **extra)


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


def _color_values(c) -> Optional[List[float]]:
    """``c`` as one number per point, or None when it is colours."""
    try:
        return _sp._to_list(c)
    except (TypeError, ValueError):
        return None


def scatter(x, y, s=None, c=None, marker=None, cmap=None, vmin=None, vmax=None, alpha=None,
            **kwargs):
    """Scatter plot; ``s`` is the marker area in points² (scalar only).

    ``c`` is one colour, or one number per point coloured through ``cmap``
    (default "viridis") with ``vmin`` / ``vmax`` as its ends.
    """
    style = _style(kwargs)
    extra: Dict[str, Any] = {}
    if c is not None and "color" not in style:
        values = None if isinstance(c, (str, tuple)) else _color_values(c)
        if isinstance(c, (str, tuple)):
            style["color"] = c
        elif values is not None:
            if (vmin is None) != (vmax is None):
                finite = [v for v in values if math.isfinite(v)] or [0.0, 1.0]
                lo, hi = min(finite), max(finite)
                vmin = lo if vmin is None else vmin
                vmax = hi if vmax is None else vmax
            extra = {"c": values, "cmap": cmap, "vmin": vmin, "vmax": vmax}
        else:
            _ignore("scatter", {"c (per-point colours)": True})
    size = None
    if s is not None:
        if isinstance(s, (int, float)):
            size = max(1.0, math.sqrt(float(s)))
        else:
            _ignore("scatter", {"s (per-point sizes)": True})
    mk = _style_token("scatter", "marker", marker)
    _ignore("scatter", kwargs)
    return _sp.scatter(x, y, size=size, opacity=alpha, marker=mk, **style, **extra)


def imshow(X, cmap=None, norm=None, aspect=None, interpolation=None, alpha=None, vmin=None,
           vmax=None, origin=None, extent=None, **kwargs):
    """Image of a 2D array (or an RGB(A) array); row 0 at the top unless
    ``origin="lower"``.  ``interpolation`` other than "bilinear" draws flat
    cells (matplotlib's "nearest")."""
    if norm is not None:
        _ignore("imshow", {"norm": True})
    if aspect not in (None, "auto"):   # cells stretch to the axes, like "auto"
        _ignore("imshow", {"aspect": aspect})
    label = kwargs.pop("label", "")
    _ignore("imshow", kwargs)
    interp = "bilinear" if interpolation in ("bilinear", "bicubic", "antialiased") else "nearest"
    return _sp.imshow(X, cmap=cmap, vmin=vmin, vmax=vmax, extent=extent,
                      origin=origin or "upper", interpolation=interp, label=label, alpha=alpha)


def _contour_style(name, kwargs):
    out: Dict[str, Any] = {}
    colors = kwargs.pop("colors", None)
    if colors is not None:
        if isinstance(colors, (list, tuple)) and len(colors) > 1 \
                and not all(isinstance(c, (int, float)) for c in colors):
            _ignore(name, {"colors (one per level)": True})
            colors = colors[0]
        out["color"] = colors
    width = _pop_alias(kwargs, "linewidths", "linewidth", "lw")
    if width is not None:
        if isinstance(width, (list, tuple)):
            width = width[0] if width else None
        if width is not None:
            out["linewidth"] = float(width)
    return out


def contour(*args, levels=None, cmap=None, vmin=None, vmax=None, **kwargs):
    """Contour lines: ``contour(Z)`` or ``contour(X, Y, Z)``; an int or
    sequence ``levels`` (also as the last positional argument)."""
    args, levels = _contour_levels(args, levels)
    style = _contour_style("contour", kwargs)
    label = kwargs.pop("label", "")
    _ignore("contour", kwargs)
    return _sp.contour(*args, levels=8 if levels is None else levels, cmap=cmap, vmin=vmin,
                       vmax=vmax, label=label, **style)


def contourf(*args, levels=None, cmap=None, vmin=None, vmax=None, **kwargs):
    """Filled contours: ``contourf(Z)`` or ``contourf(X, Y, Z)``."""
    args, levels = _contour_levels(args, levels)
    label = kwargs.pop("label", "")
    kwargs.pop("alpha", None)
    _ignore("contourf", kwargs)
    return _sp.contourf(*args, levels=8 if levels is None else levels, cmap=cmap, vmin=vmin,
                        vmax=vmax, label=label)


def _contour_levels(args, levels):
    """matplotlib allows the levels as a trailing positional argument."""
    if len(args) in (2, 4) and levels is None:
        return args[:-1], args[-1]
    return args, levels


def _bar_bottom(bottom):
    """(scalar bottom, stacked) for matplotlib's ``bottom=`` / ``left=``.

    A scalar is the bars' baseline.  A sequence is matplotlib's stacking
    idiom (``bar(x, b, bottom=a)``): the axes switches to stacked bars, which
    stack each series on the ones drawn before it.
    """
    if bottom is None:
        return 0.0, None
    try:
        len(bottom)
    except TypeError:
        return float(bottom), None
    return 0.0, True
def _fill_style(kwargs: Dict[str, Any]) -> Dict[str, Any]:
    """Pop matplotlib's patch style keywords (edgecolor, linewidth, alpha)."""
    out: Dict[str, Any] = {}
    edge = _pop_alias(kwargs, "edgecolor", "ec")
    if edge is not None and str(edge).lower() != "none":
        out["edge_color"] = edge
    width = _pop_alias(kwargs, "linewidth", "lw")
    if edge is not None and str(edge).lower() == "none":
        width = 0.0
    if width is not None:
        out["edge_width"] = float(width)
    alpha = kwargs.pop("alpha", None)
    if alpha is not None:
        out["fill_alpha"] = float(alpha)
    return out


def bar(x, height, width=0.8, bottom=None, align="center", **kwargs):
    style = _style(kwargs)
    style.update(_fill_style(kwargs))
    if align != "center":
        _ignore("bar", {"align": align})
    _ignore("bar", kwargs)
    base, stacked = _bar_bottom(bottom)
    return _sp.bar(x, height, bar_width=float(width), bottom=base, stacked=stacked, **style)


def barh(y, width, height=0.8, left=None, align="center", **kwargs):
    style = _style(kwargs)
    style.update(_fill_style(kwargs))
    if align != "center":
        _ignore("barh", {"align": align})
    _ignore("barh", kwargs)
    base, stacked = _bar_bottom(left)
    return _sp.bar(y, width, bar_width=float(height), bottom=base, horizontal=True,
                   stacked=stacked, **style)


def hist(x, bins=10, density=False, cumulative=False, **kwargs):
    """Histogram; returns ``(counts, bin_edges, series)`` like matplotlib."""
    style = _style(kwargs)
    style.update(_fill_style(kwargs))
    kwargs.pop("histtype", None)
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


def _ref_line(func: str, draw, value, lo, hi, kwargs: Dict[str, Any]):
    style = _style(kwargs)
    ls = _style_token(func, "linestyle", _pop_alias(kwargs, "linestyle", "ls"))
    width = _pop_alias(kwargs, "linewidth", "lw")
    alpha = kwargs.pop("alpha", None)
    if (lo, hi) != (0, 1):
        _ignore(func, {"xmin/xmax" if func == "axhline" else "ymin/ymax": True})
    kwargs.pop("zorder", None)
    _ignore(func, kwargs)
    series = draw(float(value), color=style.get("color"), label=style["label"])
    if ls is not None:
        series.set_line_style(ls)
    if width is not None:
        series.set_line_width(float(width))
    if alpha is not None:
        series.set_opacity(float(alpha))
    return series


def axhline(y=0, xmin=0, xmax=1, **kwargs):
    """Horizontal line across the axes at ``y`` (full width only)."""
    return _ref_line("axhline", _sp.hline, y, xmin, xmax, kwargs)


def axvline(x=0, ymin=0, ymax=1, **kwargs):
    """Vertical line across the axes at ``x`` (full height only)."""
    return _ref_line("axvline", _sp.vline, x, ymin, ymax, kwargs)


def cla() -> None:
    """Clear the current axes: remove its series."""
    _sp.clear()


def clf() -> None:
    """Clear the current figure: remove the series of every axes in it."""
    fig = _sp._state._current_fig
    if fig is None:
        return
    for ax in fig.axes:
        ax.clear()
        _sp._forget_axes_extent(ax)


# ─── Interactive mode ────────────────────────────────────────────────────────
#
# Spectra windows are separate processes that redraw on their own, so the
# figures are always live.  ``ion`` / ``ioff`` only record the mode; ``draw``
# and ``pause`` open any figure not yet on screen, and ``pause`` then sleeps
# so a plot-update loop runs at the rate the script asks for.

_interactive = False


def ion() -> None:
    """Turn interactive mode on (Spectra windows always update live)."""
    global _interactive
    _interactive = True


def ioff() -> None:
    """Turn interactive mode off."""
    global _interactive
    _interactive = False


def isinteractive() -> bool:
    return _interactive


def draw() -> None:
    """Open any figure not yet shown; updates already reach open windows."""
    _sp._state._show_if_pending()


def pause(interval: float) -> None:
    """Show pending figures, then wait ``interval`` seconds."""
    import time

    draw()
    if interval > 0:
        time.sleep(float(interval))


_fixed_ticks: Dict[Tuple[int, int], List[float]] = {}  # (id(axes), axis) -> positions


def _set_ticks(func: str, axis: int, ticks, labels, kwargs: Dict[str, Any]):
    for k in ("rotation", "fontsize", "ha", "va", "minor"):
        if k in kwargs:
            _ignore(func, {k: True})
            kwargs.pop(k)
    _ignore(func, kwargs)
    ax = _sp._state._ensure_axes()
    key = (id(ax), axis)
    if ticks is None:
        if labels is not None:
            ticks = _fixed_ticks.get(key)
            if ticks is None:
                raise TypeError(f"spectra.pyplot.{func}: labels need tick positions")
        else:
            pos = _fixed_ticks.get(key, [])
            return list(pos), []
    pos = [float(t) for t in _sp._to_list(ticks)]
    names = None if labels is None else [str(n) for n in labels]
    (ax.set_xticks if axis == 0 else ax.set_yticks)(pos, names)
    _fixed_ticks[key] = pos
    return pos, names or []


def xticks(ticks=None, labels=None, **kwargs):
    """Fixed x ticks (and labels); without arguments returns the fixed ticks set before."""
    return _set_ticks("xticks", 0, ticks, labels, kwargs)


def yticks(ticks=None, labels=None, **kwargs):
    """Fixed y ticks (and labels); without arguments returns the fixed ticks set before."""
    return _set_ticks("yticks", 1, ticks, labels, kwargs)


def _set_xticks(ticks, labels=None, **kwargs):
    _set_ticks("set_xticks", 0, ticks, labels, kwargs)


def _set_yticks(ticks, labels=None, **kwargs):
    _set_ticks("set_yticks", 1, ticks, labels, kwargs)


def _set_xticklabels(labels, **kwargs):
    _set_ticks("set_xticklabels", 0, None, labels, kwargs)


def _set_yticklabels(labels, **kwargs):
    _set_ticks("set_yticklabels", 1, None, labels, kwargs)


class Colorbar:
    """What :func:`colorbar` returns.  Spectra draws the bar itself, so its
    label and ticks cannot be changed; those calls warn once."""

    def __init__(self, ax: Any) -> None:
        self.ax = ax

    def set_label(self, label, **kwargs) -> None:
        _ignore("colorbar", {"set_label": True})

    def remove(self) -> None:
        self.ax.colorbar(False)


def colorbar(mappable=None, cax=None, ax=None, **kwargs) -> Colorbar:
    """Colorbar for the colormapped series of ``ax`` (default: the current axes)."""
    if cax is not None:
        _ignore("colorbar", {"cax": True})
    label = kwargs.pop("label", None)
    if label:
        _ignore("colorbar", {"label": True})
    _ignore("colorbar", kwargs)
    if isinstance(ax, (list, tuple)):
        ax = ax[0] if ax else None
    target = ax._ax if isinstance(ax, Axes) else _sp._current_axes_any()
    target.colorbar(True)
    return Colorbar(target)


def _set_title(label, **kw):
    title(label, **kw)


_AXES_METHODS = {
    "plot": plot,
    "scatter": scatter,
    "imshow": imshow,
    "contour": contour,
    "contourf": contourf,
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
    "axhline": axhline,
    "axvline": axvline,
    "cla": cla,
    "set_xticks": _set_xticks,
    "set_yticks": _set_yticks,
    "set_xticklabels": _set_xticklabels,
    "set_yticklabels": _set_yticklabels,
}
