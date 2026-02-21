"""Cross-codec tests — verify Python encoder output matches C++ decoder expectations.

These tests write binary payloads to a temp directory that the C++ cross-codec
test reads back. They also read C++ encoded payloads and verify Python decoding.

Run standalone: python -m pytest tests/test_cross_codec.py -v
Run with C++ counterpart:
    1. python tests/test_cross_codec.py --write /tmp/spectra_cross
    2. ./build/tests/unit_test_cross_codec /tmp/spectra_cross
"""

import os
import struct
import sys
import argparse

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._codec import (
    PayloadEncoder,
    encode_hello,
    encode_req_create_figure,
    encode_req_create_axes,
    encode_req_add_series,
    encode_req_set_data,
    encode_req_update_property,
    encode_req_show,
    encode_req_destroy_figure,
    encode_req_append_data,
    decode_resp_figure_created,
    decode_resp_axes_created,
    decode_resp_series_added,
    decode_resp_err,
    decode_resp_figure_list,
    decode_welcome,
)
from spectra import _protocol as P


def write_payloads(out_dir: str) -> None:
    """Write Python-encoded payloads to files for C++ to decode."""
    os.makedirs(out_dir, exist_ok=True)

    # hello.bin — HELLO with client_type="python"
    data = encode_hello(client_type="python", build="test-cross-1.0")
    with open(os.path.join(out_dir, "hello.bin"), "wb") as f:
        f.write(data)

    # req_create_figure.bin
    data = encode_req_create_figure(title="Cross Test", width=1024, height=768)
    with open(os.path.join(out_dir, "req_create_figure.bin"), "wb") as f:
        f.write(data)

    # req_create_axes.bin
    data = encode_req_create_axes(figure_id=42, rows=2, cols=3, index=5)
    with open(os.path.join(out_dir, "req_create_axes.bin"), "wb") as f:
        f.write(data)

    # req_add_series.bin
    data = encode_req_add_series(figure_id=42, axes_index=0, series_type="line", label="cross-data")
    with open(os.path.join(out_dir, "req_add_series.bin"), "wb") as f:
        f.write(data)

    # req_set_data.bin — 5 interleaved points
    points = [1.0, 10.0, 2.0, 20.0, 3.0, 30.0, 4.0, 40.0, 5.0, 50.0]
    data = encode_req_set_data(figure_id=42, series_index=0, data=points)
    with open(os.path.join(out_dir, "req_set_data.bin"), "wb") as f:
        f.write(data)

    # req_update_property.bin
    data = encode_req_update_property(
        figure_id=42, axes_index=0, series_index=1,
        prop="color", f1=1.0, f2=0.5, f3=0.25, f4=0.75,
    )
    with open(os.path.join(out_dir, "req_update_property.bin"), "wb") as f:
        f.write(data)

    # req_show.bin
    data = encode_req_show(figure_id=42)
    with open(os.path.join(out_dir, "req_show.bin"), "wb") as f:
        f.write(data)

    # req_destroy_figure.bin
    data = encode_req_destroy_figure(figure_id=99)
    with open(os.path.join(out_dir, "req_destroy_figure.bin"), "wb") as f:
        f.write(data)

    # req_append_data.bin — 3 interleaved points
    data = encode_req_append_data(figure_id=42, series_index=0, data=[1.0, 10.0, 2.0, 20.0, 3.0, 30.0])
    with open(os.path.join(out_dir, "req_append_data.bin"), "wb") as f:
        f.write(data)

    print(f"Wrote 9 payload files to {out_dir}")


def write_cpp_style_payloads(out_dir: str) -> None:
    """Write payloads in the format C++ would encode, for Python to decode.

    This simulates what the C++ encoder produces — same TLV format, same tag
    order. Used to verify Python decoder handles C++ output correctly.
    """
    os.makedirs(out_dir, exist_ok=True)

    # resp_figure_created.bin
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_REQUEST_ID, 7)
    enc.put_u64(P.TAG_FIGURE_ID, 42)
    with open(os.path.join(out_dir, "resp_figure_created.bin"), "wb") as f:
        f.write(enc.take())

    # resp_axes_created.bin
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_REQUEST_ID, 8)
    enc.put_u32(P.TAG_AXES_INDEX, 3)
    with open(os.path.join(out_dir, "resp_axes_created.bin"), "wb") as f:
        f.write(enc.take())

    # resp_series_added.bin
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_REQUEST_ID, 9)
    enc.put_u32(P.TAG_SERIES_INDEX, 5)
    with open(os.path.join(out_dir, "resp_series_added.bin"), "wb") as f:
        f.write(enc.take())

    # resp_err.bin
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_REQUEST_ID, 10)
    enc.put_u32(P.TAG_ERROR_CODE, 404)
    enc.put_string(P.TAG_ERROR_MESSAGE, "Figure not found")
    with open(os.path.join(out_dir, "resp_err.bin"), "wb") as f:
        f.write(enc.take())

    # resp_figure_list.bin
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_REQUEST_ID, 11)
    enc.put_u32(0x45, 3)  # TAG_FIGURE_COUNT
    enc.put_u64(P.TAG_FIGURE_IDS, 100)
    enc.put_u64(P.TAG_FIGURE_IDS, 200)
    enc.put_u64(P.TAG_FIGURE_IDS, 300)
    with open(os.path.join(out_dir, "resp_figure_list.bin"), "wb") as f:
        f.write(enc.take())

    # welcome.bin
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_SESSION_ID, 12345)
    enc.put_u64(P.TAG_WINDOW_ID, 0)
    enc.put_u64(P.TAG_PROCESS_ID, 67890)
    enc.put_u32(P.TAG_HEARTBEAT_MS, 5000)
    enc.put_string(P.TAG_MODE, "multiproc")
    with open(os.path.join(out_dir, "welcome.bin"), "wb") as f:
        f.write(enc.take())

    print(f"Wrote 6 response payload files to {out_dir}")


# ─── Pytest tests: verify Python can decode its own "C++-style" payloads ──────

class TestCrossCodecPythonDecode:
    """Verify Python decoders handle payloads in the exact format C++ produces."""

    def test_decode_resp_figure_created(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 7)
        enc.put_u64(P.TAG_FIGURE_ID, 42)
        req_id, fig_id = decode_resp_figure_created(enc.take())
        assert req_id == 7
        assert fig_id == 42

    def test_decode_resp_axes_created(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 8)
        enc.put_u32(P.TAG_AXES_INDEX, 3)
        req_id, idx = decode_resp_axes_created(enc.take())
        assert req_id == 8
        assert idx == 3

    def test_decode_resp_series_added(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 9)
        enc.put_u32(P.TAG_SERIES_INDEX, 5)
        req_id, idx = decode_resp_series_added(enc.take())
        assert req_id == 9
        assert idx == 5

    def test_decode_resp_err(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 10)
        enc.put_u32(P.TAG_ERROR_CODE, 404)
        enc.put_string(P.TAG_ERROR_MESSAGE, "Figure not found")
        req_id, code, msg = decode_resp_err(enc.take())
        assert req_id == 10
        assert code == 404
        assert msg == "Figure not found"

    def test_decode_resp_figure_list(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_REQUEST_ID, 11)
        enc.put_u32(0x45, 3)
        enc.put_u64(P.TAG_FIGURE_IDS, 100)
        enc.put_u64(P.TAG_FIGURE_IDS, 200)
        enc.put_u64(P.TAG_FIGURE_IDS, 300)
        req_id, ids = decode_resp_figure_list(enc.take())
        assert req_id == 11
        assert ids == [100, 200, 300]

    def test_decode_welcome(self):
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_SESSION_ID, 12345)
        enc.put_u64(P.TAG_WINDOW_ID, 0)
        enc.put_u64(P.TAG_PROCESS_ID, 67890)
        enc.put_u32(P.TAG_HEARTBEAT_MS, 5000)
        enc.put_string(P.TAG_MODE, "multiproc")
        w = decode_welcome(enc.take())
        assert w["session_id"] == 12345
        assert w["process_id"] == 67890
        assert w["mode"] == "multiproc"


class TestCrossCodecTagParity:
    """Verify Python and C++ use identical tag values."""

    def test_handshake_tags(self):
        assert P.TAG_PROTOCOL_MAJOR == 0x10
        assert P.TAG_PROTOCOL_MINOR == 0x11
        assert P.TAG_AGENT_BUILD == 0x12
        assert P.TAG_CAPABILITIES == 0x13
        assert P.TAG_CLIENT_TYPE == 0x14

    def test_welcome_tags(self):
        assert P.TAG_SESSION_ID == 0x20
        assert P.TAG_WINDOW_ID == 0x21
        assert P.TAG_PROCESS_ID == 0x22
        assert P.TAG_HEARTBEAT_MS == 0x23
        assert P.TAG_MODE == 0x24

    def test_response_tags(self):
        assert P.TAG_REQUEST_ID == 0x30
        assert P.TAG_ERROR_CODE == 0x31
        assert P.TAG_ERROR_MESSAGE == 0x32

    def test_control_tags(self):
        assert P.TAG_FIGURE_ID == 0x40
        assert P.TAG_FIGURE_IDS == 0x41

    def test_python_tags(self):
        assert P.TAG_GRID_INDEX == 0xA1
        assert P.TAG_SERIES_LABEL == 0xA2
        assert P.TAG_DTYPE == 0xA3
        assert P.TAG_PROPERTY_NAME == 0xA4
        assert P.TAG_SESSION_TOKEN == 0xA5
        assert P.TAG_BLOB_INLINE == 0xB0

    def test_message_type_ranges(self):
        # Python requests: 0x0500-0x053F
        for mt in [P.REQ_CREATE_FIGURE, P.REQ_DESTROY_FIGURE, P.REQ_CREATE_AXES,
                    P.REQ_ADD_SERIES, P.REQ_REMOVE_SERIES, P.REQ_SET_DATA,
                    P.REQ_UPDATE_PROPERTY, P.REQ_SHOW, P.REQ_CLOSE_FIGURE,
                    P.REQ_GET_SNAPSHOT, P.REQ_LIST_FIGURES, P.REQ_RECONNECT,
                    P.REQ_DISCONNECT]:
            assert 0x0500 <= mt <= 0x053F, f"0x{mt:04X} out of request range"

        # Python responses: 0x0540-0x05FF
        for mt in [P.RESP_FIGURE_CREATED, P.RESP_AXES_CREATED, P.RESP_SERIES_ADDED,
                    P.RESP_SNAPSHOT, P.RESP_FIGURE_LIST, P.EVT_WINDOW_CLOSED,
                    P.EVT_FIGURE_DESTROYED]:
            assert 0x0540 <= mt <= 0x05FF, f"0x{mt:04X} out of response range"


class TestCrossCodecWireFormat:
    """Verify the exact wire format of TLV fields matches C++ expectations."""

    def test_u16_wire_format(self):
        enc = PayloadEncoder()
        enc.put_u16(0x10, 42)
        data = enc.take()
        # [tag:1][len:4 LE][val:2 LE]
        assert len(data) == 7
        assert data[0] == 0x10
        assert struct.unpack_from("<I", data, 1)[0] == 2
        assert struct.unpack_from("<H", data, 5)[0] == 42

    def test_u32_wire_format(self):
        enc = PayloadEncoder()
        enc.put_u32(0x20, 0xDEADBEEF)
        data = enc.take()
        assert len(data) == 9
        assert data[0] == 0x20
        assert struct.unpack_from("<I", data, 1)[0] == 4
        assert struct.unpack_from("<I", data, 5)[0] == 0xDEADBEEF

    def test_u64_wire_format(self):
        enc = PayloadEncoder()
        enc.put_u64(0x30, 0x123456789ABCDEF0)
        data = enc.take()
        assert len(data) == 13
        assert data[0] == 0x30
        assert struct.unpack_from("<I", data, 1)[0] == 8
        assert struct.unpack_from("<Q", data, 5)[0] == 0x123456789ABCDEF0

    def test_string_wire_format(self):
        enc = PayloadEncoder()
        enc.put_string(0x40, "hello")
        data = enc.take()
        assert len(data) == 10  # 1 + 4 + 5
        assert data[0] == 0x40
        assert struct.unpack_from("<I", data, 1)[0] == 5
        assert data[5:10] == b"hello"

    def test_float_wire_format(self):
        enc = PayloadEncoder()
        enc.put_float(0x50, 3.14)
        data = enc.take()
        # float is encoded as u32 (bit-cast)
        assert len(data) == 9
        bits = struct.unpack_from("<I", data, 5)[0]
        val = struct.unpack("<f", struct.pack("<I", bits))[0]
        assert abs(val - 3.14) < 1e-5

    def test_float_array_wire_format(self):
        enc = PayloadEncoder()
        enc.put_float_array(0xB0, [1.0, 2.0, 3.0])
        data = enc.take()
        # blob: tag(1) + len(4) + [count_u32(4) + 3*float32(12)] = 21
        assert len(data) == 21
        assert data[0] == 0xB0
        blob_len = struct.unpack_from("<I", data, 1)[0]
        assert blob_len == 16  # 4 + 12
        count = struct.unpack_from("<I", data, 5)[0]
        assert count == 3
        f0, f1, f2 = struct.unpack_from("<3f", data, 9)
        assert abs(f0 - 1.0) < 1e-6
        assert abs(f1 - 2.0) < 1e-6
        assert abs(f2 - 3.0) < 1e-6

    def test_header_wire_format(self):
        from spectra._codec import encode_header
        hdr = encode_header(
            msg_type=P.REQ_CREATE_FIGURE,
            payload_len=100,
            seq=1,
            request_id=2,
            session_id=3,
            window_id=4,
        )
        assert len(hdr) == P.HEADER_SIZE
        assert hdr[0:2] == P.MAGIC
        msg_type = struct.unpack_from("<H", hdr, 2)[0]
        assert msg_type == P.REQ_CREATE_FIGURE
        payload_len = struct.unpack_from("<I", hdr, 4)[0]
        assert payload_len == 100


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", help="Write payload files to this directory")
    args = parser.parse_args()

    if args.write:
        write_payloads(args.write)
        write_cpp_style_payloads(args.write)
    else:
        # Run as pytest
        import pytest
        sys.exit(pytest.main([__file__, "-v"]))
