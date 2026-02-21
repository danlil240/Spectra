"""Axes proxy — lightweight handle to axes within a figure."""

from __future__ import annotations

from typing import TYPE_CHECKING, List, Union

from . import _protocol as P
from . import _codec as codec

if TYPE_CHECKING:
    from ._session import Session
    from ._series import Series


class Axes:
    """Proxy for an axes object within a figure.

    All mutations are sent to the backend via IPC.
    """

    __slots__ = ("_session", "_figure_id", "_index", "_series_list", "_is_3d")

    def __init__(self, session: Session, figure_id: int, axes_index: int, is_3d: bool = False) -> None:
        self._session = session
        self._figure_id = figure_id
        self._index = axes_index
        self._series_list: List[Series] = []
        self._is_3d = is_3d

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

    def remove_series(self, index: int) -> None:
        """Remove a series by index from this axes."""
        payload = codec.encode_req_remove_series(
            figure_id=self._figure_id,
            series_index=index,
        )
        self._session._request(P.REQ_REMOVE_SERIES, payload)
        self._series_list = [s for s in self._series_list if s._index != index]

    def clear(self) -> None:
        """Remove all series from this axes."""
        # Remove in reverse order so indices stay valid
        for s in reversed(list(self._series_list)):
            payload = codec.encode_req_remove_series(
                figure_id=self._figure_id,
                series_index=s._index,
            )
            self._session._request(P.REQ_REMOVE_SERIES, payload)
        self._series_list.clear()

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
