"""Axes proxy — lightweight handle to axes within a figure."""

from __future__ import annotations

import math
from typing import TYPE_CHECKING, List, Optional, Sequence, Tuple, Union

from . import _protocol as P
from . import _codec as codec

if TYPE_CHECKING:
    from ._session import Session
    from ._series import Series


class Axes:
    """Proxy for an axes object within a figure.

    All mutations are sent to the backend via IPC.
    """

    __slots__ = (
        "_session",
        "_figure_id",
        "_index",
        "_series_list",
        "_is_3d",
        "_figure_axes",
        "_annotations",
        "_bar_mode",
        "_twin",
        "_twin_host",
    )

    def __init__(self, session: Session, figure_id: int, axes_index: int, is_3d: bool = False) -> None:
        self._session = session
        self._figure_id = figure_id
        self._index = axes_index
        self._series_list: List[Series] = []
        self._is_3d = is_3d
        # The owning Figure's axes list (shared reference), set by Figure. Series
        # indices are figure-wide, so a removal must re-index every axes.
        self._figure_axes: Optional[List["Axes"]] = None
        self._annotations: List["TextAnnotation"] = []
        self._bar_mode = "grouped"
        self._twin: Optional["Axes"] = None
        self._twin_host: Optional["Axes"] = None

    @property
    def index(self) -> int:
        return self._index

    @property
    def figure_id(self) -> int:
        return self._figure_id

    def line(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
        label: str = "",
    ) -> Series:
        """Add a line series to this axes."""
        return self._add_series("line", x, y, label)

    def scatter(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
        label: str = "",
    ) -> Series:
        """Add a scatter series to this axes."""
        return self._add_series("scatter", x, y, label)

    # ─── Native statistical series ──────────────────────────────────────
    # Payload layouts mirror src/ui/app/figure_snapshot.hpp.

    def bar(
        self,
        x: Union[List[float], List[str], "object"],
        heights: Union[List[float], "object"],
        width: float = 0.8,
        bottom: float = 0.0,
        horizontal: bool = False,
        label: str = "",
        fill_alpha: Optional[float] = None,
        edge_width: Optional[float] = None,
        edge_color: Optional[Sequence[float]] = None,
        stacked: Optional[bool] = None,
    ) -> Series:
        """Add a native bar series (filled bars, legend swatch).

        ``x`` holds bar positions, or category names: bar ``i`` then sits at
        x = i and the axis is labelled with the names.  ``fill_alpha`` (default
        0.88), ``edge_width`` (pixels, default 1) and ``edge_color`` (RGB(A)
        floats; default a theme-aware shade of the fill) style the bars.
        Several bar series at the same positions are drawn side by side
        (grouped); ``stacked=True`` stacks them instead (``stacked=False``
        groups them again), see :meth:`set_bar_mode`.
        """
        if stacked is not None:
            self.set_bar_mode("stacked" if stacked else "grouped")
        names = _category_names(x)
        hv = _floats(heights)
        xv = [float(i) for i in range(len(names))] if names is not None else _floats(x)
        if len(xv) != len(hv):
            raise ValueError(f"x has {len(xv)} values but heights has {len(hv)}")
        data = [float(width), float(bottom), 1.0 if horizontal else 0.0, 0.0, float(len(xv))]
        style = _stat_style(fill_alpha, edge_width, edge_color)
        series = self._add_stats_series("bar", data + xv + hv + style, label)
        if names is not None:
            series._set_categories(names)
        return series

    def histogram(
        self,
        values: Union[List[float], "object"],
        bins: int = 30,
        density: bool = False,
        cumulative: bool = False,
        label: str = "",
        fill_alpha: Optional[float] = None,
        edge_width: Optional[float] = None,
        edge_color: Optional[Sequence[float]] = None,
    ) -> Series:
        """Add a native histogram; the bins are computed by the renderer.

        ``fill_alpha``, ``edge_width`` and ``edge_color`` style the bins as in
        :meth:`bar`.
        """
        vals = [v for v in _floats(values) if math.isfinite(v)]
        nbins = max(1, int(bins))
        data = [float(nbins), 1.0 if cumulative else 0.0, 1.0 if density else 0.0, 0.0,
                float(len(vals))]
        style = _stat_style(fill_alpha, edge_width, edge_color)
        return self._add_stats_series("histogram", data + vals + style, label)

    def band(
        self,
        x: Union[List[float], "object"],
        lower: Union[List[float], "object"],
        upper: Union[List[float], "object"],
        fill_opacity: float = 0.25,
        edge_width: float = 1.0,
        show_edges: bool = True,
        label: str = "",
    ) -> Series:
        """Add a filled band between ``lower`` and ``upper`` (fill_between)."""
        xv, lo, hi = _floats(x), _floats(lower), _floats(upper)
        if not len(xv) == len(lo) == len(hi):
            raise ValueError(
                f"x, lower and upper must have the same length ({len(xv)}, {len(lo)}, {len(hi)})"
            )
        data = [float(fill_opacity), float(edge_width), 1.0 if show_edges else 0.0, 0.0,
                float(len(xv))]
        return self._add_stats_series("band", data + xv + lo + hi, label)

    def stem(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
        baseline: float = 0.0,
        stem_width: float = 1.5,
        show_heads: bool = True,
        label: str = "",
    ) -> Series:
        """Add a native stem series (stems from ``baseline`` with round heads)."""
        xv, yv = _floats(x), _floats(y)
        if len(xv) != len(yv):
            raise ValueError(f"x has {len(xv)} values but y has {len(yv)}")
        data = [float(baseline), float(stem_width), 1.0 if show_heads else 0.0, 0.0,
                float(len(xv))]
        return self._add_stats_series("stem", data + xv + yv, label)

    # ─── Grid series: images, heatmaps, contours ────────────────────────
    # Payload layouts mirror "image" / "contour" in src/ui/app/figure_snapshot.hpp.

    def imshow(
        self,
        values,
        cmap=None,
        vmin: Optional[float] = None,
        vmax: Optional[float] = None,
        extent: Optional[Sequence[float]] = None,
        origin: str = "upper",
        interpolation: str = "nearest",
        label: str = "",
        alpha: Optional[float] = None,
    ) -> Series:
        """Show a 2D grid as an image: ``values[row][col]``, row 0 at the top.

        A scalar grid is coloured through ``cmap`` (default "viridis") with
        ``vmin`` / ``vmax`` as its ends (default: the data range); an
        ``(rows, cols, 3|4)`` array is drawn as RGB(A) (floats in 0..1, or
        integers in 0..255).  ``extent=(x0, x1, y0, y1)`` places the outer cell
        edges in data space (default: cell (c, r) centred on (c, r)).
        ``origin="lower"`` puts row 0 at the bottom; ``interpolation`` is
        "nearest" (flat cells) or "bilinear".  NaN cells are transparent.
        """
        rows, cols, channels, flat = _grid(values, allow_rgb=True)
        return self._add_image(flat, rows, cols, channels, cmap=cmap, vmin=vmin, vmax=vmax,
                               extent=extent, origin=origin, interpolation=interpolation,
                               label=label, alpha=alpha)

    def heatmap(
        self,
        values,
        col_names: Optional[Sequence[str]] = None,
        row_names: Optional[Sequence[str]] = None,
        decimals: int = 2,
        cmap=None,
        vmin: Optional[float] = None,
        vmax: Optional[float] = None,
        label: str = "",
    ) -> Series:
        """Heatmap: :meth:`imshow` with each cell's value printed in it.

        ``col_names`` / ``row_names`` label the columns / rows on the axes
        (row 0 at the top); ``decimals`` sets the printed precision.
        """
        rows, cols, _, flat = _grid(values, allow_rgb=False)
        xs = [str(n) for n in col_names] if col_names is not None else []
        ys = [str(n) for n in row_names] if row_names is not None else []
        if xs and len(xs) != cols:
            raise ValueError(f"col_names has {len(xs)} names but the grid has {cols} columns")
        if ys and len(ys) != rows:
            raise ValueError(f"row_names has {len(ys)} names but the grid has {rows} rows")
        return self._add_image(flat, rows, cols, 1, cmap=cmap, vmin=vmin, vmax=vmax,
                               show_values=True, decimals=decimals, x_names=xs, y_names=ys,
                               label=label)

    def contour(
        self,
        z,
        x=None,
        y=None,
        levels: Union[int, Sequence[float]] = 8,
        cmap=None,
        vmin: Optional[float] = None,
        vmax: Optional[float] = None,
        linewidth: float = 1.5,
        label: str = "",
    ) -> Series:
        """Contour lines of ``z[row][col]``, the value at ``(x[col], y[row])``.

        Row 0 is at the bottom.  ``x`` / ``y`` are the node coordinates (1D,
        or 2D meshgrids as from ``numpy.meshgrid``; default 0..cols-1 and
        0..rows-1).  ``levels`` is about that many "nice" levels, or the
        explicit level values.  Lines are coloured per level through ``cmap``
        (default "viridis"); give the series a colour to draw them all in it.
        """
        rows, cols, _, flat = _grid(z, allow_rgb=False)
        if rows < 2 or cols < 2:
            raise ValueError("contour needs a grid of at least 2 x 2 values")
        xs, ys = _node_coords(x, y, cols, rows)
        count, explicit = _levels_arg(levels)
        data = [float(linewidth), float(count), 0.0, 1.0 if xs is not None else 0.0,
                0.0, 0.0, 0.0, 0.0, float(len(explicit))] + explicit + [float(cols), float(rows)]
        if xs is not None:
            data += xs + ys
        series = self._add_stats_series("contour", data + flat, label)
        _apply_cmap(series, cmap, vmin, vmax, flat)
        return series

    def contourf(
        self,
        z,
        x=None,
        y=None,
        levels: Union[int, Sequence[float]] = 8,
        cmap=None,
        vmin: Optional[float] = None,
        vmax: Optional[float] = None,
        label: str = "",
    ) -> Series:
        """Filled contours of ``z`` (same layout as :meth:`contour`): colour
        bands between about ``levels`` nice levels, or between the given
        level values (sorted).  ``x`` / ``y`` give the extent (their first and
        last values; the bands are drawn on a uniform grid)."""
        rows, cols, _, flat = _grid(z, allow_rgb=False)
        if rows < 2 or cols < 2:
            raise ValueError("contourf needs a grid of at least 2 x 2 values")
        xs, ys = _node_coords(x, y, cols, rows)
        extent = (xs[0], xs[-1], ys[0], ys[-1]) if xs is not None else None
        count, explicit = _levels_arg(levels)
        if explicit and len(explicit) < 2:
            raise ValueError("contourf needs at least 2 levels")
        lv = sorted(explicit) if explicit else [float(count)]
        return self._add_image(flat, rows, cols, 1, cmap=cmap, vmin=vmin, vmax=vmax,
                               extent=extent, origin="lower", interpolation="bilinear",
                               node_aligned=True, levels=lv, label=label)

    def _add_image(self, flat, rows, cols, channels, cmap=None, vmin=None, vmax=None,
                   extent=None, origin="upper", interpolation="nearest", node_aligned=False,
                   levels=None, show_values=False, decimals=2, x_names=(), y_names=(),
                   label="", alpha=None) -> Series:
        if origin not in ("upper", "lower"):
            raise ValueError(f"origin must be 'upper' or 'lower', not {origin!r}")
        if interpolation not in ("nearest", "bilinear"):
            raise ValueError(
                f"interpolation must be 'nearest' or 'bilinear', not {interpolation!r}")
        ext = [0.0, 0.0, 0.0, 0.0]
        if extent is not None:
            ext = [float(v) for v in extent]
            if len(ext) != 4 or not all(math.isfinite(v) for v in ext):
                raise ValueError("extent must be 4 finite numbers (x0, x1, y0, y1)")
        lv = [float(v) for v in (levels or [])]
        data = [1.0 if interpolation == "bilinear" else 0.0,
                1.0 if origin == "lower" else 0.0,
                1.0 if node_aligned else 0.0,
                1.0 if show_values else 0.0,
                float(max(0, min(10, int(decimals)))),
                1.0 if extent is not None else 0.0] + ext + [
                float(len(x_names)), float(len(y_names)), float(len(lv))] + lv + [
                float(cols), float(rows), float(channels)]
        series = self._add_stats_series("image", data + flat, label)
        if x_names or y_names:
            series._set_categories(list(x_names) + list(y_names))
        if channels == 1:
            _apply_cmap(series, cmap, vmin, vmax, flat)
        if alpha is not None:
            series.set_opacity(float(alpha))
        return series

    def _add_stats_series(self, series_type: str, data: List[float], label: str) -> Series:
        from ._series import Series

        if self.is_3d:
            raise ValueError(f"{series_type} series need a 2D axes")
        payload = codec.encode_req_add_series(
            figure_id=self._figure_id,
            axes_index=self._index,
            series_type=series_type,
            label=label,
        )
        resp = self._session._request(P.REQ_ADD_SERIES, payload)
        _, series_index = codec.decode_resp_series_added(resp["payload"])
        series = Series(self._session, self._figure_id, series_index, series_type, label)
        self._series_list.append(series)
        series._set_payload(data)
        return series

    def _add_series(
        self,
        series_type: str,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
        label: str = "",
    ) -> Series:
        from ._series import Series

        # Request series creation
        payload = codec.encode_req_add_series(
            figure_id=self._figure_id,
            axes_index=self._index,
            series_type=series_type,
            label=label,
        )
        resp = self._session._request(P.REQ_ADD_SERIES, payload)
        _, series_index = codec.decode_resp_series_added(resp["payload"])

        series = Series(self._session, self._figure_id, series_index, series_type, label)
        self._series_list.append(series)

        # Set initial data
        series.set_data(x, y)

        return series

    def _add_series_3d(
        self,
        series_type: str,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
        z: Union[List[float], "object"],
        label: str = "",
    ) -> Series:
        """Add a 3D series and set XYZ data."""
        from ._series import Series

        payload = codec.encode_req_add_series(
            figure_id=self._figure_id,
            axes_index=self._index,
            series_type=series_type,
            label=label,
        )
        resp = self._session._request(P.REQ_ADD_SERIES, payload)
        _, series_index = codec.decode_resp_series_added(resp["payload"])

        series = Series(self._session, self._figure_id, series_index, series_type, label)
        self._series_list.append(series)

        series.set_data_xyz(x, y, z)

        return series

    @property
    def series(self) -> List[Series]:
        return list(self._series_list)

    def twinx(self) -> "Axes":
        """A second y axis on the right of this axes, sharing its x axis.

        Series added to the returned axes use its own y limits, ticks, scale
        and label (drawn on the right), while x limits, pan and zoom follow
        this axes.  Its series join this axes' legend and continue its colour
        cycle.  Calling it again returns the same twin; on a twin it returns
        that twin.
        """
        if self._is_3d:
            raise ValueError("twinx() needs a 2D axes")
        if self._twin_host is not None:
            return self
        if self._twin is not None:
            return self._twin
        payload = codec.encode_req_create_axes(
            figure_id=self._figure_id, rows=1, cols=1, index=1, twin_of=self._index
        )
        resp = self._session._request(P.REQ_CREATE_AXES, payload)
        _, axes_index = codec.decode_resp_axes_created(resp["payload"])
        twin = Axes(self._session, self._figure_id, axes_index)
        twin._twin_host = self
        twin._figure_axes = self._figure_axes
        if self._figure_axes is not None:
            self._figure_axes.append(twin)
        self._twin = twin
        return twin

    @property
    def twin(self) -> Optional["Axes"]:
        """The twin made by :meth:`twinx`, or None."""
        return self._twin

    @property
    def twin_host(self) -> Optional["Axes"]:
        """For a twin: the axes it belongs to; None otherwise."""
        return self._twin_host

    def set_xlim(self, xmin: float, xmax: float) -> None:
        if self._twin_host is not None:   # a twin's x axis is its host's
            self._twin_host.set_xlim(xmin, xmax)
            return
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="xlim",
            f1=xmin,
            f2=xmax,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_ylim(self, ymin: float, ymax: float) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="ylim",
            f1=ymin,
            f2=ymax,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_zlim(self, zmin: float, zmax: float) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="zlim",
            f1=zmin,
            f2=zmax,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_light_mode(self, mode: str) -> None:
        """3D axes light: ``"fixed"`` (a direction tied to the data axes) or
        ``"headlight"`` (from the camera, follows it while orbiting)."""
        mode = str(mode).lower()
        if mode not in ("fixed", "headlight"):
            raise ValueError("light mode must be 'fixed' or 'headlight'")
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="light_mode",
            str_val=mode,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_light_angles(self, azimuth: float, elevation: float) -> None:
        """3D axes fixed light direction: azimuth (degrees from +X towards +Y)
        and elevation (degrees above the XY plane), like MATLAB's lightangle."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="light_angles",
            f1=float(azimuth),
            f2=float(elevation),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_fog(self, strength: float) -> None:
        """3D axes fog: fade every series towards the background with distance
        from the camera; 0 (off) to 1 (strongest)."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="fog",
            f1=float(strength),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_xlabel(self, label: str) -> None:
        if self._twin_host is not None:   # the host draws the x axis and title
            self._twin_host.set_xlabel(label)
            return
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="xlabel",
            str_val=label,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_ylabel(self, label: str) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="ylabel",
            str_val=label,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_title(self, title: str) -> None:
        if self._twin_host is not None:   # the host draws the x axis and title
            self._twin_host.set_title(title)
            return
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="axes_title",
            str_val=title,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_xscale(self, scale: str) -> None:
        """Set the x axis scale: "linear", "log" (log10), "log2" or "sqrt"."""
        self._send_scale("xscale", scale)

    def set_yscale(self, scale: str) -> None:
        """Set the y axis scale: "linear", "log" (log10), "log2" or "sqrt"."""
        self._send_scale("yscale", scale)

    def _send_scale(self, prop: str, scale: str) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop=prop,
            f1=float(scale_to_wire(scale)),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_bar_mode(self, mode: str) -> None:
        """How several bar series at the same positions are drawn.

        ``"grouped"`` (default) puts them side by side, ``"stacked"`` stacks
        them (negative values stack downwards separately) and ``"overlay"``
        draws them over each other.
        """
        if mode not in _BAR_MODES:
            raise ValueError(f"bar mode must be one of {sorted(_BAR_MODES)}, got {mode!r}")
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="bar_mode",
            f1=float(_BAR_MODES[mode]),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)
        changed = mode != self._bar_mode
        self._bar_mode = mode
        if changed:
            # The easy API fits limits client-side: refit its bars to the mode.
            from . import _easy

            _easy._refit_bars(self)

    @property
    def bar_mode(self) -> str:
        """The bar mode last set from Python (``"grouped"`` by default)."""
        return self._bar_mode

    def set_xaxis_datetime(self, enabled: bool = True) -> None:
        """Show x tick labels as dates/times (x values are Unix epoch seconds, UTC)."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="xaxis_datetime",
            bool_val=bool(enabled),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_yaxis_datetime(self, enabled: bool = True) -> None:
        """Show y tick labels as dates/times (y values are Unix epoch seconds, UTC)."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="yaxis_datetime",
            bool_val=bool(enabled),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def grid(self, visible: bool = True) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="grid",
            bool_val=visible,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_xticks(self, ticks=None, labels=None) -> None:
        """Fixed x ticks at ``ticks`` (inside the limits), labelled with
        ``labels`` or the formatted values; ``[]`` hides the ticks and
        ``None`` goes back to automatic ticks."""
        self._send_ticks(0, ticks, labels)

    def set_yticks(self, ticks=None, labels=None) -> None:
        """Fixed y ticks; see :meth:`set_xticks`."""
        self._send_ticks(1, ticks, labels)

    def _send_ticks(self, axis: int, ticks, labels) -> None:
        if ticks is None:
            pos: List[float] = []
        else:
            pos = [float(t) for t in ticks]
            if not all(math.isfinite(p) for p in pos):
                raise ValueError("tick positions must be finite")
        names: List[str] = []
        if labels is not None and ticks is not None:
            names = [str(n) for n in labels]
            if len(names) != len(pos):
                raise ValueError(f"{len(names)} labels for {len(pos)} ticks")
            # A label may not contain the separator; an empty one is drawn blank.
            names = [n.replace("\x1f", " ") or " " for n in names]
        origin = pos[0] if pos else 0.0
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="ticks",
            f1=float(axis),
            f2=origin,
            bool_val=ticks is not None,
            data=[p - origin for p in pos],
            str_val="\x1f".join(names),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def colorbar(self, visible: bool = True) -> None:
        """Show (or hide) a colorbar for this axes' colormapped series
        (colormapped scatters, surfaces, images)."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="colorbar",
            bool_val=bool(visible),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def legend(self, visible: bool = True) -> None:
        """Toggle legend visibility on this axes."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="legend",
            bool_val=visible,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    # ─── Text annotations ────────────────────────────────────────────────

    def text(
        self,
        x: float,
        y: float,
        s: str,
        *,
        color: Optional[Tuple[float, float, float, float]] = None,
        fontsize: float = 0.0,
        ha: str = "left",
        va: str = "bottom",
        bbox=None,
        halo=None,
        rotation: float = 0.0,
    ) -> "TextAnnotation":
        """Place text with its anchor at data coordinates (x, y) (2D axes only).

        ``color`` is an RGBA tuple (None = theme text colour), ``fontsize`` is in
        pixels (0 = axis-label size), ``ha`` is "left"/"center"/"right" and
        ``va`` is "top"/"middle"/"bottom" (alignment relative to the anchor).
        ``bbox`` draws a background box: True for the theme default, or a dict
        with ``pad`` (pixels), ``facecolor`` / ``edgecolor`` (RGBA tuples) and
        ``linewidth`` (0 = no border).  ``halo`` outlines the glyphs: True for
        the theme background colour or an RGBA tuple.  ``rotation`` is in
        degrees, counter-clockwise about the anchor.
        """
        ann = TextAnnotation(self, len(self._annotations), x, y, s)
        ann._color = tuple(color) if color is not None else None
        ann._font_size = float(fontsize)
        ann._halign = _parse_align(ha, _HALIGN, "ha")
        ann._valign = _parse_align(va, _VALIGN, "va")
        ann._set_style(bbox, halo, rotation)
        ann._send(append=True)
        self._annotations.append(ann)
        return ann

    def annotate(
        self,
        s: str,
        xy: Tuple[float, float],
        xytext: Optional[Tuple[float, float]] = None,
        *,
        color: Optional[Tuple[float, float, float, float]] = None,
        fontsize: float = 0.0,
        bbox=None,
        halo=None,
        rotation: float = 0.0,
    ) -> "TextAnnotation":
        """Place text centred at ``xytext`` with an arrow pointing to ``xy``.

        Without ``xytext`` the text sits at ``xy`` and no arrow is drawn.
        ``bbox``, ``halo`` and ``rotation`` are as in :meth:`text`.
        """
        if xytext is None:
            return self.text(xy[0], xy[1], s, color=color, fontsize=fontsize, bbox=bbox,
                             halo=halo, rotation=rotation)
        ann = TextAnnotation(self, len(self._annotations), xytext[0], xytext[1], s)
        ann._color = tuple(color) if color is not None else None
        ann._font_size = float(fontsize)
        ann._halign = 1
        ann._valign = 1
        ann._arrow = (float(xy[0]), float(xy[1]))
        ann._set_style(bbox, halo, rotation)
        ann._send(append=True)
        self._annotations.append(ann)
        return ann

    @property
    def annotations(self) -> List["TextAnnotation"]:
        return list(self._annotations)

    def clear_annotations(self) -> None:
        """Remove every text annotation from this axes."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="clear_annotations",
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)
        for ann in self._annotations:
            ann._index = -1
        self._annotations.clear()

    def remove_series(self, index: int) -> None:
        """Remove the series at position ``index`` of this axes (``self.series``).

        The backend addresses series figure-wide, so the position is mapped to
        the series' figure-wide index; removing from the second subplot no
        longer removes a series of the first.
        """
        if not -len(self._series_list) <= index < len(self._series_list):
            raise IndexError(
                f"series index {index} out of range for axes with "
                f"{len(self._series_list)} series"
            )
        figure_index = self._series_list[index]._index
        payload = codec.encode_req_remove_series(
            figure_id=self._figure_id,
            series_index=figure_index,
        )
        self._session._request(P.REQ_REMOVE_SERIES, payload)
        self._on_series_removed(figure_index)

    def clear(self) -> None:
        """Remove all series from this axes."""
        # Remove highest index first so the remaining indices in this axes stay
        # valid; _on_series_removed re-indexes series in sibling axes.
        for s in sorted(self._series_list, key=lambda s: s._index, reverse=True):
            index = s._index
            payload = codec.encode_req_remove_series(
                figure_id=self._figure_id,
                series_index=index,
            )
            self._session._request(P.REQ_REMOVE_SERIES, payload)
            self._on_series_removed(index)
        self._series_list.clear()

    def _sibling_axes(self) -> List["Axes"]:
        """All axes of the owning figure (including self), without duplicates."""
        figure_axes = getattr(self, "_figure_axes", None)
        axes: List[Axes] = list(figure_axes) if figure_axes is not None else []
        if not any(a is self for a in axes):
            axes.append(self)
        return axes

    def _on_series_removed(self, index: int) -> None:
        """Mirror the backend after removing figure-wide series ``index``.

        The backend erases the series and shifts every later series index in
        the figure down by one, so drop the removed proxy from every axes and
        decrement the index of each later Series proxy (once per object).
        """
        seen = set()
        for ax in self._sibling_axes():
            ax._series_list = [s for s in ax._series_list if s._index != index]
            for s in ax._series_list:
                if id(s) in seen:
                    continue
                seen.add(id(s))
                if s._index > index:
                    s._index -= 1

    def batch(self) -> "_AxesBatchContext":
        """Context manager for batching multiple property updates into one IPC call.

        Usage::

            with ax.batch() as b:
                b.set_xlim(0, 10)
                b.set_ylim(-1, 1)
                b.set_xlabel("Time")
                b.set_ylabel("Value")
                b.grid(True)
        """
        return _AxesBatchContext(self)

    @property
    def is_3d(self) -> bool:
        return getattr(self, "_is_3d", False)

    def __repr__(self) -> str:
        is_3d = getattr(self, "_is_3d", False)
        return f"Axes(figure_id={self._figure_id}, index={self._index}, is_3d={is_3d})"



# spectra::ScaleType wire values (src/ipc/axes_format_state.hpp).
_SCALES = {"linear": 0, "log": 1, "log10": 1, "log2": 2, "sqrt": 3}


def scale_to_wire(scale: str) -> int:
    key = str(scale).lower()
    if key not in _SCALES:
        raise ValueError(f"unknown scale {scale!r}; expected one of {sorted(_SCALES)}")
    return _SCALES[key]


# Fill-style trailer of bar / histogram payloads (src/ui/app/figure_snapshot.hpp).
_STAT_STYLE_TAG = 21332.0
STAT_DEFAULT_FILL_ALPHA = 0.88
STAT_DEFAULT_EDGE_WIDTH = 1.0


def _stat_style(fill_alpha: Optional[float], edge_width: Optional[float],
                edge_color: Optional[Sequence[float]]) -> List[float]:
    """Payload trailer for a non-default fill style ([] keeps the defaults)."""
    if fill_alpha is None and edge_width is None and edge_color is None:
        return []
    a = STAT_DEFAULT_FILL_ALPHA if fill_alpha is None else float(fill_alpha)
    w = STAT_DEFAULT_EDGE_WIDTH if edge_width is None else float(edge_width)
    if not (math.isfinite(a) and 0.0 <= a <= 1.0):
        raise ValueError(f"fill_alpha must be in [0, 1], got {fill_alpha!r}")
    if not (math.isfinite(w) and w >= 0.0):
        raise ValueError(f"edge_width must be >= 0, got {edge_width!r}")
    rgba = [0.0, 0.0, 0.0, 1.0]
    if edge_color is not None:
        c = [float(v) for v in edge_color]
        if len(c) not in (3, 4) or not all(0.0 <= v <= 1.0 for v in c):
            raise ValueError(f"edge_color must be 3 or 4 floats in [0, 1], got {edge_color!r}")
        rgba = c + [1.0] if len(c) == 3 else c
    return [_STAT_STYLE_TAG, a, w, 1.0 if edge_color is not None else 0.0] + rgba



def _grid(values, allow_rgb: bool):
    """2D grid (or RGB(A) image) -> (rows, cols, channels, row-major floats)."""
    try:
        import numpy as np
    except ImportError:  # pragma: no cover - numpy is optional
        np = None
    if np is not None:
        arr = np.asarray(values)
        if arr.dtype == object:
            raise ValueError("grid values must be numbers")
        is_int = np.issubdtype(arr.dtype, np.integer) or arr.dtype == np.bool_
        arr = arr.astype(np.float32)
        if arr.ndim == 3 and allow_rgb and arr.shape[2] in (3, 4):
            if is_int:
                arr = arr / 255.0
            rows, cols, ch = arr.shape
            if ch == 3:
                arr = np.concatenate([arr, np.ones((rows, cols, 1), dtype=np.float32)], axis=2)
            return rows, cols, 4, np.clip(arr, 0.0, 1.0).ravel().tolist()
        if arr.ndim != 2 or arr.shape[0] == 0 or arr.shape[1] == 0:
            raise ValueError(f"expected a 2D grid of values, got shape {arr.shape}")
        return arr.shape[0], arr.shape[1], 1, arr.ravel().tolist()
    rows_list = [list(r) for r in values]
    if not rows_list or not rows_list[0]:
        raise ValueError("expected a non-empty 2D grid of values")
    cols = len(rows_list[0])
    if any(len(r) != cols for r in rows_list):
        raise ValueError("every row of the grid must have the same length")
    return len(rows_list), cols, 1, [float(v) for r in rows_list for v in r]


def _node_coords(x, y, cols: int, rows: int):
    """Contour node coordinates as 1D lists, or (None, None) for the defaults."""
    if x is None and y is None:
        return None, None
    if x is None or y is None:
        raise ValueError("give both x and y, or neither")

    def axis(v, n, along_cols):
        if hasattr(v, "ndim") and getattr(v, "ndim") == 2:
            v = v[0, :] if along_cols else v[:, 0]
        elif not hasattr(v, "ndim"):
            items = list(v)
            if items and isinstance(items[0], (list, tuple)):
                v = items[0] if along_cols else [r[0] for r in items]
        out = _floats(v)
        if len(out) != n:
            raise ValueError(f"expected {n} {'x' if along_cols else 'y'} coordinates, got {len(out)}")
        return out

    return axis(x, cols, True), axis(y, rows, False)


def _levels_arg(levels):
    """(approximate count, explicit values) from an int or a sequence of levels."""
    if isinstance(levels, bool):
        raise TypeError("levels must be an int or a sequence of numbers")
    if isinstance(levels, int):
        return max(1, min(256, levels)), []
    values = [v for v in _floats(levels) if math.isfinite(v)]
    if not values:
        raise ValueError("levels must hold at least one finite number")
    return len(values), values


def _apply_cmap(series, cmap, vmin, vmax, values) -> None:
    """Colormap and value range; a missing vmin / vmax is the data's."""
    if cmap is not None:
        series.set_colormap(cmap)
    if vmin is None and vmax is None:
        return
    finite = [v for v in values if math.isfinite(v)] or [0.0, 1.0]
    lo = min(finite) if vmin is None else float(vmin)
    hi = max(finite) if vmax is None else float(vmax)
    series.set_colormap_range(lo, hi)

def _floats(data) -> List[float]:
    """Array-like (list, tuple, NumPy, pandas, datetimes) -> list of floats."""
    from ._easy import _to_list

    return _to_list(data)


def _category_names(x) -> Optional[List[str]]:
    """Category names when ``x`` holds strings, else None."""
    if isinstance(x, str):
        return None
    try:
        items = list(x.tolist()) if hasattr(x, "tolist") else list(x)
    except TypeError:
        return None
    if items and all(isinstance(v, str) for v in items):
        return [str(v) for v in items]
    return None


# "annotation" property: series_index that appends (mirrors the daemon).
_ANNOTATION_APPEND = 0xFFFFFFFF
_HALIGN = {"left": 0, "center": 1, "centre": 1, "right": 2}
# spectra::BarMode wire values.
_BAR_MODES = {"grouped": 0, "stacked": 1, "overlay": 2}

_VALIGN = {"top": 0, "middle": 1, "center": 1, "centre": 1, "bottom": 2, "baseline": 2}


def _rgba(c) -> Optional[Tuple[float, ...]]:
    """RGBA tuple from None / "none" / an RGB(A) sequence of 0..1 floats."""
    if c is None or (isinstance(c, str) and c.lower() == "none"):
        return None
    if isinstance(c, str):
        from ._colors import to_rgba

        c = to_rgba(c)
    vals = tuple(float(v) for v in c)
    if len(vals) not in (3, 4):
        raise ValueError(f"expected an RGB or RGBA colour, got {c!r}")
    return vals if len(vals) == 4 else vals + (1.0,)


def _parse_align(value, table: dict, what: str) -> int:
    if isinstance(value, int) and 0 <= value <= 2:
        return value
    key = str(value).lower()
    if key not in table:
        raise ValueError(f"invalid {what} {value!r}; expected one of {sorted(set(table))}")
    return table[key]


class TextAnnotation:
    """Proxy for a text annotation on a 2D axes (see :meth:`Axes.text`).

    Setters send the full annotation state, packed like
    ``spectra::ipc::pack_annotation()`` in src/ipc/annotation_state.hpp.
    """

    __slots__ = (
        "_axes",
        "_index",
        "_x",
        "_y",
        "_text",
        "_color",
        "_font_size",
        "_halign",
        "_valign",
        "_arrow",
        "_visible",
        "_bbox",
        "_halo",
        "_halo_color",
        "_halo_width",
        "_rotation",
    )

    def __init__(self, axes: Axes, index: int, x: float, y: float, text: str) -> None:
        self._axes = axes
        self._index = index
        self._x = float(x)
        self._y = float(y)
        self._text = str(text)
        self._color: Optional[Tuple[float, ...]] = None
        self._font_size = 0.0
        self._halign = 0
        self._valign = 2
        self._arrow: Optional[Tuple[float, float]] = None
        self._visible = True
        # Background box: None, or (pad px, fill RGBA or None, edge RGBA or None, edge width).
        self._bbox: Optional[Tuple[float, Optional[Tuple[float, ...]], Optional[Tuple[float, ...]], float]] = None
        self._halo = False
        self._halo_color: Optional[Tuple[float, ...]] = None
        self._halo_width = 2.0
        self._rotation = 0.0

    def _set_style(self, bbox, halo, rotation) -> None:
        """Store bbox / halo / rotation arguments (see Axes.text)."""
        if bbox is None or bbox is False:
            self._bbox = None
        elif bbox is True:
            self._bbox = (4.0, None, None, 1.0)
        elif isinstance(bbox, dict):
            fc = bbox.get("facecolor", bbox.get("fc"))
            ec = bbox.get("edgecolor", bbox.get("ec"))
            lw = bbox.get("linewidth", bbox.get("lw", 1.0))
            self._bbox = (
                float(bbox.get("pad", 4.0)),
                _rgba(fc),
                _rgba(ec),
                0.0 if ec in ("none", "None") else float(lw),
            )
        else:
            raise TypeError("bbox must be None, True/False or a dict")
        if halo is None or halo is False:
            self._halo = False
        elif halo is True:
            self._halo = True
        else:
            self._halo = True
            self._halo_color = _rgba(halo)
        self._rotation = float(rotation or 0.0)

    @property
    def index(self) -> int:
        return self._index

    @property
    def text(self) -> str:
        return self._text

    @property
    def position(self) -> Tuple[float, float]:
        return (self._x, self._y)

    def _packed(self) -> List[float]:
        c = self._color
        r, g, b, a = (tuple(c) + (1.0,))[:4] if c is not None else (0.0, 0.0, 0.0, 1.0)
        ax, ay = self._arrow if self._arrow is not None else (0.0, 0.0)
        return [
            self._x,
            self._y,
            1.0 if c is not None else 0.0,
            float(r),
            float(g),
            float(b),
            float(a),
            self._font_size,
            float(self._halign),
            float(self._valign),
            1.0 if self._arrow is not None else 0.0,
            float(ax),
            float(ay),
            1.0 if self._visible else 0.0,
        ] + self._packed_style()

    def _packed_style(self) -> List[float]:
        """Optional style tail of ipc::pack_annotation() (box, halo, rotation)."""
        if self._bbox is None and not self._halo and self._rotation == 0.0:
            return []

        def rgba(c):
            return list((tuple(c) + (1.0,))[:4]) if c is not None else [0.0, 0.0, 0.0, 1.0]

        pad, fill, edge, edge_w = self._bbox if self._bbox is not None else (4.0, None, None, 1.0)
        return (
            [1.0 if self._bbox is not None else 0.0, float(pad)]
            + [1.0 if fill is not None else 0.0] + rgba(fill)
            + [1.0 if edge is not None else 0.0] + rgba(edge)
            + [float(edge_w), 1.0 if self._halo else 0.0]
            + [1.0 if self._halo_color is not None else 0.0] + rgba(self._halo_color)
            + [float(self._halo_width), float(self._rotation)]
        )

    def _send(self, append: bool = False) -> None:
        if self._index < 0:
            raise RuntimeError("annotation was removed by clear_annotations()")
        payload = codec.encode_req_update_property(
            figure_id=self._axes._figure_id,
            axes_index=self._axes._index,
            series_index=_ANNOTATION_APPEND if append else self._index,
            prop="annotation",
            str_val=self._text,
            data=self._packed(),
        )
        self._axes._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_text(self, text: str) -> "TextAnnotation":
        self._text = str(text)
        self._send()
        return self

    def set_position(self, x: float, y: float) -> "TextAnnotation":
        self._x, self._y = float(x), float(y)
        self._send()
        return self

    def set_color(self, r: float, g: float, b: float, a: float = 1.0) -> "TextAnnotation":
        self._color = (float(r), float(g), float(b), float(a))
        self._send()
        return self

    def set_fontsize(self, px: float) -> "TextAnnotation":
        self._font_size = max(0.0, float(px))
        self._send()
        return self

    def set_alignment(self, ha: str = "left", va: str = "bottom") -> "TextAnnotation":
        self._halign = _parse_align(ha, _HALIGN, "ha")
        self._valign = _parse_align(va, _VALIGN, "va")
        self._send()
        return self

    def set_arrow(self, x: Optional[float], y: Optional[float] = None) -> "TextAnnotation":
        """Point an arrow at (x, y); ``set_arrow(None)`` removes it."""
        self._arrow = None if x is None else (float(x), float(y if y is not None else 0.0))
        self._send()
        return self

    def set_visible(self, visible: bool = True) -> "TextAnnotation":
        self._visible = bool(visible)
        self._send()
        return self

    def set_bbox(self, bbox=True) -> "TextAnnotation":
        """Background box: True (theme default), False/None (none) or a dict
        with ``pad``, ``facecolor``, ``edgecolor``, ``linewidth``."""
        self._set_style(bbox, self._halo_color or self._halo, self._rotation)
        self._send()
        return self

    def set_halo(self, halo=True, width: Optional[float] = None) -> "TextAnnotation":
        """Glyph outline: True (theme background), an RGBA tuple, or False."""
        self._halo_color = None
        bb = self._bbox
        self._set_style(None, halo, self._rotation)
        self._bbox = bb
        if width is not None:
            self._halo_width = max(0.0, float(width))
        self._send()
        return self

    def set_rotation(self, degrees: float) -> "TextAnnotation":
        """Rotation in degrees, counter-clockwise about the anchor."""
        self._rotation = float(degrees)
        self._send()
        return self

    def __repr__(self) -> str:
        return f"TextAnnotation(index={self._index}, text={self._text!r}, x={self._x}, y={self._y})"


class _AxesBatchContext:
    """Collects property updates and sends them as a single REQ_UPDATE_BATCH."""

    __slots__ = ("_axes", "_updates")

    def __init__(self, axes: Axes) -> None:
        self._axes = axes
        self._updates: list = []

    def __enter__(self) -> "_AxesBatchContext":
        return self

    def __exit__(self, *args) -> None:
        if self._updates:
            self._axes._session.batch_update(self._updates)
        self._updates.clear()

    def _add(self, **kwargs) -> None:
        kwargs.setdefault("figure_id", self._axes._figure_id)
        kwargs.setdefault("axes_index", self._axes._index)
        self._updates.append(kwargs)

    def set_xlim(self, xmin: float, xmax: float) -> None:
        self._add(prop="xlim", f1=xmin, f2=xmax)

    def set_ylim(self, ymin: float, ymax: float) -> None:
        self._add(prop="ylim", f1=ymin, f2=ymax)

    def set_xlabel(self, label: str) -> None:
        self._add(prop="xlabel", str_val=label)

    def set_ylabel(self, label: str) -> None:
        self._add(prop="ylabel", str_val=label)

    def set_title(self, title: str) -> None:
        self._add(prop="axes_title", str_val=title)

    def grid(self, visible: bool = True) -> None:
        self._add(prop="grid", bool_val=visible)

    def legend(self, visible: bool = True) -> None:
        self._add(prop="legend", bool_val=visible)
