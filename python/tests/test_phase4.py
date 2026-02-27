"""Phase 4 Python IPC tests — batch mutations, reconnect, protocol version, batch context.

Tests are organized into suites:
  - TestBatchCodec: encode/decode round-trips for REQ_UPDATE_BATCH
  - TestBatchProtocol: protocol constant verification
  - TestReconnectCodec: extended reconnect codec tests
  - TestSessionBatchAPI: Session.batch_update() API presence
  - TestSessionReconnectAPI: Session.reconnect() API presence
  - TestAxesBatchContext: Axes.batch() context manager
  - TestBatchWireFormat: wire format verification
  - TestProtocolVersionConstants: version constants match C++
  - TestBatchItemNesting: nested TLV batch item structure
  - TestReconnectSessionIdVariants: various session_id values
  - TestBatchEdgeCases: empty batch, single item, many items

Run: python -m pytest tests/test_phase4.py -v
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._codec import (
    PayloadDecoder,
    encode_header,
    decode_header,
    encode_req_update_property,
    encode_req_reconnect,
    encode_req_update_batch,
)
from spectra import _protocol as P


# ─── REQ_UPDATE_BATCH codec tests ────────────────────────────────────────────

class TestBatchCodec:
    """Verify encode/decode round-trips for REQ_UPDATE_BATCH."""

    def test_encode_single_item(self):
        updates = [
            dict(figure_id=1, axes_index=0, prop="xlim", f1=0.0, f2=10.0),
        ]
        data = encode_req_update_batch(updates)
        assert len(data) > 0

        # Decode: should find one TAG_BATCH_ITEM blob
        dec = PayloadDecoder(data)
        count = 0
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                count += 1
                blob = dec.as_blob()
                # The blob should be a valid update_property payload
                inner = PayloadDecoder(blob)
                found_prop = None
                while inner.next():
                    if inner.tag == P.TAG_PROPERTY_NAME:
                        found_prop = inner.as_string()
                assert found_prop == "xlim"
        assert count == 1

    def test_encode_multiple_items(self):
        updates = [
            dict(figure_id=1, axes_index=0, prop="xlim", f1=0.0, f2=10.0),
            dict(figure_id=1, axes_index=0, prop="ylim", f1=-1.0, f2=1.0),
            dict(figure_id=1, axes_index=0, prop="grid", bool_val=True),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        count = 0
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                count += 1
        assert count == 3

    def test_encode_preserves_values(self):
        updates = [
            dict(figure_id=42, axes_index=2, prop="xlabel", str_val="Time (s)"),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                found = {}
                while inner.next():
                    if inner.tag == P.TAG_FIGURE_ID:
                        found["figure_id"] = inner.as_u64()
                    elif inner.tag == P.TAG_AXES_INDEX:
                        found["axes_index"] = inner.as_u32()
                    elif inner.tag == P.TAG_PROPERTY_NAME:
                        found["prop"] = inner.as_string()
                    elif inner.tag == P.TAG_STR_VAL:
                        found["str_val"] = inner.as_string()
                assert found["figure_id"] == 42
                assert found["axes_index"] == 2
                assert found["prop"] == "xlabel"
                assert found["str_val"] == "Time (s)"

    def test_encode_empty_list(self):
        data = encode_req_update_batch([])
        assert data == b""

    def test_encode_float_properties(self):
        updates = [
            dict(figure_id=1, series_index=0, prop="color",
                 f1=1.0, f2=0.0, f3=0.0, f4=1.0),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                found_prop = None
                while inner.next():
                    if inner.tag == P.TAG_PROPERTY_NAME:
                        found_prop = inner.as_string()
                assert found_prop == "color"

    def test_encode_bool_property(self):
        updates = [
            dict(figure_id=1, axes_index=0, prop="grid", bool_val=False),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                found_bool = None
                while inner.next():
                    if inner.tag == P.TAG_BOOL_VAL:
                        found_bool = inner.as_bool()
                assert found_bool is False


# ─── Protocol constant tests ─────────────────────────────────────────────────

class TestBatchProtocol:
    """Verify REQ_UPDATE_BATCH protocol constants."""

    def test_message_type_value(self):
        assert P.REQ_UPDATE_BATCH == 0x050A

    def test_in_python_request_range(self):
        assert 0x0500 <= P.REQ_UPDATE_BATCH <= 0x053F

    def test_tag_batch_item_value(self):
        assert P.TAG_BATCH_ITEM == 0xB1

    def test_unique_among_request_types(self):
        req_types = [
            P.REQ_CREATE_FIGURE, P.REQ_DESTROY_FIGURE, P.REQ_CREATE_AXES,
            P.REQ_ADD_SERIES, P.REQ_REMOVE_SERIES, P.REQ_SET_DATA,
            P.REQ_UPDATE_PROPERTY, P.REQ_SHOW, P.REQ_CLOSE_FIGURE,
            P.REQ_APPEND_DATA, P.REQ_UPDATE_BATCH, P.REQ_GET_SNAPSHOT,
            P.REQ_LIST_FIGURES, P.REQ_RECONNECT, P.REQ_DISCONNECT,
        ]
        assert len(req_types) == len(set(req_types))

    def test_header_with_batch(self):
        hdr = encode_header(
            msg_type=P.REQ_UPDATE_BATCH,
            payload_len=100,
            seq=1,
            request_id=5,
        )
        decoded = decode_header(hdr)
        assert decoded is not None
        assert decoded["type"] == P.REQ_UPDATE_BATCH


# ─── Extended reconnect codec tests ──────────────────────────────────────────

class TestReconnectCodecExtended:
    """Extended reconnect codec verification."""

    def test_reconnect_zero_session(self):
        data = encode_req_reconnect(session_id=0)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                assert dec.as_u64() == 0
                return
        assert False, "TAG_SESSION_ID not found"

    def test_reconnect_large_session_id(self):
        big_id = 0xDEADBEEFCAFEBABE
        data = encode_req_reconnect(session_id=big_id)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                assert dec.as_u64() == big_id
                return
        assert False, "TAG_SESSION_ID not found"

    def test_reconnect_with_long_token(self):
        token = "a" * 256
        data = encode_req_reconnect(session_id=1, session_token=token)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_TOKEN:
                assert dec.as_string() == token
                return
        assert False, "TAG_SESSION_TOKEN not found"

    def test_reconnect_unicode_token(self):
        token = "reconnect_\u00e9\u00e8\u00ea"
        data = encode_req_reconnect(session_id=1, session_token=token)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_TOKEN:
                assert dec.as_string() == token
                return
        assert False, "TAG_SESSION_TOKEN not found"


# ─── Session API presence tests ──────────────────────────────────────────────

class TestSessionBatchAPI:
    """Verify Session.batch_update() exists."""

    def test_session_has_batch_update(self):
        from spectra._session import Session
        assert hasattr(Session, "batch_update")

    def test_batch_update_is_callable(self):
        from spectra._session import Session
        assert callable(getattr(Session, "batch_update"))


class TestSessionReconnectAPI:
    """Verify Session.reconnect() exists."""

    def test_session_has_reconnect(self):
        from spectra._session import Session
        assert hasattr(Session, "reconnect")

    def test_reconnect_is_callable(self):
        from spectra._session import Session
        assert callable(getattr(Session, "reconnect"))


# ─── Axes batch context manager tests ────────────────────────────────────────

class TestAxesBatchContext:
    """Test Axes.batch() context manager (no backend)."""

    def test_axes_has_batch(self):
        from spectra._axes import Axes
        assert hasattr(Axes, "batch")

    def test_batch_context_class_exists(self):
        from spectra._axes import _AxesBatchContext
        assert _AxesBatchContext is not None

    def test_batch_context_has_set_xlim(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "set_xlim")

    def test_batch_context_has_set_ylim(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "set_ylim")

    def test_batch_context_has_set_xlabel(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "set_xlabel")

    def test_batch_context_has_set_ylabel(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "set_ylabel")

    def test_batch_context_has_set_title(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "set_title")

    def test_batch_context_has_grid(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "grid")

    def test_batch_context_has_legend(self):
        from spectra._axes import _AxesBatchContext
        assert hasattr(_AxesBatchContext, "legend")

    def test_batch_context_collects_updates(self):
        """Verify that the context manager collects updates without sending."""
        from spectra._axes import _AxesBatchContext, Axes

        ax = Axes.__new__(Axes)
        ax._session = None
        ax._figure_id = 1
        ax._index = 0
        ax._series_list = []

        ctx = _AxesBatchContext(ax)
        ctx.set_xlim(0, 10)
        ctx.set_ylim(-1, 1)
        ctx.set_xlabel("X")

        assert len(ctx._updates) == 3
        assert ctx._updates[0]["prop"] == "xlim"
        assert ctx._updates[1]["prop"] == "ylim"
        assert ctx._updates[2]["prop"] == "xlabel"

    def test_batch_context_sets_figure_id(self):
        from spectra._axes import _AxesBatchContext, Axes

        ax = Axes.__new__(Axes)
        ax._session = None
        ax._figure_id = 42
        ax._index = 3
        ax._series_list = []

        ctx = _AxesBatchContext(ax)
        ctx.set_xlim(0, 10)

        assert ctx._updates[0]["figure_id"] == 42
        assert ctx._updates[0]["axes_index"] == 3


# ─── Batch wire format tests ─────────────────────────────────────────────────

class TestBatchWireFormat:
    """Verify wire format of REQ_UPDATE_BATCH matches C++ expectations."""

    def test_batch_items_are_blobs(self):
        updates = [
            dict(figure_id=1, prop="xlim", f1=0.0, f2=10.0),
            dict(figure_id=1, prop="ylim", f1=-1.0, f2=1.0),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        tags = []
        while dec.next():
            tags.append(dec.tag)
        assert all(t == P.TAG_BATCH_ITEM for t in tags)
        assert len(tags) == 2

    def test_each_item_is_valid_update_property(self):
        """Each batch item blob should decode as a valid update_property payload."""
        updates = [
            dict(figure_id=5, axes_index=1, prop="grid", bool_val=True),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                # Should have the same structure as encode_req_update_property
                ref = encode_req_update_property(
                    figure_id=5, axes_index=1, prop="grid", bool_val=True,
                )
                assert blob == ref

    def test_item_order_preserved(self):
        updates = [
            dict(figure_id=1, prop="xlabel", str_val="A"),
            dict(figure_id=1, prop="ylabel", str_val="B"),
            dict(figure_id=1, prop="axes_title", str_val="C"),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        props = []
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                while inner.next():
                    if inner.tag == P.TAG_PROPERTY_NAME:
                        props.append(inner.as_string())
        assert props == ["xlabel", "ylabel", "axes_title"]


# ─── Protocol version constants ──────────────────────────────────────────────

class TestProtocolVersionConstants:
    """Verify protocol version constants match C++ header."""

    def test_protocol_major(self):
        assert P.PROTOCOL_MAJOR == 1

    def test_protocol_minor(self):
        assert P.PROTOCOL_MINOR == 0

    def test_header_size(self):
        assert P.HEADER_SIZE == 40

    def test_magic(self):
        assert P.MAGIC == b"\x53\x50"

    def test_max_payload_size(self):
        assert P.MAX_PAYLOAD_SIZE == 256 * 1024 * 1024


# ─── Batch item nesting tests ────────────────────────────────────────────────

class TestBatchItemNesting:
    """Verify nested TLV structure of batch items."""

    def test_nested_blob_decodable(self):
        updates = [
            dict(figure_id=99, axes_index=0, series_index=3,
                 prop="opacity", f1=0.5),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                found = {}
                while inner.next():
                    if inner.tag == P.TAG_FIGURE_ID:
                        found["figure_id"] = inner.as_u64()
                    elif inner.tag == P.TAG_SERIES_INDEX:
                        found["series_index"] = inner.as_u32()
                    elif inner.tag == P.TAG_PROPERTY_NAME:
                        found["prop"] = inner.as_string()
                    elif inner.tag == P.TAG_F1:
                        found["f1"] = inner.as_double()
                assert found["figure_id"] == 99
                assert found["series_index"] == 3
                assert found["prop"] == "opacity"
                assert abs(found["f1"] - 0.5) < 0.001

    def test_multiple_figures_in_batch(self):
        updates = [
            dict(figure_id=1, prop="title", str_val="Fig 1"),
            dict(figure_id=2, prop="title", str_val="Fig 2"),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        figure_ids = []
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                while inner.next():
                    if inner.tag == P.TAG_FIGURE_ID:
                        figure_ids.append(inner.as_u64())
        assert figure_ids == [1, 2]


# ─── Reconnect session ID variants ──────────────────────────────────────────

class TestReconnectSessionIdVariants:
    """Test various session_id values for reconnect."""

    def test_session_id_one(self):
        data = encode_req_reconnect(session_id=1)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                assert dec.as_u64() == 1
                return
        assert False

    def test_session_id_max_u32(self):
        data = encode_req_reconnect(session_id=0xFFFFFFFF)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                assert dec.as_u64() == 0xFFFFFFFF
                return
        assert False

    def test_session_id_max_u64(self):
        data = encode_req_reconnect(session_id=0xFFFFFFFFFFFFFFFF)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_SESSION_ID:
                assert dec.as_u64() == 0xFFFFFFFFFFFFFFFF
                return
        assert False


# ─── Batch edge cases ────────────────────────────────────────────────────────

class TestBatchEdgeCases:
    """Edge cases for batch encoding."""

    def test_single_item_batch(self):
        updates = [dict(figure_id=1, prop="grid", bool_val=True)]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        count = 0
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                count += 1
        assert count == 1

    def test_many_items_batch(self):
        updates = [
            dict(figure_id=1, prop=f"prop_{i}", str_val=f"val_{i}")
            for i in range(50)
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        count = 0
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                count += 1
        assert count == 50

    def test_batch_with_all_property_types(self):
        """Batch containing float, bool, and string properties."""
        updates = [
            dict(figure_id=1, prop="xlim", f1=0.0, f2=10.0),
            dict(figure_id=1, prop="grid", bool_val=True),
            dict(figure_id=1, prop="xlabel", str_val="Time"),
            dict(figure_id=1, prop="color", f1=1.0, f2=0.0, f3=0.0, f4=1.0),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        props = []
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                while inner.next():
                    if inner.tag == P.TAG_PROPERTY_NAME:
                        props.append(inner.as_string())
        assert props == ["xlim", "grid", "xlabel", "color"]

    def test_empty_string_val_not_encoded(self):
        """Empty str_val should not produce a TAG_STR_VAL field."""
        updates = [
            dict(figure_id=1, prop="xlim", f1=0.0, f2=1.0, str_val=""),
        ]
        data = encode_req_update_batch(updates)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_BATCH_ITEM:
                blob = dec.as_blob()
                inner = PayloadDecoder(blob)
                has_str_val = False
                while inner.next():
                    if inner.tag == P.TAG_STR_VAL:
                        has_str_val = True
                assert not has_str_val
