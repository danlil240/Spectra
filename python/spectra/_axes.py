"""Axes proxy — lightweight handle to axes within a figure."""

from __future__ import annotations

from typing import TYPE_CHECKING, List, Optional, Tuple, Union

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

    def set_xlim(self, xmin: float, xmax: float) -> None:
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

    def set_xlabel(self, label: str) -> None:
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
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            axes_index=self._index,
            prop="axes_title",
            str_val=title,
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
    ) -> "TextAnnotation":
        """Place text with its anchor at data coordinates (x, y) (2D axes only).

        ``color`` is an RGBA tuple (None = theme text colour), ``fontsize`` is in
        pixels (0 = axis-label size), ``ha`` is "left"/"center"/"right" and
        ``va`` is "top"/"middle"/"bottom" (alignment relative to the anchor).
        """
        ann = TextAnnotation(self, len(self._annotations), x, y, s)
        ann._color = tuple(color) if color is not None else None
        ann._font_size = float(fontsize)
        ann._halign = _parse_align(ha, _HALIGN, "ha")
        ann._valign = _parse_align(va, _VALIGN, "va")
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
    ) -> "TextAnnotation":
        """Place text centred at ``xytext`` with an arrow pointing to ``xy``.

        Without ``xytext`` the text sits at ``xy`` and no arrow is drawn.
        """
        if xytext is None:
            return self.text(xy[0], xy[1], s, color=color, fontsize=fontsize)
        ann = TextAnnotation(self, len(self._annotations), xytext[0], xytext[1], s)
        ann._color = tuple(color) if color is not None else None
        ann._font_size = float(fontsize)
        ann._halign = 1
        ann._valign = 1
        ann._arrow = (float(xy[0]), float(xy[1]))
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



# "annotation" property: series_index that appends (mirrors the daemon).
_ANNOTATION_APPEND = 0xFFFFFFFF
_HALIGN = {"left": 0, "center": 1, "centre": 1, "right": 2}
_VALIGN = {"top": 0, "middle": 1, "center": 1, "centre": 1, "bottom": 2, "baseline": 2}


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
        ]

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
