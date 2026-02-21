"""Phase 3 Python IPC tests — remove series, close figure, reconnect, animation, legend, clear.

Tests are organized into suites:
  - TestRemoveSeriesCodec: encode/decode round-trips for REQ_REMOVE_SERIES
  - TestCloseFigureCodec: encode/decode round-trips for REQ_CLOSE_FIGURE
  - TestReconnectCodec: encode/decode round-trips for REQ_RECONNECT
  - TestAxesLegend: Axes.legend() codec verification
  - TestAxesClear: Axes.clear() and remove_series() proxy behavior
  - TestFigureCloseWindow: Figure.close_window() proxy behavior
  - TestSessionListFigures: Session.list_figures() codec verification
  - TestAnimationModule: ipc_sleep and FramePacer
  - TestNewExports: verify new items in __init__.py
  - TestRemoveSeriesDiffOp: verify REMOVE_SERIES DiffOp type constant

Run: python -m pytest tests/test_phase3.py -v
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._codec import (
    PayloadEncoder,
    PayloadDecoder,
    encode_header,
    decode_header,
    encode_req_remove_series,
    encode_req_close_figure,
    encode_req_reconnect,
    encode_req_update_property,
    encode_req_list_figures,
    decode_resp_figure_list,
)
from spectra import _protocol as P


# ─── REQ_REMOVE_SERIES codec tests ──────────────────────────────────────────

class TestRemoveSeriesCodec:
    """Verify encode/decode round-trips for REQ_REMOVE_SERIES."""

    def test_encode_basic(self):
        data = encode_req_remove_series(figure_id=42, series_index=3)
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found["figure_id"] = dec.as_u64()
            elif dec.tag == P.TAG_SERIES_INDEX:
                found["series_index"] = dec.as_u32()
        assert found["figure_id"] == 42
        assert found["series_index"] == 3

    def test_encode_zero_index(self):
        data = encode_req_remove_series(figure_id=1, series_index=0)
        dec = PayloadDecoder(data)
        found_series = None
        while dec.next():
            if dec.tag == P.TAG_SERIES_INDEX:
                found_series = dec.as_u32()
        assert found_series == 0

    def test_large_figure_id(self):
        big_id = 0xFFFFFFFFFFFF
        data = encode_req_remove_series(figure_id=big_id, series_index=0)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                assert dec.as_u64() == big_id
                return
        assert False, "TAG_FIGURE_ID not found"

    def test_tag_order(self):
        data = encode_req_remove_series(figure_id=1, series_index=2)
        dec = PayloadDecoder(data)
        tags = []
        while dec.next():
            tags.append(dec.tag)
        assert tags[0] == P.TAG_FIGURE_ID
        assert tags[1] == P.TAG_SERIES_INDEX

    def test_header_with_remove_series(self):
        hdr = encode_header(
            msg_type=P.REQ_REMOVE_SERIES,
            payload_len=50,
            seq=1,
            request_id=2,
        )
        decoded = decode_header(hdr)
        assert decoded is not None
        assert decoded["type"] == P.REQ_REMOVE_SERIES

    def test_message_type_value(self):
        assert P.REQ_REMOVE_SERIES == 0x0504

    def test_in_python_request_range(self):
        assert 0x0500 <= P.REQ_REMOVE_SERIES <= 0x053F


# ─── REQ_CLOSE_FIGURE codec tests ───────────────────────────────────────────

class TestCloseFigureCodec:
    """Verify encode/decode round-trips for REQ_CLOSE_FIGURE."""

    def test_encode_basic(self):
        data = encode_req_close_figure(figure_id=7)
        dec = PayloadDecoder(data)
        found_id = None
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found_id = dec.as_u64()
        assert found_id == 7

    def test_large_figure_id(self):
        big_id = 0xDEADBEEFCAFE
        data = encode_req_close_figure(figure_id=big_id)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                assert dec.as_u64() == big_id
                return
        assert False, "TAG_FIGURE_ID not found"

    def test_message_type_value(self):
        assert P.REQ_CLOSE_FIGURE == 0x0508

    def test_header_with_close_figure(self):
        hdr = encode_header(
            msg_type=P.REQ_CLOSE_FIGURE,
            payload_len=13,
            seq=5,
            request_id=10,
        )
        decoded = decode_header(hdr)
        assert decoded is not None
        assert decoded["type"] == P.REQ_CLOSE_FIGURE


# ─── REQ_RECONNECT codec tests ──────────────────────────────────────────────

class TestReconnectCodec:
    """Verify encode/decode round-trips for REQ_RECONNECT."""

    def test_encode_basic(self):
        data = encode_req_reconnect(session_id=42)
        dec = PayloadDecoder(data)
        found_session = None
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                found_session = dec.as_u64()
        assert found_session == 42

    def test_encode_with_token(self):
        data = encode_req_reconnect(session_id=1, session_token="abc123")
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                found["session_id"] = dec.as_u64()
            elif dec.tag == P.TAG_SESSION_TOKEN:
                found["token"] = dec.as_string()
        assert found["session_id"] == 1
        assert found["token"] == "abc123"

    def test_encode_without_token(self):
        data = encode_req_reconnect(session_id=99)
        dec = PayloadDecoder(data)
        found_token = False
        while dec.next():
            if dec.tag == P.TAG_SESSION_TOKEN:
                found_token = True
        assert not found_token

    def test_message_type_value(self):
        assert P.REQ_RECONNECT == 0x0530


# ─── Axes.legend() codec tests ──────────────────────────────────────────────

class TestAxesLegend:
    """Verify legend property encoding."""

    def test_legend_visible_true(self):
        data = encode_req_update_property(
            figure_id=1, axes_index=0, prop="legend", bool_val=True,
        )
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found["prop"] = dec.as_string()
            elif dec.tag == P.TAG_BOOL_VAL:
                found["bool"] = dec.as_bool()
        assert found["prop"] == "legend"
        assert found["bool"] is True

    def test_legend_visible_false(self):
        data = encode_req_update_property(
            figure_id=1, axes_index=0, prop="legend", bool_val=False,
        )
        dec = PayloadDecoder(data)
        found_bool = None
        while dec.next():
            if dec.tag == P.TAG_BOOL_VAL:
                found_bool = dec.as_bool()
        assert found_bool is False


# ─── Axes.clear() and remove_series() proxy tests ───────────────────────────

class TestAxesClear:
    """Test Axes.clear() and remove_series() proxy behavior (no backend)."""

    def test_axes_has_remove_series(self):
        from spectra._axes import Axes
        assert hasattr(Axes, "remove_series")

    def test_axes_has_clear(self):
        from spectra._axes import Axes
        assert hasattr(Axes, "clear")

    def test_axes_has_legend(self):
        from spectra._axes import Axes
        assert hasattr(Axes, "legend")

    def test_axes_repr(self):
        from spectra._axes import Axes
        ax = Axes.__new__(Axes)
        ax._session = None
        ax._figure_id = 5
        ax._index = 2
        ax._series_list = []
        assert "5" in repr(ax)
        assert "2" in repr(ax)


# ─── Figure.close_window() proxy tests ──────────────────────────────────────

class TestFigureCloseWindow:
    """Test Figure.close_window() proxy behavior (no backend)."""

    def test_figure_has_close_window(self):
        from spectra._figure import Figure
        assert hasattr(Figure, "close_window")

    def test_figure_has_close(self):
        from spectra._figure import Figure
        assert hasattr(Figure, "close")

    def test_close_window_distinct_from_close(self):
        from spectra._figure import Figure
        assert Figure.close_window is not Figure.close


# ─── Session.list_figures() codec tests ──────────────────────────────────────

class TestSessionListFigures:
    """Test list_figures codec round-trip."""

    def test_encode_list_figures_empty(self):
        data = encode_req_list_figures()
        assert data == b""

    def test_decode_resp_figure_list_basic(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 42)
        enc.put_u64(P.TAG_FIGURE_IDS, 1)
        enc.put_u64(P.TAG_FIGURE_IDS, 2)
        enc.put_u64(P.TAG_FIGURE_IDS, 3)
        request_id, ids = decode_resp_figure_list(enc.take())
        assert request_id == 42
        assert ids == [1, 2, 3]

    def test_decode_resp_figure_list_empty(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 1)
        request_id, ids = decode_resp_figure_list(enc.take())
        assert request_id == 1
        assert ids == []

    def test_session_has_list_figures(self):
        from spectra._session import Session
        assert hasattr(Session, "list_figures")


# ─── Animation module tests ─────────────────────────────────────────────────

class TestAnimationModule:
    """Test _animation module (no backend needed)."""

    def test_import_ipc_sleep(self):
        from spectra._animation import ipc_sleep
        assert callable(ipc_sleep)

    def test_import_frame_pacer(self):
        from spectra._animation import FramePacer
        assert FramePacer is not None

    def test_frame_pacer_default_fps(self):
        from spectra._animation import FramePacer
        p = FramePacer()
        assert abs(p.fps - 30.0) < 0.1

    def test_frame_pacer_custom_fps(self):
        from spectra._animation import FramePacer
        p = FramePacer(fps=60.0)
        assert abs(p.fps - 60.0) < 0.1

    def test_frame_pacer_set_fps(self):
        from spectra._animation import FramePacer
        p = FramePacer(fps=30.0)
        p.fps = 120.0
        assert abs(p.fps - 120.0) < 0.1

    def test_frame_pacer_min_fps(self):
        from spectra._animation import FramePacer
        p = FramePacer(fps=0.0)
        assert abs(p.fps - 1.0) < 0.1

    def test_ipc_sleep_no_session(self):
        """ipc_sleep with a mock session that has no transport should fall back to time.sleep."""
        from spectra._animation import ipc_sleep

        class MockSession:
            _transport = None

        start = time.monotonic()
        ipc_sleep(MockSession(), 0.01)
        elapsed = time.monotonic() - start
        assert elapsed >= 0.009  # at least ~10ms


# ─── New exports in __init__.py ──────────────────────────────────────────────

class TestNewExports:
    """Verify new items are exported from spectra package."""

    def test_ipc_sleep_exported(self):
        import spectra as sp
        assert hasattr(sp, "ipc_sleep")
        assert "ipc_sleep" in sp.__all__

    def test_frame_pacer_exported(self):
        import spectra as sp
        assert hasattr(sp, "FramePacer")
        assert "FramePacer" in sp.__all__

    def test_animation_module_exists(self):
        from spectra import _animation
        assert hasattr(_animation, "ipc_sleep")
        assert hasattr(_animation, "FramePacer")


# ─── Cross-codec: REQ_REMOVE_SERIES wire format ─────────────────────────────

class TestRemoveSeriesWireFormat:
    """Verify wire format of REQ_REMOVE_SERIES matches C++ expectations."""

    def test_wire_format_structure(self):
        data = encode_req_remove_series(figure_id=42, series_index=1)
        dec = PayloadDecoder(data)
        tags_seen = []
        while dec.next():
            tags_seen.append(dec.tag)
        assert P.TAG_FIGURE_ID in tags_seen
        assert P.TAG_SERIES_INDEX in tags_seen

    def test_no_extra_fields(self):
        data = encode_req_remove_series(figure_id=1, series_index=0)
        dec = PayloadDecoder(data)
        count = 0
        while dec.next():
            count += 1
        assert count == 2  # exactly figure_id + series_index


# ─── Cross-codec: REQ_CLOSE_FIGURE wire format ──────────────────────────────

class TestCloseFigureWireFormat:
    """Verify wire format of REQ_CLOSE_FIGURE matches C++ expectations."""

    def test_wire_format_structure(self):
        data = encode_req_close_figure(figure_id=99)
        dec = PayloadDecoder(data)
        tags_seen = []
        while dec.next():
            tags_seen.append(dec.tag)
        assert P.TAG_FIGURE_ID in tags_seen

    def test_single_field(self):
        data = encode_req_close_figure(figure_id=1)
        dec = PayloadDecoder(data)
        count = 0
        while dec.next():
            count += 1
        assert count == 1  # exactly figure_id


# ─── Protocol constants completeness ────────────────────────────────────────

class TestProtocolCompleteness:
    """Verify all Python request/response types are defined and unique."""

    def test_all_python_request_types_unique(self):
        req_types = [
            P.REQ_CREATE_FIGURE, P.REQ_DESTROY_FIGURE, P.REQ_CREATE_AXES,
            P.REQ_ADD_SERIES, P.REQ_REMOVE_SERIES, P.REQ_SET_DATA,
            P.REQ_UPDATE_PROPERTY, P.REQ_SHOW, P.REQ_CLOSE_FIGURE,
            P.REQ_APPEND_DATA, P.REQ_GET_SNAPSHOT, P.REQ_LIST_FIGURES,
            P.REQ_RECONNECT, P.REQ_DISCONNECT,
        ]
        assert len(req_types) == len(set(req_types))

    def test_all_python_response_types_unique(self):
        resp_types = [
            P.RESP_FIGURE_CREATED, P.RESP_AXES_CREATED, P.RESP_SERIES_ADDED,
            P.RESP_SNAPSHOT, P.RESP_FIGURE_LIST,
            P.EVT_WINDOW_CLOSED, P.EVT_FIGURE_DESTROYED,
        ]
        assert len(resp_types) == len(set(resp_types))

    def test_request_types_in_range(self):
        """All Python request types should be in 0x0500-0x053F range."""
        for name in dir(P):
            if name.startswith("REQ_") and name not in ("REQ_RECONNECT", "REQ_DISCONNECT"):
                val = getattr(P, name)
                if isinstance(val, int) and val >= 0x0500:
                    assert 0x0500 <= val <= 0x053F, f"{name}=0x{val:04X} out of range"


# ─── Series proxy extended tests ─────────────────────────────────────────────

class TestSeriesProxyExtended:
    """Extended Series proxy tests for new methods."""

    def test_series_has_set_color(self):
        from spectra._series import Series
        assert hasattr(Series, "set_color")

    def test_series_has_set_line_width(self):
        from spectra._series import Series
        assert hasattr(Series, "set_line_width")

    def test_series_has_set_marker_size(self):
        from spectra._series import Series
        assert hasattr(Series, "set_marker_size")

    def test_series_has_set_visible(self):
        from spectra._series import Series
        assert hasattr(Series, "set_visible")

    def test_series_has_set_opacity(self):
        from spectra._series import Series
        assert hasattr(Series, "set_opacity")

    def test_series_has_append(self):
        from spectra._series import Series
        assert hasattr(Series, "append")

    def test_series_has_set_label(self):
        from spectra._series import Series
        assert hasattr(Series, "set_label")
