"""TLV (Tag-Length-Value) codec mirroring src/ipc/codec.hpp.

Wire format per field: [tag: u8] [len: u32 LE] [data: len bytes]
"""

import struct
from typing import List, Optional, Tuple

from . import _protocol as P


# ─── Encoder ──────────────────────────────────────────────────────────────────

class PayloadEncoder:
    """Builds a TLV byte buffer."""

    __slots__ = ("_buf",)

    def __init__(self) -> None:
        self._buf = bytearray()

    def put_u16(self, tag: int, val: int) -> None:
        self._buf.append(tag & 0xFF)
        self._buf.extend(struct.pack("<I", 2))
        self._buf.extend(struct.pack("<H", val & 0xFFFF))

    def put_u32(self, tag: int, val: int) -> None:
        self._buf.append(tag & 0xFF)
        self._buf.extend(struct.pack("<I", 4))
        self._buf.extend(struct.pack("<I", val & 0xFFFFFFFF))

    def put_u64(self, tag: int, val: int) -> None:
        self._buf.append(tag & 0xFF)
        self._buf.extend(struct.pack("<I", 8))
        self._buf.extend(struct.pack("<Q", val & 0xFFFFFFFFFFFFFFFF))

    def put_string(self, tag: int, val: str) -> None:
        raw = val.encode("utf-8")
        self._buf.append(tag & 0xFF)
        self._buf.extend(struct.pack("<I", len(raw)))
        self._buf.extend(raw)

    def put_float(self, tag: int, val: float) -> None:
        bits = struct.pack("<f", val)
        u32 = struct.unpack("<I", bits)[0]
        self.put_u32(tag, u32)

    def put_double(self, tag: int, val: float) -> None:
        bits = struct.pack("<d", val)
        u64 = struct.unpack("<Q", bits)[0]
        self.put_u64(tag, u64)

    def put_bool(self, tag: int, val: bool) -> None:
        self.put_u16(tag, 1 if val else 0)

    def put_blob(self, tag: int, data: bytes) -> None:
        self._buf.append(tag & 0xFF)
        self._buf.extend(struct.pack("<I", len(data)))
        self._buf.extend(data)

    def put_float_array(self, tag: int, arr: List[float]) -> None:
        """Encode as [count_u32][float0][float1]... wrapped in a blob."""
        count = len(arr)
        raw = struct.pack("<I", count)
        if count > 0:
            raw += struct.pack(f"<{count}f", *arr)
        self.put_blob(tag, raw)

    def take(self) -> bytes:
        return bytes(self._buf)


# ─── Decoder ──────────────────────────────────────────────────────────────────

class PayloadDecoder:
    """Reads TLV fields from a byte buffer."""

    __slots__ = ("_data", "_pos", "_tag", "_len", "_val_offset")

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._pos = 0
        self._tag = 0
        self._len = 0
        self._val_offset = 0

    def next(self) -> bool:
        if self._pos + 5 > len(self._data):
            return False
        self._tag = self._data[self._pos]
        self._len = struct.unpack_from("<I", self._data, self._pos + 1)[0]
        self._val_offset = self._pos + 5
        if self._val_offset + self._len > len(self._data):
            return False
        self._pos = self._val_offset + self._len
        return True

    @property
    def tag(self) -> int:
        return self._tag

    @property
    def field_len(self) -> int:
        return self._len

    def as_u16(self) -> int:
        if self._len < 2:
            return 0
        return struct.unpack_from("<H", self._data, self._val_offset)[0]

    def as_u32(self) -> int:
        if self._len < 4:
            return 0
        return struct.unpack_from("<I", self._data, self._val_offset)[0]

    def as_u64(self) -> int:
        if self._len < 8:
            return 0
        return struct.unpack_from("<Q", self._data, self._val_offset)[0]

    def as_string(self) -> str:
        return self._data[self._val_offset:self._val_offset + self._len].decode("utf-8", errors="replace")

    def as_blob(self) -> bytes:
        return bytes(self._data[self._val_offset:self._val_offset + self._len])

    def as_float(self) -> float:
        bits = self.as_u32()
        return struct.unpack("<f", struct.pack("<I", bits))[0]

    def as_double(self) -> float:
        bits = self.as_u64()
        return struct.unpack("<d", struct.pack("<Q", bits))[0]

    def as_bool(self) -> bool:
        return self.as_u16() != 0

    def as_float_array(self) -> List[float]:
        raw = self.as_blob()
        if len(raw) < 4:
            return []
        count = struct.unpack_from("<I", raw, 0)[0]
        if len(raw) < 4 + count * 4:
            return []
        return list(struct.unpack_from(f"<{count}f", raw, 4))


# ─── Header encode/decode ─────────────────────────────────────────────────────

def encode_header(
    msg_type: int,
    payload_len: int,
    seq: int = 0,
    request_id: int = 0,
    session_id: int = 0,
    window_id: int = 0,
) -> bytes:
    return struct.pack(
        P.HEADER_FMT,
        P.MAGIC,
        msg_type,
        payload_len,
        seq,
        request_id,
        session_id,
        window_id,
    )


def decode_header(data: bytes) -> Optional[dict]:
    if len(data) < P.HEADER_SIZE:
        return None
    magic, msg_type, payload_len, seq, request_id, session_id, window_id = (
        struct.unpack_from(P.HEADER_FMT, data, 0)
    )
    if magic != P.MAGIC:
        return None
    return {
        "type": msg_type,
        "payload_len": payload_len,
        "seq": seq,
        "request_id": request_id,
        "session_id": session_id,
        "window_id": window_id,
    }


# ─── Convenience: encode specific payloads ────────────────────────────────────

def encode_hello(client_type: str = "python", build: str = "") -> bytes:
    enc = PayloadEncoder()
    enc.put_u16(P.TAG_PROTOCOL_MAJOR, P.PROTOCOL_MAJOR)
    enc.put_u16(P.TAG_PROTOCOL_MINOR, P.PROTOCOL_MINOR)
    enc.put_string(P.TAG_AGENT_BUILD, build)
    enc.put_u32(P.TAG_CAPABILITIES, 0)
    enc.put_string(P.TAG_CLIENT_TYPE, client_type)
    return enc.take()


def decode_welcome(data: bytes) -> dict:
    result = {"session_id": 0, "window_id": 0, "process_id": 0, "heartbeat_ms": 5000, "mode": ""}
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_SESSION_ID:
            result["session_id"] = dec.as_u64()
        elif t == P.TAG_WINDOW_ID:
            result["window_id"] = dec.as_u64()
        elif t == P.TAG_PROCESS_ID:
            result["process_id"] = dec.as_u64()
        elif t == P.TAG_HEARTBEAT_MS:
            result["heartbeat_ms"] = dec.as_u32()
        elif t == P.TAG_MODE:
            result["mode"] = dec.as_string()
    return result


def encode_req_create_figure(title: str = "", width: int = 1280, height: int = 720) -> bytes:
    enc = PayloadEncoder()
    enc.put_string(P.TAG_TITLE, title)
    enc.put_u32(P.TAG_WIDTH, width)
    enc.put_u32(P.TAG_HEIGHT, height)
    return enc.take()


def encode_req_create_axes(figure_id: int, rows: int, cols: int, index: int, is_3d: bool = False) -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_GRID_ROWS, rows)
    enc.put_u32(P.TAG_GRID_COLS, cols)
    enc.put_u32(P.TAG_GRID_INDEX, index)
    if is_3d:
        enc.put_bool(P.TAG_IS_3D, True)
    return enc.take()


def encode_req_add_series(figure_id: int, axes_index: int, series_type: str, label: str = "") -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_AXES_INDEX, axes_index)
    enc.put_string(P.TAG_SERIES_TYPE, series_type)
    enc.put_string(P.TAG_SERIES_LABEL, label)
    return enc.take()


def encode_req_set_data(figure_id: int, series_index: int, data: List[float], dtype: int = 0) -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    enc.put_u16(P.TAG_DTYPE, dtype)
    if data:
        enc.put_float_array(P.TAG_BLOB_INLINE, data)
    return enc.take()


def encode_req_set_data_raw(figure_id: int, series_index: int, raw_bytes: bytes, count: int, dtype: int = 0) -> bytes:
    """Encode REQ_SET_DATA with pre-packed float array bytes for zero-copy from numpy."""
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    enc.put_u16(P.TAG_DTYPE, dtype)
    # Build the float_array blob: [count_u32][raw float bytes]
    blob = struct.pack("<I", count) + raw_bytes
    enc.put_blob(P.TAG_BLOB_INLINE, blob)
    return enc.take()


def encode_req_set_data_chunked(
    figure_id: int,
    series_index: int,
    raw_bytes: bytes,
    count: int,
    chunk_index: int,
    chunk_count: int,
    total_count: int,
    dtype: int = 0,
) -> bytes:
    """Encode a single chunk of a chunked REQ_SET_DATA transfer.

    For arrays exceeding CHUNK_SIZE, the caller splits the data and sends
    multiple messages with chunk_index in [0, chunk_count).
    The backend reassembles chunks before applying to FigureModel.
    """
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    enc.put_u16(P.TAG_DTYPE, dtype)
    enc.put_u32(P.TAG_CHUNK_INDEX, chunk_index)
    enc.put_u32(P.TAG_CHUNK_COUNT, chunk_count)
    enc.put_u32(P.TAG_TOTAL_COUNT, total_count)
    blob = struct.pack("<I", count) + raw_bytes
    enc.put_blob(P.TAG_BLOB_INLINE, blob)
    return enc.take()


def encode_req_append_data(figure_id: int, series_index: int, data: List[float]) -> bytes:
    """Encode REQ_APPEND_DATA for streaming append."""
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    if data:
        enc.put_float_array(P.TAG_BLOB_INLINE, data)
    return enc.take()


def encode_req_append_data_raw(figure_id: int, series_index: int, raw_bytes: bytes, count: int) -> bytes:
    """Encode REQ_APPEND_DATA with pre-packed float array bytes for zero-copy from numpy."""
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    blob = struct.pack("<I", count) + raw_bytes
    enc.put_blob(P.TAG_BLOB_INLINE, blob)
    return enc.take()


def encode_req_update_property(
    figure_id: int,
    axes_index: int = 0,
    series_index: int = 0,
    prop: str = "",
    f1: float = 0.0,
    f2: float = 0.0,
    f3: float = 0.0,
    f4: float = 0.0,
    bool_val: bool = False,
    str_val: str = "",
) -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_AXES_INDEX, axes_index)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    enc.put_string(P.TAG_PROPERTY_NAME, prop)
    enc.put_double(P.TAG_F1, f1)
    enc.put_double(P.TAG_F2, f2)
    enc.put_double(P.TAG_F3, f3)
    enc.put_double(P.TAG_F4, f4)
    enc.put_bool(P.TAG_BOOL_VAL, bool_val)
    if str_val:
        enc.put_string(P.TAG_STR_VAL, str_val)
    return enc.take()


def encode_req_show(figure_id: int, window_id: int = 0) -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    if window_id != 0:
        enc.put_u64(P.TAG_WINDOW_ID, window_id)
    return enc.take()


def encode_req_destroy_figure(figure_id: int) -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    return enc.take()


def encode_req_remove_series(figure_id: int, series_index: int) -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_u32(P.TAG_SERIES_INDEX, series_index)
    return enc.take()


def encode_req_close_figure(figure_id: int) -> bytes:
    """Encode REQ_CLOSE_FIGURE — close the window but keep the figure in the model."""
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    return enc.take()


def encode_req_update_batch(updates: list) -> bytes:
    """Encode REQ_UPDATE_BATCH — multiple property updates in one message.

    Each item in `updates` is a dict with keys matching encode_req_update_property kwargs:
        figure_id, axes_index, series_index, prop, f1..f4, bool_val, str_val
    """
    enc = PayloadEncoder()
    for upd in updates:
        item_bytes = encode_req_update_property(**upd)
        enc.put_blob(P.TAG_BATCH_ITEM, item_bytes)
    return enc.take()


def encode_req_reconnect(session_id: int, session_token: str = "") -> bytes:
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_SESSION_ID, session_id)
    if session_token:
        enc.put_string(P.TAG_SESSION_TOKEN, session_token)
    return enc.take()


def encode_req_list_figures() -> bytes:
    return b""


def encode_req_disconnect() -> bytes:
    return b""


def encode_req_anim_start(figure_id: int, fps: float = 60.0, duration: float = 0.0) -> bytes:
    """Encode REQ_ANIM_START — start backend-driven animation.

    fps: target frames per second
    duration: total duration in seconds (0 = infinite)
    """
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    enc.put_float(P.TAG_F1, fps)
    enc.put_float(P.TAG_F2, duration)
    return enc.take()


def encode_req_anim_stop(figure_id: int) -> bytes:
    """Encode REQ_ANIM_STOP — stop backend-driven animation."""
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_FIGURE_ID, figure_id)
    return enc.take()


def decode_anim_tick(data: bytes) -> dict:
    """Decode ANIM_TICK from backend. Returns {figure_id, frame_num, t, dt}."""
    result = {"figure_id": 0, "frame_num": 0, "t": 0.0, "dt": 0.0}
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_FIGURE_ID:
            result["figure_id"] = dec.as_u64()
        elif t == P.TAG_F1:
            result["frame_num"] = dec.as_u32()
        elif t == P.TAG_F2:
            result["t"] = dec.as_float()
        elif t == P.TAG_F3:
            result["dt"] = dec.as_float()
    return result


def decode_blob_release(data: bytes) -> str:
    """Decode BLOB_RELEASE — returns the shm blob name to unlink."""
    dec = PayloadDecoder(data)
    while dec.next():
        if dec.tag == P.TAG_BLOB_SHM:
            return dec.as_string()
    return ""


# ─── Convenience: decode response payloads ────────────────────────────────────

def decode_resp_err(data: bytes) -> Tuple[int, int, str]:
    """Returns (request_id, code, message)."""
    request_id = 0
    code = 0
    message = ""
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_REQUEST_ID:
            request_id = dec.as_u64()
        elif t == P.TAG_ERROR_CODE:
            code = dec.as_u32()
        elif t == P.TAG_ERROR_MESSAGE:
            message = dec.as_string()
    return request_id, code, message


def decode_resp_figure_created(data: bytes) -> Tuple[int, int]:
    """Returns (request_id, figure_id)."""
    request_id = 0
    figure_id = 0
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_REQUEST_ID:
            request_id = dec.as_u64()
        elif t == P.TAG_FIGURE_ID:
            figure_id = dec.as_u64()
    return request_id, figure_id


def decode_resp_axes_created(data: bytes) -> Tuple[int, int]:
    """Returns (request_id, axes_index)."""
    request_id = 0
    axes_index = 0
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_REQUEST_ID:
            request_id = dec.as_u64()
        elif t == P.TAG_AXES_INDEX:
            axes_index = dec.as_u32()
    return request_id, axes_index


def decode_resp_series_added(data: bytes) -> Tuple[int, int]:
    """Returns (request_id, series_index)."""
    request_id = 0
    series_index = 0
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_REQUEST_ID:
            request_id = dec.as_u64()
        elif t == P.TAG_SERIES_INDEX:
            series_index = dec.as_u32()
    return request_id, series_index


def decode_resp_figure_list(data: bytes) -> Tuple[int, List[int]]:
    """Returns (request_id, [figure_ids])."""
    request_id = 0
    figure_ids: List[int] = []
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_REQUEST_ID:
            request_id = dec.as_u64()
        elif t == P.TAG_FIGURE_IDS:
            figure_ids.append(dec.as_u64())
    return request_id, figure_ids


def decode_resp_ok(data: bytes) -> int:
    """Returns request_id."""
    request_id = 0
    dec = PayloadDecoder(data)
    while dec.next():
        if dec.tag == P.TAG_REQUEST_ID:
            request_id = dec.as_u64()
    return request_id


def decode_evt_window_closed(data: bytes) -> Tuple[int, int, str]:
    """Returns (figure_id, window_id, reason)."""
    figure_id = 0
    window_id = 0
    reason = ""
    dec = PayloadDecoder(data)
    while dec.next():
        t = dec.tag
        if t == P.TAG_FIGURE_ID:
            figure_id = dec.as_u64()
        elif t == P.TAG_WINDOW_ID:
            window_id = dec.as_u64()
        elif t == P.TAG_REASON:
            reason = dec.as_string()
    return figure_id, window_id, reason
