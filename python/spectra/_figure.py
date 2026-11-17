"""Figure proxy — lightweight handle to a backend-owned figure."""

from __future__ import annotations

from typing import TYPE_CHECKING, List, Optional

from . import _protocol as P
from . import _codec as codec

if TYPE_CHECKING:
    from ._session import Session
    from ._axes import Axes


# Share modes of Figure.set_layout(sharex=..., sharey=...), as spectra::ShareAxes.
_SHARE_MODES = {False: 0, "none": 0, True: 1, "all": 1, "row": 2, "col": 3}


def share_mode(mode) -> int:
    """matplotlib sharex/sharey value (True/"all", "row", "col", False/"none") -> ShareAxes."""
    if mode is None:
        return 0
    key = mode.lower() if isinstance(mode, str) else bool(mode)
    if key not in _SHARE_MODES:
        raise ValueError(f"share mode must be True, False, 'all', 'row', 'col' or 'none', got {mode!r}")
    return _SHARE_MODES[key]


class Figure:
    """Proxy for a figure managed by the spectra-backend.

    All mutations are sent to the backend via IPC.
    """

    __slots__ = (
        "_session",
        "_id",
        "_title",
        "_axes_list",
        "_visible",
        "_shown_once",
        "_window_id",
        "_suptitle",
        "_layout",
        "_size",
        "_style",
    )

    def __init__(self, session: Session, figure_id: int, title: str = "") -> None:
        self._session = session
        self._id = figure_id
        self._title = title
        self._axes_list: List[Axes] = []
        self._visible = False
        self._shown_once = False
        self._window_id: int = 0
        self._suptitle = ""
        self._layout = [True, 0, 0]  # constrained, sharex, sharey
        self._size = None  # (width, height) the figure was created with, if known
        self._style: Optional[str] = None

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

    @property
    def style(self) -> Optional[str]:
        """Name of the plot style applied with :meth:`apply_style`, or ``None``."""
        return self._style

    def apply_style(self, name: str) -> None:
        """Apply the built-in plot style ``name`` to this figure.

        ``"default"``, ``"paper"``, ``"talk"``, ``"minimal"`` or ``"dark-paper"``
        (see :mod:`spectra.style`).  Existing axes and series are restyled; axes
        and series added later follow the style too.
        """
        from . import style as _style

        _style._check(name)
        payload = codec.encode_req_update_property(
            figure_id=self._id,
            prop="style",
            str_val=name,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)
        self._style = name

    def suptitle(self, text: str) -> None:
        """Set the figure title drawn centred above the subplot grid ("" removes it)."""
        text = str(text)
        payload = codec.encode_req_update_property(
            figure_id=self._id,
            prop="suptitle",
            str_val=text,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)
        self._suptitle = text

    def get_suptitle(self) -> str:
        return self._suptitle

    def set_layout(self, constrained: Optional[bool] = None, sharex=None, sharey=None) -> None:
        """Change the subplot layout.

        ``constrained`` sizes margins and gaps from the labels so they never
        overlap (on by default).  ``sharex`` / ``sharey`` link axis limits
        between subplots like matplotlib: ``True``/``"all"``, ``"row"``,
        ``"col"`` or ``False``/``"none"``; arguments left as None keep their value.
        """
        if constrained is not None:
            self._layout[0] = bool(constrained)
        for i, mode in ((1, sharex), (2, sharey)):
            if mode is not None:
                self._layout[i] = share_mode(mode)
        payload = codec.encode_req_update_property(
            figure_id=self._id,
            prop="figure_layout",
            f1=1.0 if self._layout[0] else 0.0,
            f2=float(self._layout[1]),
            f3=float(self._layout[2]),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def tight_layout(self) -> None:
        """Turn on constrained layout (labels sized into the subplot spacing)."""
        self.set_layout(constrained=True)

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
        ax._figure_axes = self._axes_list
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
        ax._figure_axes = self._axes_list
        self._axes_list.append(ax)
        return ax

    @property
    def axes(self) -> List[Axes]:
        return list(self._axes_list)

    @property
    def is_visible(self) -> bool:
        """Whether this figure's window is currently open."""
        return self._visible

    @property
    def window_id(self) -> int:
        """The window ID this figure is displayed in (0 if not shown yet)."""
        return self._window_id

    def show(self, window_id: int = 0) -> None:
        """Show this figure in a window.

        Args:
            window_id: If non-zero, add this figure as a tab in the specified
                       window instead of spawning a new window. Use
                       ``other_fig.window_id`` to get a valid window ID.
        """
        payload = codec.encode_req_show(self._id, window_id=window_id)
        resp = self._session._request(P.REQ_SHOW, payload)
        # Backend returns the window_id in the response header
        resp_wid = resp.get("header", {}).get("window_id", 0)
        if resp_wid:
            self._window_id = resp_wid
        self._visible = True
        self._shown_once = True

    def savefig(
        self,
        path,
        width: int = 0,
        height: int = 0,
        format: Optional[str] = None,
        theme: str = "",
        dpi=None,
    ) -> str:
        """Write this figure to a PNG or SVG file and return its absolute path.

        The backend renders the figure offscreen (no window has to be open)
        and replies once the file is written.  ``format`` is "png" or "svg";
        by default it follows the file extension (PNG unless ``.svg``).
        ``width`` / ``height`` set the PNG size in pixels (0 = figure size);
        ``theme`` (e.g. "light") overrides the saved theme for this file.
        ``dpi`` is accepted for matplotlib compatibility and ignored.

        Raises :class:`~spectra.BackendError` when the backend cannot write
        the file (bad directory, no GPU/Vulkan driver, ...).
        """
        import os

        target = os.path.abspath(os.path.expanduser(os.fspath(path)))
        fmt = (format or "").lower()
        if fmt not in ("", "png", "svg"):
            raise ValueError(f"unsupported format {format!r}; use 'png' or 'svg'")
        if int(width) < 0 or int(height) < 0:
            raise ValueError("width and height must be >= 0")
        payload = codec.encode_req_export_figure(
            figure_id=self._id,
            path=target,
            fmt=fmt,
            width=int(width),
            height=int(height),
            theme=theme,
        )
        self._session._request(P.REQ_EXPORT_FIGURE, payload)
        return target

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

    def to_png(self) -> Optional[bytes]:
        """PNG bytes of this figure rendered offscreen (None if it cannot be rendered).

        Uses the inline settings of :func:`spectra.use_inline` (size, theme).
        """
        from ._inline import render_png

        return render_png(self)

    def _repr_png_(self) -> Optional[bytes]:
        """IPython/Jupyter rich display: the figure as an inline PNG image."""
        return self.to_png()

    def __repr__(self) -> str:
        return f"Figure(id={self._id}, title={self._title!r})"
