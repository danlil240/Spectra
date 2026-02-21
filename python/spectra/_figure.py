"""Figure proxy — lightweight handle to a backend-owned figure."""

from __future__ import annotations

from typing import TYPE_CHECKING, List

from . import _protocol as P
from . import _codec as codec

if TYPE_CHECKING:
    from ._session import Session
    from ._axes import Axes


class Figure:
    """Proxy for a figure managed by the spectra-backend.

    All mutations are sent to the backend via IPC.
    """

    __slots__ = ("_session", "_id", "_title", "_axes_list", "_visible", "_shown_once")

    def __init__(self, session: Session, figure_id: int, title: str = "") -> None:
        self._session = session
        self._id = figure_id
        self._title = title
        self._axes_list: List[Axes] = []
        self._visible = False
        self._shown_once = False

    @property
    def id(self) -> int:
        return self._id

    @property
    def title(self) -> str:
        return self._title

    @title.setter
    def title(self, value: str) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._id,
            prop="title",
            str_val=value,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)
        self._title = value

    def subplot(self, rows: int, cols: int, index: int) -> Axes:
        """Create or get a subplot axes. Uses 1-based MATLAB-style indexing."""
        from ._axes import Axes

        payload = codec.encode_req_create_axes(
            figure_id=self._id,
            rows=rows,
            cols=cols,
            index=index,
        )
        resp = self._session._request(P.REQ_CREATE_AXES, payload)
        _, axes_index = codec.decode_resp_axes_created(resp["payload"])

        ax = Axes(self._session, self._id, axes_index)
        self._axes_list.append(ax)
        return ax

    def subplot3d(self, rows: int = 1, cols: int = 1, index: int = 1) -> Axes:
        """Create or get a 3D subplot axes. Uses 1-based MATLAB-style indexing."""
        from ._axes import Axes

        payload = codec.encode_req_create_axes(
            figure_id=self._id,
            rows=rows,
            cols=cols,
            index=index,
            is_3d=True,
        )
        resp = self._session._request(P.REQ_CREATE_AXES, payload)
        _, axes_index = codec.decode_resp_axes_created(resp["payload"])

        ax = Axes(self._session, self._id, axes_index, is_3d=True)
        self._axes_list.append(ax)
        return ax

    @property
    def axes(self) -> List[Axes]:
        return list(self._axes_list)

    @property
    def is_visible(self) -> bool:
        """Whether this figure's window is currently open."""
        return self._visible

    def show(self) -> None:
        """Show this figure in a window (spawns an agent process)."""
        payload = codec.encode_req_show(self._id)
        self._session._request(P.REQ_SHOW, payload)
        self._visible = True
        self._shown_once = True

    def close_window(self) -> None:
        """Close this figure's window but keep the figure in the model.

        The figure can be shown again with show().
        """
        payload = codec.encode_req_close_figure(self._id)
        self._session._request(P.REQ_CLOSE_FIGURE, payload)
        self._visible = False

    def close(self) -> None:
        """Destroy this figure (removes from backend model)."""
        payload = codec.encode_req_destroy_figure(self._id)
        self._session._request(P.REQ_DESTROY_FIGURE, payload)
        self._visible = False

    def __repr__(self) -> str:
        return f"Figure(id={self._id}, title={self._title!r})"
