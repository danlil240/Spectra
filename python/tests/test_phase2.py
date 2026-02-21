"""Phase 2 Python IPC tests — codec, convenience API, streaming, properties.

Tests are organized into suites:
  - TestAppendDataCodec: encode/decode round-trips for REQ_APPEND_DATA
  - TestAppendDataRawCodec: raw bytes path for REQ_APPEND_DATA
  - TestProtocolConstants: new message type constants
  - TestSeriesHelpers: _interleave_xy, _to_float_list, _try_interleave_numpy
  - TestConvenienceAPI: module-level sp.figure(), sp.line(), sp.show() etc.
  - TestFigureProxy: Figure proxy properties and methods
  - TestAxesProxy: Axes proxy methods
  - TestSeriesProxy: Series proxy methods (set_data, append, set_color, etc.)
  - TestPayloadEncoderExtended: edge cases for encoder
  - TestPayloadDecoderExtended: edge cases for decoder
  - TestCrossCodecAppendData: wire format parity for append_data

Run: python -m pytest tests/test_phase2.py -v
"""

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._codec import (
    PayloadEncoder,
    PayloadDecoder,
    encode_header,
    decode_header,
    encode_req_append_data,
    encode_req_append_data_raw,
    encode_req_update_property,
    decode_evt_window_closed,
)
from spectra import _protocol as P
from spectra._series import _to_float_list, _interleave_xy, _try_interleave_numpy


# ─── REQ_APPEND_DATA codec tests ─────────────────────────────────────────────

class TestAppendDataCodec:
    """Verify encode/decode round-trips for REQ_APPEND_DATA."""

    def test_encode_basic(self):
        data = encode_req_append_data(figure_id=42, series_index=1, data=[1.0, 2.0, 3.0, 4.0])
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found["figure_id"] = dec.as_u64()
            elif dec.tag == P.TAG_SERIES_INDEX:
                found["series_index"] = dec.as_u32()
            elif dec.tag == P.TAG_BLOB_INLINE:
                found["data"] = dec.as_float_array()
        assert found["figure_id"] == 42
        assert found["series_index"] == 1
        assert len(found["data"]) == 4
        assert abs(found["data"][0] - 1.0) < 1e-6
        assert abs(found["data"][3] - 4.0) < 1e-6

    def test_encode_empty_data(self):
        data = encode_req_append_data(figure_id=1, series_index=0, data=[])
        dec = PayloadDecoder(data)
        found_blob = False
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_blob = True
        assert not found_blob  # empty data should not produce a blob

    def test_encode_single_point(self):
        data = encode_req_append_data(figure_id=1, series_index=0, data=[5.0, 10.0])
        dec = PayloadDecoder(data)
        found_data = None
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_data = dec.as_float_array()
        assert found_data is not None
        assert len(found_data) == 2
        assert abs(found_data[0] - 5.0) < 1e-6
        assert abs(found_data[1] - 10.0) < 1e-6

    def test_encode_large_append(self):
        """Verify large data arrays encode correctly."""
        n = 10000
        data_list = [float(i) for i in range(n)]
        data = encode_req_append_data(figure_id=1, series_index=0, data=data_list)
        dec = PayloadDecoder(data)
        found_data = None
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_data = dec.as_float_array()
        assert found_data is not None
        assert len(found_data) == n
        assert abs(found_data[0] - 0.0) < 1e-6
        assert abs(found_data[-1] - float(n - 1)) < 1e-3

    def test_figure_id_preserved(self):
        """Verify large figure IDs survive encoding."""
        big_id = 0xFFFFFFFFFFFF
        data = encode_req_append_data(figure_id=big_id, series_index=0, data=[1.0])
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                assert dec.as_u64() == big_id
                return
        assert False, "TAG_FIGURE_ID not found"


class TestAppendDataRawCodec:
    """Verify raw bytes path for REQ_APPEND_DATA."""

    def test_encode_raw_basic(self):
        raw = struct.pack("<4f", 1.0, 2.0, 3.0, 4.0)
        data = encode_req_append_data_raw(figure_id=42, series_index=1, raw_bytes=raw, count=4)
        dec = PayloadDecoder(data)
        found_data = None
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_data = dec.as_float_array()
        assert found_data is not None
        assert len(found_data) == 4
        assert abs(found_data[0] - 1.0) < 1e-6

    def test_encode_raw_matches_list(self):
        """Raw and list encoding should produce identical blob content."""
        values = [1.5, 2.5, 3.5, 4.5]
        raw = struct.pack(f"<{len(values)}f", *values)
        data_raw = encode_req_append_data_raw(figure_id=1, series_index=0, raw_bytes=raw, count=len(values))
        data_list = encode_req_append_data(figure_id=1, series_index=0, data=values)
        # Both should decode to the same float array
        def extract_floats(payload):
            dec = PayloadDecoder(payload)
            while dec.next():
                if dec.tag == P.TAG_BLOB_INLINE:
                    return dec.as_float_array()
            return None
        assert extract_floats(data_raw) is not None
        assert extract_floats(data_list) is not None
        for a, b in zip(extract_floats(data_raw), extract_floats(data_list)):
            assert abs(a - b) < 1e-6

    def test_numpy_interleave_raw(self):
        """Verify numpy fast path produces valid raw bytes."""
        try:
            import numpy as np
        except ImportError:
            return  # skip if numpy not available
        x = np.array([1.0, 2.0, 3.0], dtype=np.float32)
        y = np.array([10.0, 20.0, 30.0], dtype=np.float32)
        result = _try_interleave_numpy(x, y)
        assert result is not None
        raw_bytes, count = result
        assert count == 6  # 3 points * 2 coords
        data = encode_req_append_data_raw(figure_id=1, series_index=0, raw_bytes=raw_bytes, count=count)
        dec = PayloadDecoder(data)
        found_data = None
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_data = dec.as_float_array()
        assert found_data is not None
        assert len(found_data) == 6
        assert abs(found_data[0] - 1.0) < 1e-6
        assert abs(found_data[1] - 10.0) < 1e-6


# ─── Protocol constants ──────────────────────────────────────────────────────

class TestProtocolConstants:
    """Verify new message type constants are correct."""

    def test_req_append_data_value(self):
        assert P.REQ_APPEND_DATA == 0x0509

    def test_req_append_data_in_request_range(self):
        assert 0x0500 <= P.REQ_APPEND_DATA <= 0x053F

    def test_all_request_types_unique(self):
        req_types = [
            P.REQ_CREATE_FIGURE, P.REQ_DESTROY_FIGURE, P.REQ_CREATE_AXES,
            P.REQ_ADD_SERIES, P.REQ_REMOVE_SERIES, P.REQ_SET_DATA,
            P.REQ_UPDATE_PROPERTY, P.REQ_SHOW, P.REQ_CLOSE_FIGURE,
            P.REQ_APPEND_DATA, P.REQ_GET_SNAPSHOT, P.REQ_LIST_FIGURES,
            P.REQ_RECONNECT, P.REQ_DISCONNECT,
        ]
        assert len(req_types) == len(set(req_types))

    def test_all_response_types_unique(self):
        resp_types = [
            P.RESP_FIGURE_CREATED, P.RESP_AXES_CREATED, P.RESP_SERIES_ADDED,
            P.RESP_SNAPSHOT, P.RESP_FIGURE_LIST,
            P.EVT_WINDOW_CLOSED, P.EVT_FIGURE_DESTROYED,
        ]
        assert len(resp_types) == len(set(resp_types))


# ─── Series helper tests ─────────────────────────────────────────────────────

class TestSeriesHelpers:
    """Test _to_float_list, _interleave_xy, _try_interleave_numpy."""

    def test_to_float_list_from_list(self):
        assert _to_float_list([1, 2, 3]) == [1.0, 2.0, 3.0]

    def test_to_float_list_from_ints(self):
        assert _to_float_list([1, 2, 3]) == [1.0, 2.0, 3.0]

    def test_to_float_list_from_generator(self):
        result = _to_float_list(x * 0.5 for x in range(4))
        assert result == [0.0, 0.5, 1.0, 1.5]

    def test_interleave_xy_basic(self):
        result = _interleave_xy([1, 2, 3], [10, 20, 30])
        assert result == [1.0, 10.0, 2.0, 20.0, 3.0, 30.0]

    def test_interleave_xy_single(self):
        result = _interleave_xy([5], [10])
        assert result == [5.0, 10.0]

    def test_interleave_xy_empty(self):
        result = _interleave_xy([], [])
        assert result == []

    def test_interleave_xy_mismatch_raises(self):
        import pytest
        with pytest.raises(ValueError, match="same length"):
            _interleave_xy([1, 2], [10])

    def test_numpy_interleave_basic(self):
        try:
            import numpy as np
        except ImportError:
            return
        x = np.array([1.0, 2.0], dtype=np.float64)
        y = np.array([10.0, 20.0], dtype=np.float64)
        result = _try_interleave_numpy(x, y)
        assert result is not None
        raw, count = result
        assert count == 4
        floats = struct.unpack(f"<{count}f", raw)
        assert abs(floats[0] - 1.0) < 1e-5
        assert abs(floats[1] - 10.0) < 1e-5

    def test_numpy_interleave_shape_mismatch(self):
        try:
            import numpy as np
        except ImportError:
            return
        x = np.array([1.0, 2.0])
        y = np.array([10.0])
        result = _try_interleave_numpy(x, y)
        assert result is None

    def test_numpy_interleave_non_numpy(self):
        result = _try_interleave_numpy([1.0, 2.0], [10.0, 20.0])
        assert result is None


# ─── Convenience API tests (module-level) ────────────────────────────────────

class TestConvenienceAPI:
    """Test module-level convenience functions (no backend needed — just import checks)."""

    def test_exports_exist(self):
        import spectra as sp
        assert hasattr(sp, "figure")
        assert hasattr(sp, "subplot")
        assert hasattr(sp, "line")
        assert hasattr(sp, "scatter")
        assert hasattr(sp, "show")
        assert hasattr(sp, "close")
        assert hasattr(sp, "gcf")
        assert hasattr(sp, "gca")

    def test_exports_in_all(self):
        import spectra as sp
        for name in ["figure", "subplot", "line", "scatter", "show", "close", "gcf", "gca"]:
            assert name in sp.__all__

    def test_version_exists(self):
        import spectra as sp
        assert hasattr(sp, "__version__")
        assert sp.__version__ == "0.1.0"

    def test_session_class_exported(self):
        from spectra import Session
        assert Session is not None

    def test_figure_class_exported(self):
        from spectra import Figure
        assert Figure is not None

    def test_axes_class_exported(self):
        from spectra import Axes
        assert Axes is not None

    def test_series_class_exported(self):
        from spectra import Series
        assert Series is not None

    def test_error_classes_exported(self):
        from spectra import (
            SpectraError,
            ConnectionError as SCE,
            ProtocolError,
            TimeoutError as STE,
            FigureNotFoundError,
            BackendError,
        )
        assert issubclass(SCE, SpectraError)
        assert issubclass(ProtocolError, SpectraError)
        assert issubclass(STE, SpectraError)
        assert issubclass(FigureNotFoundError, SpectraError)
        assert issubclass(BackendError, SpectraError)

    def test_close_without_session(self):
        """close() should not raise even if no session exists."""
        import spectra as sp
        sp._default_session = None
        sp._current_figure = None
        sp._current_axes = None
        sp.close()  # should not raise

    def test_show_without_session(self):
        """show() should not raise even if no session exists."""
        import spectra as sp
        sp._default_session = None
        sp.show()  # should not raise


# ─── Figure proxy tests ──────────────────────────────────────────────────────

class TestFigureProxy:
    """Test Figure proxy object (without backend)."""

    def test_figure_repr(self):
        from spectra._figure import Figure
        fig = Figure.__new__(Figure)
        fig._session = None
        fig._id = 42
        fig._title = "Test"
        fig._axes_list = []
        fig._visible = False
        fig._shown_once = False
        assert "42" in repr(fig)
        assert "Test" in repr(fig)

    def test_figure_id_property(self):
        from spectra._figure import Figure
        fig = Figure.__new__(Figure)
        fig._id = 99
        fig._session = None
        fig._title = ""
        fig._axes_list = []
        fig._visible = False
        fig._shown_once = False
        assert fig.id == 99

    def test_figure_title_property(self):
        from spectra._figure import Figure
        fig = Figure.__new__(Figure)
        fig._id = 1
        fig._session = None
        fig._title = "My Plot"
        fig._axes_list = []
        fig._visible = False
        fig._shown_once = False
        assert fig.title == "My Plot"

    def test_figure_is_visible_default(self):
        from spectra._figure import Figure
        fig = Figure.__new__(Figure)
        fig._id = 1
        fig._session = None
        fig._title = ""
        fig._axes_list = []
        fig._visible = False
        fig._shown_once = False
        assert fig.is_visible is False

    def test_figure_is_visible_after_show(self):
        from spectra._figure import Figure
        fig = Figure.__new__(Figure)
        fig._id = 1
        fig._session = None
        fig._title = ""
        fig._axes_list = []
        fig._visible = True
        fig._shown_once = True
        assert fig.is_visible is True

    def test_figure_axes_empty(self):
        from spectra._figure import Figure
        fig = Figure.__new__(Figure)
        fig._id = 1
        fig._session = None
        fig._title = ""
        fig._axes_list = []
        fig._visible = False
        fig._shown_once = False
        assert fig.axes == []


# ─── Series proxy tests ──────────────────────────────────────────────────────

class TestSeriesProxy:
    """Test Series proxy object properties (without backend)."""

    def test_series_repr(self):
        from spectra._series import Series
        s = Series.__new__(Series)
        s._session = None
        s._figure_id = 1
        s._index = 0
        s._type = "line"
        s._label = "data"
        assert "line" in repr(s)
        assert "1" in repr(s)

    def test_series_index(self):
        from spectra._series import Series
        s = Series.__new__(Series)
        s._index = 5
        s._session = None
        s._figure_id = 1
        s._type = "scatter"
        s._label = ""
        assert s.index == 5

    def test_series_figure_id(self):
        from spectra._series import Series
        s = Series.__new__(Series)
        s._figure_id = 42
        s._session = None
        s._index = 0
        s._type = "line"
        s._label = ""
        assert s.figure_id == 42

    def test_series_type(self):
        from spectra._series import Series
        s = Series.__new__(Series)
        s._type = "scatter"
        s._session = None
        s._figure_id = 1
        s._index = 0
        s._label = ""
        assert s.series_type == "scatter"

    def test_series_label(self):
        from spectra._series import Series
        s = Series.__new__(Series)
        s._label = "my data"
        s._session = None
        s._figure_id = 1
        s._index = 0
        s._type = "line"
        assert s.label == "my data"


# ─── Extended encoder/decoder edge case tests ────────────────────────────────

class TestPayloadEncoderExtended:
    """Edge cases for PayloadEncoder."""

    def test_empty_string(self):
        enc = PayloadEncoder()
        enc.put_string(0x40, "")
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_string() == ""

    def test_unicode_string(self):
        enc = PayloadEncoder()
        enc.put_string(0x40, "日本語テスト")
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_string() == "日本語テスト"

    def test_zero_float(self):
        enc = PayloadEncoder()
        enc.put_float(0x50, 0.0)
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_float() == 0.0

    def test_negative_float(self):
        enc = PayloadEncoder()
        enc.put_float(0x50, -3.14)
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert abs(dec.as_float() - (-3.14)) < 1e-5

    def test_max_u32(self):
        enc = PayloadEncoder()
        enc.put_u32(0x20, 0xFFFFFFFF)
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_u32() == 0xFFFFFFFF

    def test_max_u64(self):
        enc = PayloadEncoder()
        enc.put_u64(0x30, 0xFFFFFFFFFFFFFFFF)
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_u64() == 0xFFFFFFFFFFFFFFFF

    def test_bool_true(self):
        enc = PayloadEncoder()
        enc.put_bool(0x60, True)
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_bool() is True

    def test_bool_false(self):
        enc = PayloadEncoder()
        enc.put_bool(0x60, False)
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.as_bool() is False

    def test_float_array_empty(self):
        enc = PayloadEncoder()
        enc.put_float_array(0x70, [])
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        arr = dec.as_float_array()
        assert arr == []

    def test_float_array_single(self):
        enc = PayloadEncoder()
        enc.put_float_array(0x70, [42.0])
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        arr = dec.as_float_array()
        assert len(arr) == 1
        assert abs(arr[0] - 42.0) < 1e-6

    def test_multiple_blobs(self):
        enc = PayloadEncoder()
        enc.put_float_array(0x70, [1.0, 2.0])
        enc.put_float_array(0x71, [3.0, 4.0])
        data = enc.take()
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.tag == 0x70
        a1 = dec.as_float_array()
        assert dec.next()
        assert dec.tag == 0x71
        a2 = dec.as_float_array()
        assert a1 == [1.0, 2.0]  # float comparison ok for exact values
        assert a2 == [3.0, 4.0]


class TestPayloadDecoderExtended:
    """Edge cases for PayloadDecoder."""

    def test_truncated_header(self):
        dec = PayloadDecoder(b"\x10\x02\x00")
        assert not dec.next()

    def test_truncated_value(self):
        # tag + len says 4 bytes but only 2 available
        data = b"\x10" + struct.pack("<I", 4) + b"\x00\x00"
        dec = PayloadDecoder(data)
        assert not dec.next()

    def test_zero_length_field(self):
        data = b"\x10" + struct.pack("<I", 0)
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.tag == 0x10

    def test_sequential_iteration(self):
        enc = PayloadEncoder()
        for i in range(10):
            enc.put_u32(0x20, i)
        data = enc.take()
        dec = PayloadDecoder(data)
        values = []
        while dec.next():
            values.append(dec.as_u32())
        assert values == list(range(10))


# ─── Cross-codec wire format for append_data ─────────────────────────────────

class TestCrossCodecAppendData:
    """Verify wire format of REQ_APPEND_DATA matches C++ expectations."""

    def test_wire_format_structure(self):
        data = encode_req_append_data(figure_id=42, series_index=1, data=[1.0, 2.0])
        # Should contain: TAG_FIGURE_ID(u64) + TAG_SERIES_INDEX(u32) + TAG_BLOB_INLINE(float_array)
        dec = PayloadDecoder(data)
        tags_seen = []
        while dec.next():
            tags_seen.append(dec.tag)
        assert P.TAG_FIGURE_ID in tags_seen
        assert P.TAG_SERIES_INDEX in tags_seen
        assert P.TAG_BLOB_INLINE in tags_seen

    def test_tag_order(self):
        """Tags should appear in the order they were encoded."""
        data = encode_req_append_data(figure_id=1, series_index=0, data=[1.0])
        dec = PayloadDecoder(data)
        tags = []
        while dec.next():
            tags.append(dec.tag)
        assert tags[0] == P.TAG_FIGURE_ID
        assert tags[1] == P.TAG_SERIES_INDEX
        assert tags[2] == P.TAG_BLOB_INLINE

    def test_message_type_value(self):
        """REQ_APPEND_DATA should be 0x0509."""
        assert P.REQ_APPEND_DATA == 0x0509

    def test_header_with_append_data(self):
        """Verify header encodes correctly with REQ_APPEND_DATA type."""
        hdr = encode_header(
            msg_type=P.REQ_APPEND_DATA,
            payload_len=100,
            seq=1,
            request_id=2,
            session_id=3,
            window_id=4,
        )
        decoded = decode_header(hdr)
        assert decoded is not None
        assert decoded["type"] == P.REQ_APPEND_DATA
        assert decoded["payload_len"] == 100


# ─── EVT_WINDOW_CLOSED decoder tests ─────────────────────────────────────────

class TestEvtWindowClosed:
    """Verify EVT_WINDOW_CLOSED decoding."""

    def test_decode_basic(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_FIGURE_ID, 42)
        enc.put_u64(P.TAG_WINDOW_ID, 7)
        enc.put_string(P.TAG_REASON, "user_close")
        figure_id, window_id, reason = decode_evt_window_closed(enc.take())
        assert figure_id == 42
        assert window_id == 7
        assert reason == "user_close"

    def test_decode_missing_reason(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_FIGURE_ID, 1)
        enc.put_u64(P.TAG_WINDOW_ID, 2)
        figure_id, window_id, reason = decode_evt_window_closed(enc.take())
        assert figure_id == 1
        assert window_id == 2
        assert reason == ""

    def test_evt_window_closed_type_value(self):
        assert P.EVT_WINDOW_CLOSED == 0x0550


# ─── Update property codec tests ─────────────────────────────────────────────

class TestUpdatePropertyCodec:
    """Verify encode_req_update_property for various property types."""

    def test_color_property(self):
        data = encode_req_update_property(
            figure_id=1, series_index=0, prop="color",
            f1=1.0, f2=0.5, f3=0.25, f4=0.75,
        )
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found["prop"] = dec.as_string()
            elif dec.tag == P.TAG_F1:
                found["f1"] = dec.as_float()
            elif dec.tag == P.TAG_F2:
                found["f2"] = dec.as_float()
            elif dec.tag == P.TAG_F3:
                found["f3"] = dec.as_float()
            elif dec.tag == P.TAG_F4:
                found["f4"] = dec.as_float()
        assert found["prop"] == "color"
        assert abs(found["f1"] - 1.0) < 1e-6
        assert abs(found["f2"] - 0.5) < 1e-6
        assert abs(found["f3"] - 0.25) < 1e-6
        assert abs(found["f4"] - 0.75) < 1e-6

    def test_line_width_property(self):
        data = encode_req_update_property(
            figure_id=1, series_index=0, prop="line_width", f1=2.5,
        )
        dec = PayloadDecoder(data)
        found_prop = None
        found_f1 = None
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found_prop = dec.as_string()
            elif dec.tag == P.TAG_F1:
                found_f1 = dec.as_float()
        assert found_prop == "line_width"
        assert abs(found_f1 - 2.5) < 1e-6

    def test_visible_property(self):
        data = encode_req_update_property(
            figure_id=1, series_index=0, prop="visible", bool_val=True,
        )
        dec = PayloadDecoder(data)
        found_prop = None
        found_bool = None
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found_prop = dec.as_string()
            elif dec.tag == P.TAG_BOOL_VAL:
                found_bool = dec.as_bool()
        assert found_prop == "visible"
        assert found_bool is True

    def test_label_property_with_str_val(self):
        data = encode_req_update_property(
            figure_id=1, series_index=0, prop="label", str_val="my series",
        )
        dec = PayloadDecoder(data)
        found_prop = None
        found_str = None
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found_prop = dec.as_string()
            elif dec.tag == P.TAG_STR_VAL:
                found_str = dec.as_string()
        assert found_prop == "label"
        assert found_str == "my series"

    def test_title_property(self):
        data = encode_req_update_property(
            figure_id=1, prop="title", str_val="My Figure",
        )
        dec = PayloadDecoder(data)
        found_prop = None
        found_str = None
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found_prop = dec.as_string()
            elif dec.tag == P.TAG_STR_VAL:
                found_str = dec.as_string()
        assert found_prop == "title"
        assert found_str == "My Figure"

    def test_grid_property(self):
        data = encode_req_update_property(
            figure_id=1, axes_index=0, prop="grid", bool_val=True,
        )
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found["prop"] = dec.as_string()
            elif dec.tag == P.TAG_AXES_INDEX:
                found["axes_index"] = dec.as_u32()
            elif dec.tag == P.TAG_BOOL_VAL:
                found["bool"] = dec.as_bool()
        assert found["prop"] == "grid"
        assert found["axes_index"] == 0
        assert found["bool"] is True

    def test_opacity_property(self):
        data = encode_req_update_property(
            figure_id=1, series_index=2, prop="opacity", f1=0.5,
        )
        dec = PayloadDecoder(data)
        found_prop = None
        found_f1 = None
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found_prop = dec.as_string()
            elif dec.tag == P.TAG_F1:
                found_f1 = dec.as_float()
        assert found_prop == "opacity"
        assert abs(found_f1 - 0.5) < 1e-6

    def test_xlim_property(self):
        data = encode_req_update_property(
            figure_id=1, axes_index=0, prop="xlim", f1=-10.0, f2=10.0,
        )
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found["prop"] = dec.as_string()
            elif dec.tag == P.TAG_F1:
                found["f1"] = dec.as_float()
            elif dec.tag == P.TAG_F2:
                found["f2"] = dec.as_float()
        assert found["prop"] == "xlim"
        assert abs(found["f1"] - (-10.0)) < 1e-5
        assert abs(found["f2"] - 10.0) < 1e-5
