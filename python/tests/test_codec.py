"""Tests for the TLV codec — verifies encode/decode round-trips and wire format."""

import struct
import sys
import os

# Ensure the spectra package is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._codec import PayloadEncoder, PayloadDecoder, encode_header, decode_header
from spectra._codec import (
    encode_hello,
    decode_welcome,
    encode_req_create_figure,
    decode_resp_figure_created,
    encode_req_create_axes,
    decode_resp_axes_created,
    encode_req_add_series,
    decode_resp_series_added,
    encode_req_set_data,
    encode_req_update_property,
    decode_resp_err,
    decode_resp_figure_list,
    decode_resp_ok,
    encode_req_show,
    encode_req_destroy_figure,
    encode_req_list_figures,
    encode_req_set_data_raw,
)
from spectra import _protocol as P


class TestPayloadEncoder:
    def test_put_u16(self):
        enc = PayloadEncoder()
        enc.put_u16(0x10, 42)
        data = enc.take()
        assert data[0] == 0x10
        assert struct.unpack_from("<I", data, 1)[0] == 2
        assert struct.unpack_from("<H", data, 5)[0] == 42

    def test_put_u32(self):
        enc = PayloadEncoder()
        enc.put_u32(0x20, 0xDEADBEEF)
        data = enc.take()
        assert data[0] == 0x20
        assert struct.unpack_from("<I", data, 1)[0] == 4
        assert struct.unpack_from("<I", data, 5)[0] == 0xDEADBEEF

    def test_put_u64(self):
        enc = PayloadEncoder()
        enc.put_u64(0x30, 0x123456789ABCDEF0)
        data = enc.take()
        assert data[0] == 0x30
        assert struct.unpack_from("<I", data, 1)[0] == 8
        assert struct.unpack_from("<Q", data, 5)[0] == 0x123456789ABCDEF0

    def test_put_string(self):
        enc = PayloadEncoder()
        enc.put_string(0x40, "hello")
        data = enc.take()
        assert data[0] == 0x40
        assert struct.unpack_from("<I", data, 1)[0] == 5
        assert data[5:10] == b"hello"

    def test_put_float(self):
        enc = PayloadEncoder()
        enc.put_float(0x50, 3.14)
        data = enc.take()
        # Should be tag(1) + len(4) + u32(4) = 9 bytes
        assert len(data) == 9
        assert data[0] == 0x50

    def test_put_bool(self):
        enc = PayloadEncoder()
        enc.put_bool(0x60, True)
        enc.put_bool(0x61, False)
        data = enc.take()
        # Two fields: each tag(1) + len(4) + u16(2) = 7 bytes
        assert len(data) == 14

    def test_put_float_array(self):
        enc = PayloadEncoder()
        enc.put_float_array(0x70, [1.0, 2.0, 3.0])
        data = enc.take()
        assert data[0] == 0x70
        blob_len = struct.unpack_from("<I", data, 1)[0]
        # 4 (count) + 3*4 (floats) = 16
        assert blob_len == 16


class TestPayloadDecoder:
    def test_roundtrip_u16(self):
        enc = PayloadEncoder()
        enc.put_u16(0x10, 1234)
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        assert dec.tag == 0x10
        assert dec.as_u16() == 1234
        assert not dec.next()

    def test_roundtrip_u32(self):
        enc = PayloadEncoder()
        enc.put_u32(0x20, 0xCAFEBABE)
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        assert dec.as_u32() == 0xCAFEBABE

    def test_roundtrip_u64(self):
        enc = PayloadEncoder()
        enc.put_u64(0x30, 0xFEDCBA9876543210)
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        assert dec.as_u64() == 0xFEDCBA9876543210

    def test_roundtrip_string(self):
        enc = PayloadEncoder()
        enc.put_string(0x40, "spectra")
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        assert dec.as_string() == "spectra"

    def test_roundtrip_float(self):
        enc = PayloadEncoder()
        enc.put_float(0x50, 2.5)
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        assert abs(dec.as_float() - 2.5) < 1e-6

    def test_roundtrip_bool(self):
        enc = PayloadEncoder()
        enc.put_bool(0x60, True)
        enc.put_bool(0x61, False)
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        assert dec.as_bool() is True
        assert dec.next()
        assert dec.as_bool() is False

    def test_roundtrip_float_array(self):
        enc = PayloadEncoder()
        enc.put_float_array(0x70, [1.5, 2.5, 3.5])
        dec = PayloadDecoder(enc.take())
        assert dec.next()
        arr = dec.as_float_array()
        assert len(arr) == 3
        assert abs(arr[0] - 1.5) < 1e-6
        assert abs(arr[1] - 2.5) < 1e-6
        assert abs(arr[2] - 3.5) < 1e-6

    def test_multiple_fields(self):
        enc = PayloadEncoder()
        enc.put_u16(0x10, 1)
        enc.put_u32(0x20, 2)
        enc.put_string(0x30, "abc")
        data = enc.take()

        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.tag == 0x10 and dec.as_u16() == 1
        assert dec.next()
        assert dec.tag == 0x20 and dec.as_u32() == 2
        assert dec.next()
        assert dec.tag == 0x30 and dec.as_string() == "abc"
        assert not dec.next()

    def test_empty_data(self):
        dec = PayloadDecoder(b"")
        assert not dec.next()

    def test_skip_unknown_tags(self):
        enc = PayloadEncoder()
        enc.put_u16(0x10, 42)
        enc.put_u32(0xFF, 999)  # unknown tag
        enc.put_u16(0x11, 43)
        dec = PayloadDecoder(enc.take())

        found = {}
        while dec.next():
            found[dec.tag] = True
        assert 0x10 in found
        assert 0xFF in found
        assert 0x11 in found


class TestHeader:
    def test_roundtrip(self):
        hdr = encode_header(
            msg_type=P.HELLO,
            payload_len=100,
            seq=1,
            request_id=2,
            session_id=3,
            window_id=4,
        )
        assert len(hdr) == P.HEADER_SIZE

        decoded = decode_header(hdr)
        assert decoded is not None
        assert decoded["type"] == P.HELLO
        assert decoded["payload_len"] == 100
        assert decoded["seq"] == 1
        assert decoded["request_id"] == 2
        assert decoded["session_id"] == 3
        assert decoded["window_id"] == 4

    def test_magic_bytes(self):
        hdr = encode_header(msg_type=P.HELLO, payload_len=0)
        assert hdr[0:2] == P.MAGIC

    def test_bad_magic(self):
        hdr = bytearray(encode_header(msg_type=P.HELLO, payload_len=0))
        hdr[0] = 0xFF
        assert decode_header(bytes(hdr)) is None

    def test_too_short(self):
        assert decode_header(b"\x53\x50") is None


class TestHelloPayload:
    def test_encode(self):
        data = encode_hello(client_type="python", build="test-build")
        dec = PayloadDecoder(data)
        found_type = False
        found_build = False
        while dec.next():
            if dec.tag == P.TAG_CLIENT_TYPE:
                assert dec.as_string() == "python"
                found_type = True
            elif dec.tag == P.TAG_AGENT_BUILD:
                assert dec.as_string() == "test-build"
                found_build = True
        assert found_type
        assert found_build


class TestPythonPayloads:
    def test_create_figure(self):
        data = encode_req_create_figure(title="Test", width=800, height=600)
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_TITLE:
                found["title"] = dec.as_string()
            elif dec.tag == P.TAG_WIDTH:
                found["width"] = dec.as_u32()
            elif dec.tag == P.TAG_HEIGHT:
                found["height"] = dec.as_u32()
        assert found["title"] == "Test"
        assert found["width"] == 800
        assert found["height"] == 600

    def test_create_axes(self):
        data = encode_req_create_axes(figure_id=42, rows=2, cols=3, index=4)
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found["figure_id"] = dec.as_u64()
            elif dec.tag == P.TAG_GRID_ROWS:
                found["rows"] = dec.as_u32()
            elif dec.tag == P.TAG_GRID_COLS:
                found["cols"] = dec.as_u32()
            elif dec.tag == P.TAG_GRID_INDEX:
                found["index"] = dec.as_u32()
        assert found["figure_id"] == 42
        assert found["rows"] == 2
        assert found["cols"] == 3
        assert found["index"] == 4

    def test_add_series(self):
        data = encode_req_add_series(figure_id=1, axes_index=0, series_type="line", label="data")
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found["figure_id"] = dec.as_u64()
            elif dec.tag == P.TAG_AXES_INDEX:
                found["axes_index"] = dec.as_u32()
            elif dec.tag == P.TAG_SERIES_TYPE:
                found["type"] = dec.as_string()
            elif dec.tag == P.TAG_SERIES_LABEL:
                found["label"] = dec.as_string()
        assert found["figure_id"] == 1
        assert found["axes_index"] == 0
        assert found["type"] == "line"
        assert found["label"] == "data"

    def test_set_data(self):
        data = encode_req_set_data(figure_id=1, series_index=0, data=[1.0, 2.0, 3.0, 4.0])
        dec = PayloadDecoder(data)
        found_data = None
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_data = dec.as_float_array()
        assert found_data is not None
        assert len(found_data) == 4
        assert abs(found_data[0] - 1.0) < 1e-6
        assert abs(found_data[3] - 4.0) < 1e-6

    def test_set_data_raw(self):
        raw = struct.pack("<4f", 1.0, 2.0, 3.0, 4.0)
        data = encode_req_set_data_raw(figure_id=1, series_index=0, raw_bytes=raw, count=4)
        dec = PayloadDecoder(data)
        found_data = None
        while dec.next():
            if dec.tag == P.TAG_BLOB_INLINE:
                found_data = dec.as_float_array()
        assert found_data is not None
        assert len(found_data) == 4

    def test_update_property(self):
        data = encode_req_update_property(
            figure_id=1, series_index=2, prop="color",
            f1=1.0, f2=0.5, f3=0.0, f4=1.0,
        )
        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_PROPERTY_NAME:
                found["prop"] = dec.as_string()
            elif dec.tag == P.TAG_F1:
                found["f1"] = dec.as_float()
        assert found["prop"] == "color"
        assert abs(found["f1"] - 1.0) < 1e-6

    def test_show(self):
        data = encode_req_show(figure_id=7)
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.tag == P.TAG_FIGURE_ID
        assert dec.as_u64() == 7

    def test_destroy_figure(self):
        data = encode_req_destroy_figure(figure_id=99)
        dec = PayloadDecoder(data)
        assert dec.next()
        assert dec.tag == P.TAG_FIGURE_ID
        assert dec.as_u64() == 99

    def test_list_figures_empty(self):
        data = encode_req_list_figures()
        assert data == b""

    def test_decode_resp_figure_created(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 10)
        enc.put_u64(P.TAG_FIGURE_ID, 42)
        req_id, fig_id = decode_resp_figure_created(enc.take())
        assert req_id == 10
        assert fig_id == 42

    def test_decode_resp_axes_created(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 11)
        enc.put_u32(P.TAG_AXES_INDEX, 3)
        req_id, idx = decode_resp_axes_created(enc.take())
        assert req_id == 11
        assert idx == 3

    def test_decode_resp_series_added(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 12)
        enc.put_u32(P.TAG_SERIES_INDEX, 5)
        req_id, idx = decode_resp_series_added(enc.take())
        assert req_id == 12
        assert idx == 5

    def test_decode_resp_err(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 13)
        enc.put_u32(P.TAG_ERROR_CODE, 404)
        enc.put_string(P.TAG_ERROR_MESSAGE, "Not found")
        req_id, code, message = decode_resp_err(enc.take())
        assert req_id == 13
        assert code == 404
        assert message == "Not found"

    def test_decode_resp_figure_list(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 14)
        enc.put_u32(0x45, 2)  # TAG_FIGURE_COUNT
        enc.put_u64(P.TAG_FIGURE_IDS, 100)
        enc.put_u64(P.TAG_FIGURE_IDS, 200)
        req_id, ids = decode_resp_figure_list(enc.take())
        assert req_id == 14
        assert ids == [100, 200]

    def test_decode_resp_ok(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 15)
        req_id = decode_resp_ok(enc.take())
        assert req_id == 15

    def test_decode_welcome(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_SESSION_ID, 1)
        enc.put_u64(P.TAG_WINDOW_ID, 0)
        enc.put_u64(P.TAG_PROCESS_ID, 12345)
        enc.put_u32(P.TAG_HEARTBEAT_MS, 5000)
        enc.put_string(P.TAG_MODE, "multiproc")
        result = decode_welcome(enc.take())
        assert result["session_id"] == 1
        assert result["process_id"] == 12345
        assert result["mode"] == "multiproc"
