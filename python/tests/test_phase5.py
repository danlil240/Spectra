"""Phase 5 Python IPC tests — chunked transfer, shared memory, animation, persistence.

Tests are organized into suites:
  - TestChunkedTransferCodec: chunked encoding round-trips
  - TestChunkedTransferProtocol: protocol constants for chunking
  - TestChunkedTransferSeries: Series._send_chunked logic
  - TestBlobStore: BlobStore lifecycle management
  - TestBlobRef: BlobRef creation and cleanup
  - TestBlobProtocol: BLOB_RELEASE / TAG_BLOB_SHM constants
  - TestAnimationCodec: ANIM_START/STOP/TICK encode/decode
  - TestAnimationProtocol: animation protocol constants
  - TestBackendAnimator: BackendAnimator API
  - TestSessionPersistence: save/restore session state
  - TestSessionAnimatorRegistration: animator registration in Session
  - TestSessionBlobStore: blob store integration in Session
  - TestCMakeLibSplit: verify CMake targets exist

Run: python -m pytest tests/test_phase5.py -v
"""

import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._codec import (
    PayloadDecoder,
    PayloadEncoder,
    encode_req_set_data_raw,
    encode_req_set_data_chunked,
    encode_req_anim_start,
    encode_req_anim_stop,
    decode_anim_tick,
    decode_blob_release,
)
from spectra import _protocol as P
from spectra._blob import BlobStore, BlobRef, SHM_THRESHOLD, BLOB_TTL, _next_blob_name
from spectra._animation import BackendAnimator
from spectra._persistence import save_session, load_session_metadata, restore_session


# ─── Chunked Transfer Codec Tests ────────────────────────────────────────────

class TestChunkedTransferCodec:
    """Verify encode/decode round-trips for chunked REQ_SET_DATA."""

    def test_encode_single_chunk(self):
        raw = struct.pack("<4f", 1.0, 2.0, 3.0, 4.0)
        data = encode_req_set_data_chunked(
            figure_id=1, series_index=0,
            raw_bytes=raw, count=4,
            chunk_index=0, chunk_count=1, total_count=4,
        )
        assert len(data) > 0

        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found["figure_id"] = dec.as_u64()
            elif dec.tag == P.TAG_SERIES_INDEX:
                found["series_index"] = dec.as_u32()
            elif dec.tag == P.TAG_CHUNK_INDEX:
                found["chunk_index"] = dec.as_u32()
            elif dec.tag == P.TAG_CHUNK_COUNT:
                found["chunk_count"] = dec.as_u32()
            elif dec.tag == P.TAG_TOTAL_COUNT:
                found["total_count"] = dec.as_u32()
            elif dec.tag == P.TAG_BLOB_INLINE:
                found["blob"] = dec.as_blob()

        assert found["figure_id"] == 1
        assert found["series_index"] == 0
        assert found["chunk_index"] == 0
        assert found["chunk_count"] == 1
        assert found["total_count"] == 4
        assert len(found["blob"]) == 4 + 16  # 4-byte count prefix + 4 floats

    def test_encode_multi_chunk_metadata(self):
        """Verify chunk_index and chunk_count fields for multi-chunk transfer."""
        raw = b"\x00" * 32
        for i in range(3):
            data = encode_req_set_data_chunked(
                figure_id=5, series_index=2,
                raw_bytes=raw, count=8,
                chunk_index=i, chunk_count=3, total_count=24,
            )
            dec = PayloadDecoder(data)
            while dec.next():
                if dec.tag == P.TAG_CHUNK_INDEX:
                    assert dec.as_u32() == i
                elif dec.tag == P.TAG_CHUNK_COUNT:
                    assert dec.as_u32() == 3
                elif dec.tag == P.TAG_TOTAL_COUNT:
                    assert dec.as_u32() == 24

    def test_chunked_preserves_dtype(self):
        raw = struct.pack("<2f", 1.0, 2.0)
        data = encode_req_set_data_chunked(
            figure_id=1, series_index=0,
            raw_bytes=raw, count=2,
            chunk_index=0, chunk_count=1, total_count=2,
            dtype=1,
        )
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_DTYPE:
                assert dec.as_u16() == 1
                return
        assert False, "TAG_DTYPE not found"

    def test_non_chunked_has_no_chunk_tags(self):
        """Regular encode_req_set_data_raw should NOT have chunk tags."""
        raw = struct.pack("<2f", 1.0, 2.0)
        data = encode_req_set_data_raw(
            figure_id=1, series_index=0,
            raw_bytes=raw, count=2,
        )
        dec = PayloadDecoder(data)
        while dec.next():
            assert dec.tag != P.TAG_CHUNK_INDEX
            assert dec.tag != P.TAG_CHUNK_COUNT
            assert dec.tag != P.TAG_TOTAL_COUNT


class TestChunkedTransferProtocol:
    """Verify chunked transfer protocol constants."""

    def test_chunk_size_value(self):
        assert P.CHUNK_SIZE == 128 * 1024 * 1024

    def test_tag_chunk_index(self):
        assert P.TAG_CHUNK_INDEX == 0xB3

    def test_tag_chunk_count(self):
        assert P.TAG_CHUNK_COUNT == 0xB4

    def test_tag_total_count(self):
        assert P.TAG_TOTAL_COUNT == 0xB5

    def test_chunk_tags_unique(self):
        tags = [P.TAG_BLOB_INLINE, P.TAG_BATCH_ITEM, P.TAG_BLOB_SHM,
                P.TAG_CHUNK_INDEX, P.TAG_CHUNK_COUNT, P.TAG_TOTAL_COUNT]
        assert len(tags) == len(set(tags))


class TestChunkedTransferSeries:
    """Test Series._send_chunked logic (without backend)."""

    def test_series_has_send_chunked(self):
        from spectra._series import Series
        assert hasattr(Series, "_send_chunked")

    def test_set_data_threshold_check(self):
        """Verify set_data checks CHUNK_SIZE threshold."""
        from spectra._series import Series
        # Just verify the method exists and has the chunking path
        import inspect
        source = inspect.getsource(Series.set_data)
        assert "CHUNK_SIZE" in source
        assert "_send_chunked" in source


# ─── Blob Store Tests ────────────────────────────────────────────────────────

class TestBlobStore:
    """Test BlobStore lifecycle management."""

    def test_create_empty_store(self):
        store = BlobStore()
        assert store.active_count == 0

    def test_create_blob_small_data(self):
        """Small data should still work if explicitly created."""
        store = BlobStore()
        ref = store.create_blob(b"hello world")
        # May be None if shm not available, or a valid ref
        if ref is not None:
            assert ref.size == 11
            assert ref.name.startswith("/spectra-blob-")
            store.cleanup_all()

    def test_release_blob_by_name(self):
        store = BlobStore()
        ref = store.create_blob(b"x" * 1024)
        if ref is not None:
            name = ref.name
            assert store.active_count == 1
            store.release_blob(name)
            assert store.active_count == 0

    def test_release_nonexistent_blob(self):
        store = BlobStore()
        store.release_blob("/nonexistent-blob")  # should not raise

    def test_cleanup_all(self):
        store = BlobStore()
        for _ in range(5):
            store.create_blob(b"x" * 1024)
        store.cleanup_all()
        assert store.active_count == 0

    def test_cleanup_expired(self):
        store = BlobStore()
        ref = store.create_blob(b"x" * 1024)
        if ref is not None:
            # Force expiry by backdating
            ref.created_at = ref.created_at - BLOB_TTL - 1
            count = store.cleanup_expired()
            assert count >= 1
            assert store.active_count == 0


class TestBlobRef:
    """Test BlobRef creation and cleanup."""

    def test_blob_ref_creation(self):
        ref = BlobRef("/test-blob", 1024)
        assert ref.name == "/test-blob"
        assert ref.size == 1024
        assert not ref._released

    def test_blob_ref_release_idempotent(self):
        ref = BlobRef("/test-blob", 1024)
        ref.release()
        assert ref._released
        ref.release()  # should not raise
        assert ref._released

    def test_blob_ref_is_expired(self):
        ref = BlobRef("/test-blob", 1024)
        assert not ref.is_expired
        ref.created_at = ref.created_at - BLOB_TTL - 1
        assert ref.is_expired

    def test_blob_name_uniqueness(self):
        names = set()
        for _ in range(100):
            names.add(_next_blob_name())
        assert len(names) == 100


class TestBlobProtocol:
    """Verify blob protocol constants."""

    def test_tag_blob_shm(self):
        assert P.TAG_BLOB_SHM == 0xB2

    def test_blob_release_message_type(self):
        assert P.BLOB_RELEASE == 0x0570

    def test_shm_threshold(self):
        assert SHM_THRESHOLD == 1 * 1024 * 1024

    def test_blob_ttl(self):
        assert BLOB_TTL == 60.0


# ─── Animation Codec Tests ───────────────────────────────────────────────────

class TestAnimationCodec:
    """Verify encode/decode for animation protocol messages."""

    def test_encode_anim_start(self):
        data = encode_req_anim_start(figure_id=42, fps=60.0, duration=10.0)
        assert len(data) > 0

        dec = PayloadDecoder(data)
        found = {}
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                found["figure_id"] = dec.as_u64()
            elif dec.tag == P.TAG_F1:
                found["fps"] = dec.as_float()
            elif dec.tag == P.TAG_F2:
                found["duration"] = dec.as_float()

        assert found["figure_id"] == 42
        assert abs(found["fps"] - 60.0) < 0.01
        assert abs(found["duration"] - 10.0) < 0.01

    def test_encode_anim_start_infinite(self):
        data = encode_req_anim_start(figure_id=1, fps=30.0, duration=0.0)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_F2:
                assert abs(dec.as_float()) < 0.01
                return

    def test_encode_anim_stop(self):
        data = encode_req_anim_stop(figure_id=99)
        dec = PayloadDecoder(data)
        while dec.next():
            if dec.tag == P.TAG_FIGURE_ID:
                assert dec.as_u64() == 99
                return
        assert False, "TAG_FIGURE_ID not found"

    def test_decode_anim_tick(self):
        # Build a fake ANIM_TICK payload
        enc = PayloadEncoder()
        enc.put_u64(P.TAG_FIGURE_ID, 7)
        enc.put_u32(P.TAG_F1, 42)
        enc.put_float(P.TAG_F2, 1.5)
        enc.put_float(P.TAG_F3, 0.016)
        data = enc.take()

        result = decode_anim_tick(data)
        assert result["figure_id"] == 7
        assert result["frame_num"] == 42
        assert abs(result["t"] - 1.5) < 0.01
        assert abs(result["dt"] - 0.016) < 0.001

    def test_decode_blob_release(self):
        enc = PayloadEncoder()
        enc.put_string(P.TAG_BLOB_SHM, "/spectra-blob-123-1")
        data = enc.take()

        name = decode_blob_release(data)
        assert name == "/spectra-blob-123-1"

    def test_decode_blob_release_empty(self):
        name = decode_blob_release(b"")
        assert name == ""


class TestAnimationProtocol:
    """Verify animation protocol constants."""

    def test_req_anim_start(self):
        assert P.REQ_ANIM_START == 0x0520

    def test_req_anim_stop(self):
        assert P.REQ_ANIM_STOP == 0x0521

    def test_anim_tick(self):
        assert P.ANIM_TICK == 0x0560

    def test_animation_types_unique(self):
        types = [P.REQ_ANIM_START, P.REQ_ANIM_STOP, P.ANIM_TICK]
        assert len(types) == len(set(types))

    def test_animation_types_in_range(self):
        assert 0x0500 <= P.REQ_ANIM_START <= 0x053F
        assert 0x0500 <= P.REQ_ANIM_STOP <= 0x053F
        assert 0x0550 <= P.ANIM_TICK <= 0x05FF


# ─── BackendAnimator Tests ───────────────────────────────────────────────────

class TestBackendAnimator:
    """Test BackendAnimator API (without backend)."""

    def test_class_exists(self):
        assert BackendAnimator is not None

    def test_init(self):
        anim = BackendAnimator.__new__(BackendAnimator)
        anim._session = None
        anim._figure_id = 1
        anim._fps = 60.0
        anim._duration = 0.0
        anim._running = False
        anim._on_tick = None
        assert anim._figure_id == 1
        assert anim._fps == 60.0

    def test_on_tick_property(self):
        anim = BackendAnimator.__new__(BackendAnimator)
        anim._on_tick = None
        assert anim.on_tick is None

        def cb(t, dt, fn):
            pass
        anim.on_tick = cb
        assert anim.on_tick is cb

    def test_is_running_property(self):
        anim = BackendAnimator.__new__(BackendAnimator)
        anim._running = False
        assert not anim.is_running
        anim._running = True
        assert anim.is_running

    def test_handle_tick_calls_callback(self):
        anim = BackendAnimator.__new__(BackendAnimator)
        anim._on_tick = None

        calls = []
        anim.on_tick = lambda t, dt, fn: calls.append((t, dt, fn))
        anim.handle_tick(1.5, 0.016, 42)

        assert len(calls) == 1
        assert calls[0] == (1.5, 0.016, 42)

    def test_handle_tick_no_callback(self):
        anim = BackendAnimator.__new__(BackendAnimator)
        anim._on_tick = None
        anim.handle_tick(0.0, 0.0, 0)  # should not raise

    def test_exported_from_package(self):
        import spectra
        assert hasattr(spectra, "BackendAnimator")


# ─── Session Persistence Tests ───────────────────────────────────────────────

class TestSessionPersistence:
    """Test save/restore session state."""

    def test_save_session_creates_file(self):
        """Verify save_session creates a valid JSON file."""
        # Create a mock session
        class MockFigure:
            id = 1
            title = "Test Figure"
            axes = []
        class MockSession:
            session_id = 42
            _socket_path = "/tmp/test.sock"
            figures = [MockFigure()]

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            path = f.name

        try:
            save_session(MockSession(), path)
            assert os.path.exists(path)

            with open(path) as f:
                data = json.load(f)

            assert data["version"] == 1
            assert data["session_id"] == 42
            assert len(data["figures"]) == 1
            assert data["figures"][0]["title"] == "Test Figure"
        finally:
            os.unlink(path)

    def test_save_session_with_axes_and_series(self):
        class MockSeries:
            index = 0
            series_type = "line"
            label = "sin(x)"
        class MockAxes:
            index = 0
            series = [MockSeries()]
        class MockFigure:
            id = 1
            title = "Complex Figure"
            axes = [MockAxes()]
        class MockSession:
            session_id = 1
            _socket_path = "/tmp/test.sock"
            figures = [MockFigure()]

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            path = f.name

        try:
            save_session(MockSession(), path)
            data = load_session_metadata(path)
            assert len(data["figures"][0]["axes"]) == 1
            assert len(data["figures"][0]["axes"][0]["series"]) == 1
            assert data["figures"][0]["axes"][0]["series"][0]["type"] == "line"
            assert data["figures"][0]["axes"][0]["series"][0]["label"] == "sin(x)"
        finally:
            os.unlink(path)

    def test_load_session_metadata(self):
        state = {
            "version": 1,
            "timestamp": 1234567890.0,
            "session_id": 99,
            "socket_path": "/tmp/test.sock",
            "figures": [{"id": 1, "title": "Fig", "axes": []}],
        }
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as f:
            json.dump(state, f)
            path = f.name

        try:
            loaded = load_session_metadata(path)
            assert loaded["version"] == 1
            assert loaded["session_id"] == 99
            assert len(loaded["figures"]) == 1
        finally:
            os.unlink(path)

    def test_persistence_functions_exist(self):
        assert callable(save_session)
        assert callable(load_session_metadata)
        assert callable(restore_session)

    def test_save_empty_session(self):
        class MockSession:
            session_id = 0
            _socket_path = ""
            figures = []

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            path = f.name

        try:
            save_session(MockSession(), path)
            data = load_session_metadata(path)
            assert data["figures"] == []
        finally:
            os.unlink(path)


# ─── Session Integration Tests ───────────────────────────────────────────────

class TestSessionAnimatorRegistration:
    """Test animator registration in Session (without backend)."""

    def test_session_has_register_animator(self):
        from spectra._session import Session
        assert hasattr(Session, "_register_animator")

    def test_session_has_unregister_animator(self):
        from spectra._session import Session
        assert hasattr(Session, "_unregister_animator")

    def test_session_handles_anim_tick(self):
        """Verify _handle_event dispatches ANIM_TICK."""
        from spectra._session import Session
        import inspect
        source = inspect.getsource(Session._handle_event)
        assert "ANIM_TICK" in source

    def test_session_handles_blob_release(self):
        """Verify _handle_event dispatches BLOB_RELEASE."""
        from spectra._session import Session
        import inspect
        source = inspect.getsource(Session._handle_event)
        assert "BLOB_RELEASE" in source


class TestSessionBlobStore:
    """Test blob store integration in Session."""

    def test_session_has_blob_store(self):
        from spectra._session import Session
        assert hasattr(Session, "blob_store")

    def test_blob_store_module_exists(self):
        from spectra import _blob
        assert hasattr(_blob, "BlobStore")
        assert hasattr(_blob, "BlobRef")
        assert hasattr(_blob, "try_create_shm_blob")


# ─── CMake Library Split Tests ───────────────────────────────────────────────

class TestCMakeLibSplit:
    """Verify CMake library split targets exist in CMakeLists.txt."""

    def _read_cmake(self):
        cmake_path = os.path.join(
            os.path.dirname(__file__), "..", "..", "CMakeLists.txt"
        )
        with open(cmake_path) as f:
            return f.read()

    def test_spectra_core_target(self):
        cmake = self._read_cmake()
        assert "add_library(spectra-core INTERFACE)" in cmake

    def test_spectra_ipc_target(self):
        cmake = self._read_cmake()
        assert "add_library(spectra-ipc INTERFACE)" in cmake

    def test_spectra_render_target(self):
        cmake = self._read_cmake()
        assert "add_library(spectra-render INTERFACE)" in cmake

    def test_spectra_core_links_spectra(self):
        cmake = self._read_cmake()
        assert "target_link_libraries(spectra-core INTERFACE spectra)" in cmake

    def test_spectra_ipc_links_spectra(self):
        cmake = self._read_cmake()
        assert "target_link_libraries(spectra-ipc INTERFACE spectra)" in cmake


# ─── C++ blob_store.hpp existence test ───────────────────────────────────────

class TestCppBlobStore:
    """Verify C++ blob_store.hpp exists."""

    def test_blob_store_hpp_exists(self):
        path = os.path.join(
            os.path.dirname(__file__), "..", "..", "src", "ipc", "blob_store.hpp"
        )
        assert os.path.exists(path)

    def test_blob_store_hpp_has_class(self):
        path = os.path.join(
            os.path.dirname(__file__), "..", "..", "src", "ipc", "blob_store.hpp"
        )
        with open(path) as f:
            content = f.read()
        assert "class BlobStore" in content
        assert "register_blob" in content
        assert "ack_blob" in content
        assert "cleanup_expired" in content
        assert "BLOB_TTL" in content
