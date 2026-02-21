"""Series proxy — lightweight handle to a data series within a figure."""

from __future__ import annotations

from typing import TYPE_CHECKING, List, Union

from . import _protocol as P
from . import _codec as codec

if TYPE_CHECKING:
    from ._session import Session


def _to_float_list(data: Union[List[float], "object"]) -> List[float]:
    """Convert data to a list of floats. Supports lists and numpy arrays."""
    if isinstance(data, list):
        return [float(v) for v in data]
    # numpy array path
    try:
        import numpy as np

        if isinstance(data, np.ndarray):
            return data.astype(np.float32).tolist()
    except ImportError:
        pass
    # Generic iterable fallback
    return [float(v) for v in data]


def _interleave_xy(
    x: Union[List[float], "object"],
    y: Union[List[float], "object"],
) -> List[float]:
    """Interleave x and y into [x0, y0, x1, y1, ...]."""
    xf = _to_float_list(x)
    yf = _to_float_list(y)
    if len(xf) != len(yf):
        raise ValueError(f"x and y must have same length ({len(xf)} vs {len(yf)})")
    result = []
    for xi, yi in zip(xf, yf):
        result.append(xi)
        result.append(yi)
    return result


def _try_interleave_numpy(
    x: Union[List[float], "object"],
    y: Union[List[float], "object"],
) -> tuple:
    """Try to interleave using numpy for zero-copy. Returns (raw_bytes, count) or None."""
    try:
        import numpy as np

        if isinstance(x, np.ndarray) and isinstance(y, np.ndarray):
            xf = np.ascontiguousarray(x, dtype=np.float32)
            yf = np.ascontiguousarray(y, dtype=np.float32)
            if xf.shape != yf.shape:
                return None
            # Interleave: stack columns then flatten
            interleaved = np.column_stack((xf, yf)).astype(np.float32)
            raw = interleaved.tobytes()
            count = interleaved.size  # total float count
            return raw, count
    except ImportError:
        pass
    return None


class Series:
    """Proxy for a data series within a figure.

    All mutations are sent to the backend via IPC.
    """

    __slots__ = ("_session", "_figure_id", "_index", "_type", "_label")

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
        """
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

    def append(
        self,
        x: Union[List[float], "object"],
        y: Union[List[float], "object"],
    ) -> None:
        """Append x/y data points to this series (streaming)."""
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
        xf = _to_float_list(x)
        yf = _to_float_list(y)
        zf = _to_float_list(z)
        n = min(len(xf), len(yf), len(zf))
        interleaved: List[float] = []
        for i in range(n):
            interleaved.append(xf[i])
            interleaved.append(yf[i])
            interleaved.append(zf[i])
        payload = codec.encode_req_set_data(
            figure_id=self._figure_id,
            series_index=self._index,
            data=interleaved,
        )
        self._session._request(P.REQ_SET_DATA, payload)

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
