"""Series proxy — lightweight handle to a data series within a figure."""

from __future__ import annotations

import math
import struct
from typing import TYPE_CHECKING, List, Union

from . import _protocol as P
from . import _codec as codec

if TYPE_CHECKING:
    from ._session import Session


# Series types whose data is a structured payload (not interleaved x, y): the
# backend rebuilds the native C++ series from it (src/ui/app/figure_snapshot.hpp).
STATS_SERIES_TYPES = frozenset({"bar", "histogram", "band", "stem", "box", "violin"})


def _numpy():
    """The numpy module, or None when it is not installed (numpy is optional)."""
    try:
        import numpy as np
    except ImportError:
        return None
    return np


def _to_float_list(data: Union[List[float], "object"]) -> List[float]:
    """Convert data to a list of floats. Supports lists and numpy arrays."""
    if isinstance(data, list):
        return [float(v) for v in data]
    np = _numpy()
    if np is not None and isinstance(data, np.ndarray):
        return data.astype(np.float32).tolist()
    # Generic iterable fallback
    return [float(v) for v in data]


def _as_float64(data):
    """``data`` as a flat float64 NumPy array, or None (no numpy / not numeric)."""
    np = _numpy()
    if np is None:
        return None
    if isinstance(data, np.ndarray) and data.dtype == np.float64 and data.ndim == 1:
        return data
    try:
        return np.asarray(data, dtype=np.float64).ravel()
    except (TypeError, ValueError):
        return None


def _interleave_xy(
    x: Union[List[float], "object"],
    y: Union[List[float], "object"],
) -> List[float]:
    """Interleave x and y into [x0, y0, x1, y1, ...] (pure-Python fallback)."""
    xf = _to_float_list(x)
    yf = _to_float_list(y)
    if len(xf) != len(yf):
        raise ValueError(f"x and y must have same length ({len(xf)} vs {len(yf)})")
    result = [0.0] * (2 * len(xf))
    result[0::2] = xf
    result[1::2] = yf
    return result


def _interleave_numpy(*columns):
    """Interleave equal-length columns into one contiguous little-endian float32 array."""
    np = _numpy()
    n = len(columns[0])
    out = np.empty((n, len(columns)), dtype="<f4")
    with np.errstate(over="ignore", invalid="ignore"):
        for i, c in enumerate(columns):
            out[:, i] = c
    return out.ravel()


def _try_interleave_numpy(
    x: Union[List[float], "object"],
    y: Union[List[float], "object"],
) -> tuple:
    """Interleave two NumPy arrays into float32 bytes. Returns (raw_bytes, count) or None.

    None when numpy is missing, either input is not an ndarray or the lengths
    differ (the caller then falls back to the list path, which reports the
    mismatch).  Series.set_data()/append() convert inputs to float64 arrays
    first whenever numpy is installed, so a multi-million-point series costs
    one vectorised copy instead of a Python loop.
    """
    np = _numpy()
    if np is None or not isinstance(x, np.ndarray) or not isinstance(y, np.ndarray):
        return None
    xf = x.ravel()
    yf = y.ravel()
    if xf.shape != yf.shape:
        return None
    interleaved = _interleave_numpy(xf, yf)
    return interleaved.tobytes(), int(interleaved.size)


def _float32_exact(v: float) -> bool:
    try:
        return struct.unpack("<f", struct.pack("<f", v))[0] == v
    except (OverflowError, struct.error):
        return False


def _choose_data_origin(values) -> float:
    """Origin for storing ``values`` as float32 offsets without losing precision.

    Python port of ``detail::choose_data_origin`` (src/core/data_origin.hpp):
    0 while float32 holds the values well relative to their spread, otherwise
    the centre of their range (e.g. for epoch timestamps).
    """
    lo = hi = 0.0
    found = False
    try:
        import numpy as np

        if isinstance(values, np.ndarray):
            v = np.asarray(values, dtype=np.float64)
            v = v[np.isfinite(v)]
            if v.size:
                lo, hi, found = float(v.min()), float(v.max()), True
            values = ()
    except ImportError:
        pass
    for v in values:
        v = float(v)
        if not math.isfinite(v):
            continue
        lo = min(lo, v) if found else v
        hi = max(hi, v) if found else v
        found = True
    if not found:
        return 0.0
    rng = hi - lo
    if max(abs(lo), abs(hi)) <= 64.0 * rng:
        return 0.0
    if rng == 0.0 and _float32_exact(lo):
        return 0.0
    return lo + 0.5 * rng


def _choose_stream_origin(first_value: float) -> float:
    """Port of ``detail::choose_stream_origin``: values >= 1000 become the origin."""
    v = float(first_value)
    if not math.isfinite(v) or abs(v) < 1000.0:
        return 0.0
    return v


def _shift(values, origin: float):
    """``values - origin`` in double precision (list or float64 numpy array)."""
    try:
        import numpy as np

        if isinstance(values, np.ndarray):
            arr = np.asarray(values, dtype=np.float64)
            return arr - origin if origin != 0.0 else arr
    except ImportError:
        pass
    vals = [float(v) for v in values]
    return [v - origin for v in vals] if origin != 0.0 else vals


class Series:
    """Proxy for a data series within a figure.

    All mutations are sent to the backend via IPC.
    """

    __slots__ = ("_session", "_figure_id", "_index", "_type", "_label", "_origin", "_has_data")

    def __init__(
        self,
        session: Session,
        figure_id: int,
        series_index: int,
        series_type: str,
        label: str = "",
    ) -> None:
        self._session = session
        self._figure_id = figure_id
        self._index = series_index
        self._type = series_type
        self._label = label
        # Double-precision data origins (x, y): values are sent as float32
        # offsets from them, so large values such as epoch timestamps keep
        # their precision.  Mirrors the backend's per-series x/y origin.
        self._origin = (0.0, 0.0)
        self._has_data = False

    @property
    def index(self) -> int:
        return self._index

    @property
    def figure_id(self) -> int:
        return self._figure_id

    @property
    def series_type(self) -> str:
        return self._type

    @property
    def label(self) -> str:
        return self._label

    def set_data(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
    ) -> None:
        """Set the x/y data for this series.

        For arrays exceeding CHUNK_SIZE (~128 MiB), data is automatically
        split into multiple chunked REQ_SET_DATA messages. The backend
        reassembles chunks before applying to the figure model.

        Values too large for float32 precision (e.g. epoch timestamps) are
        sent as offsets from a double-precision origin, which the backend
        keeps with the series.
        """
        xa, ya = _as_float64(x), _as_float64(y)
        if xa is not None and ya is not None:
            x, y = xa, ya  # vectorised origin / shift / interleave below
        origin = (_choose_data_origin(x), _choose_data_origin(y))
        x = _shift(x, origin[0])
        y = _shift(y, origin[1])
        self._send_set_data(x, y)
        # The backend resets a series' origin on new data: send it afterwards.
        if origin != (0.0, 0.0):
            self._send_origin(origin)
        self._origin = origin
        self._has_data = len(x) > 0

    def _send_origin(self, origin) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="data_origin",
            f1=origin[0],
            f2=origin[1],
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def _send_set_data(self, x, y) -> None:
        # Try numpy fast path
        np_result = _try_interleave_numpy(x, y)
        if np_result is not None:
            raw_bytes, count = np_result
            # Check if chunking is needed
            if len(raw_bytes) > P.CHUNK_SIZE:
                self._send_chunked(raw_bytes, count)
                return
            payload = codec.encode_req_set_data_raw(
                figure_id=self._figure_id,
                series_index=self._index,
                raw_bytes=raw_bytes,
                count=count,
            )
        else:
            interleaved = _interleave_xy(x, y)
            payload = codec.encode_req_set_data(
                figure_id=self._figure_id,
                series_index=self._index,
                data=interleaved,
            )

        self._session._request(P.REQ_SET_DATA, payload)

    def _send_chunked(self, raw_bytes: bytes, total_count: int) -> None:
        """Send data in multiple chunks for arrays exceeding CHUNK_SIZE."""
        import math

        chunk_size = P.CHUNK_SIZE
        num_chunks = math.ceil(len(raw_bytes) / chunk_size)

        for i in range(num_chunks):
            start = i * chunk_size
            end = min(start + chunk_size, len(raw_bytes))
            chunk_bytes = raw_bytes[start:end]
            # Each float is 4 bytes
            chunk_float_count = len(chunk_bytes) // 4

            payload = codec.encode_req_set_data_chunked(
                figure_id=self._figure_id,
                series_index=self._index,
                raw_bytes=chunk_bytes,
                count=chunk_float_count,
                chunk_index=i,
                chunk_count=num_chunks,
                total_count=total_count,
            )
            self._session._request(P.REQ_SET_DATA, payload)

    def _set_payload(self, data: List[float]) -> None:
        """Send a statistical series' structured payload.

        ``data`` follows the per-type float layout documented in
        src/ui/app/figure_snapshot.hpp ("bar", "histogram", "band", "stem", ...);
        the backend stores it as-is and the window rebuilds the native series.
        """
        try:
            import numpy as np

            arr = np.ascontiguousarray(data, dtype=np.float32)
            payload = codec.encode_req_set_data_raw(
                figure_id=self._figure_id,
                series_index=self._index,
                raw_bytes=arr.tobytes(),
                count=int(arr.size),
            )
        except ImportError:
            payload = codec.encode_req_set_data(
                figure_id=self._figure_id,
                series_index=self._index,
                data=[float(v) for v in data],
            )
        self._session._request(P.REQ_SET_DATA, payload)
        self._has_data = bool(data)

    def append(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
    ) -> None:
        """Append x/y data points to this series (streaming).

        Values are sent relative to the series' data origin (see set_data);
        the first points streamed into an empty series pick that origin.
        """
        if self._type in STATS_SERIES_TYPES:
            raise TypeError(f"append() is not supported for {self._type!r} series; "
                            "create the series again with the full data")
        xa, ya = _as_float64(x), _as_float64(y)
        if xa is not None and ya is not None:
            x, y = xa, ya
        if not self._has_data:
            xs = _shift(x, 0.0)
            ys = _shift(y, 0.0)
            if len(xs) and len(ys):
                origin = (_choose_stream_origin(xs[0]), _choose_stream_origin(ys[0]))
                if origin != (0.0, 0.0):
                    self._send_origin(origin)
                self._origin = origin
                self._has_data = True
            x, y = xs, ys
        x = _shift(x, self._origin[0])
        y = _shift(y, self._origin[1])
        # Try numpy fast path
        np_result = _try_interleave_numpy(x, y)
        if np_result is not None:
            raw_bytes, count = np_result
            payload = codec.encode_req_append_data_raw(
                figure_id=self._figure_id,
                series_index=self._index,
                raw_bytes=raw_bytes,
                count=count,
            )
        else:
            interleaved = _interleave_xy(x, y)
            payload = codec.encode_req_append_data(
                figure_id=self._figure_id,
                series_index=self._index,
                data=interleaved,
            )

        self._session._request(P.REQ_APPEND_DATA, payload)

    def set_data_xyz(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
        z: Union[List[float], "object"],
    ) -> None:
        """Set XYZ data for 3D series. Sends as interleaved [x0,y0,z0, x1,y1,z1, ...]."""
        xa, ya, za = _as_float64(x), _as_float64(y), _as_float64(z)
        if xa is not None and ya is not None and za is not None:
            n = min(xa.size, ya.size, za.size)
            data = _interleave_numpy(xa[:n], ya[:n], za[:n])
        else:
            xf = _to_float_list(x)
            yf = _to_float_list(y)
            zf = _to_float_list(z)
            n = min(len(xf), len(yf), len(zf))
            data = [0.0] * (3 * n)
            data[0::3] = xf[:n]
            data[1::3] = yf[:n]
            data[2::3] = zf[:n]
        payload = codec.encode_req_set_data(
            figure_id=self._figure_id,
            series_index=self._index,
            data=data,
        )
        self._session._request(P.REQ_SET_DATA, payload)

    def _set_categories(self, names: List[str]) -> None:
        """Category names of a bar series (one tick label per bar)."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="categories",
            f1=float(len(names)),
            # 0x1F separates names (ipc::CATEGORY_SEPARATOR); strip it from names.
            str_val="\x1f".join(str(n).replace("\x1f", " ") for n in names),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_label(self, label: str) -> None:
        """Set the series label."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="label",
            str_val=label,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)
        self._label = label

    def set_color(self, r: float, g: float, b: float, a: float = 1.0) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="color",
            f1=r,
            f2=g,
            f3=b,
            f4=a,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_color_letter(self, letter: str) -> None:
        """Colour the series with a MATLAB colour letter (r g b c m y k w).

        Unlike set_color(), the window picks the letter's shade for its theme
        (e.g. 'k' is black on a light plot and light gray on a dark one) and
        re-resolves it when the theme changes.
        """
        if len(letter) != 1 or letter not in "rgbcmykw":
            raise ValueError("color letter must be one of r g b c m y k w, got %r" % (letter,))
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="color_letter",
            str_val=letter,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_palette_index(self, index: int) -> None:
        """Colour the series with slot ``index`` of the data palette ("C<n>").

        Like set_color_letter(), the window resolves the slot from its own
        theme and data palette and re-resolves it when either changes.
        """
        index = int(index)
        if index < 0:
            raise ValueError("palette index must be >= 0, got %d" % index)
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="palette_index",
            f1=float(index),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_line_width(self, width: float) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="line_width",
            f1=width,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_marker_size(self, size: float) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="marker_size",
            f1=size,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_line_style(self, style) -> None:
        """Line style: "-", "--", ":", "-.", "-.." or "none" (also "solid",
        "dashed", "dotted", "dashdot")."""
        from ._style import normalize_linestyle

        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="line_style",
            str_val=normalize_linestyle(style),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_marker_style(self, marker) -> None:
        """Marker: "o", ".", "s", "d", "D", "^", "v", "<", ">", "+", "x", "*",
        "p", "h" or "none" (matplotlib names such as "circle" also work)."""
        from ._style import normalize_marker

        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="marker_style",
            str_val=normalize_marker(marker),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_depth_shade(self, enabled: bool = True) -> None:
        """3D scatter / line: fade the parts farther from the camera towards the
        background (matplotlib's ``depthshade``).  On by default for 3D scatter."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="depth_shade",
            bool_val=bool(enabled),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_show_edges(self, enabled: bool = True) -> None:
        """Surface: draw the grid edge lines over the shaded faces."""
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="show_edges",
            bool_val=bool(enabled),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_contour_projection(self, enabled: bool = True, levels: int = 10) -> None:
        """Surface: project ``levels`` contour lines onto the floor of the 3D box
        (MATLAB's ``surfc``)."""
        if enabled and int(levels) < 1:
            raise ValueError("levels must be at least 1")
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="contour_projection",
            f1=float(levels),
            bool_val=bool(enabled),
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_format(self, fmt: str) -> None:
        """Apply a MATLAB / matplotlib format string such as "r--o" or "g^"."""
        from ._style import resolve_style

        color, line, marker = resolve_style(fmt)
        if color is not None:
            self.set_color_letter(color)
        if line is not None:
            self.set_line_style(line)
        if marker is not None:
            self.set_marker_style(marker)

    def set_visible(self, visible: bool) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="visible",
            bool_val=visible,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def set_opacity(self, opacity: float) -> None:
        payload = codec.encode_req_update_property(
            figure_id=self._figure_id,
            series_index=self._index,
            prop="opacity",
            f1=opacity,
        )
        self._session._request(P.REQ_UPDATE_PROPERTY, payload)

    def __repr__(self) -> str:
        return f"Series(figure_id={self._figure_id}, index={self._index}, type={self._type!r})"
