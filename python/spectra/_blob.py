"""POSIX shared memory blob management for zero-copy large array transfer.

For arrays > 1 MB, data can be placed in shared memory instead of being
sent inline through the socket. The backend and agents open the same
shm segment read-only, avoiding copies.

Memory lifetime:
  - Python creates shm, writes data
  - Backend and agent open shm read-only
  - Backend sends BLOB_RELEASE to Python when all agents have ACK'd
  - Python calls shm.unlink() after receiving BLOB_RELEASE
  - If Python crashes before unlink: backend unlinks after TTL (60s)
"""

import os
import time
import threading
from typing import Optional, Dict

# Shared memory threshold: use shm for arrays > 1 MB
SHM_THRESHOLD = 1 * 1024 * 1024

# TTL for unreleased blobs (seconds)
BLOB_TTL = 60.0

# Counter for unique blob names
_blob_counter = 0
_blob_lock = threading.Lock()


def _next_blob_name() -> str:
    """Generate a unique shm segment name."""
    global _blob_counter
    with _blob_lock:
        _blob_counter += 1
        return f"/spectra-blob-{os.getpid()}-{_blob_counter}"


class BlobRef:
    """Reference to a shared memory blob."""

    __slots__ = ("name", "size", "created_at", "_shm", "_released")

    def __init__(self, name: str, size: int) -> None:
        self.name = name
        self.size = size
        self.created_at = time.monotonic()
        self._shm = None
        self._released = False

    def release(self) -> None:
        """Unlink the shared memory segment."""
        if self._released:
            return
        self._released = True
        if self._shm is not None:
            try:
                self._shm.close()
                self._shm.unlink()
            except Exception:
                pass
            self._shm = None

    @property
    def is_expired(self) -> bool:
        return (time.monotonic() - self.created_at) > BLOB_TTL

    def __del__(self) -> None:
        self.release()


class BlobStore:
    """Manages active shared memory blobs for a session.

    Tracks all created blobs and handles cleanup on release or timeout.
    """

    def __init__(self) -> None:
        self._blobs: Dict[str, BlobRef] = {}
        self._lock = threading.Lock()

    def create_blob(self, data: bytes) -> Optional[BlobRef]:
        """Create a shared memory segment and write data into it.

        Returns a BlobRef on success, None if shm is not available.
        """
        try:
            from multiprocessing import shared_memory
        except ImportError:
            return None

        name = _next_blob_name()
        try:
            shm = shared_memory.SharedMemory(name=name, create=True, size=len(data))
            shm.buf[:len(data)] = data
            ref = BlobRef(name, len(data))
            ref._shm = shm

            with self._lock:
                self._blobs[name] = ref

            return ref
        except (OSError, ValueError):
            return None

    def release_blob(self, name: str) -> None:
        """Release a blob by name (called when BLOB_RELEASE received)."""
        with self._lock:
            ref = self._blobs.pop(name, None)
        if ref is not None:
            ref.release()

    def cleanup_expired(self) -> int:
        """Release all expired blobs. Returns count of cleaned blobs."""
        expired = []
        with self._lock:
            for name, ref in list(self._blobs.items()):
                if ref.is_expired:
                    expired.append(name)
                    del self._blobs[name]

        for name in expired:
            # Try to unlink even if BlobRef is gone
            try:
                from multiprocessing import shared_memory
                shm = shared_memory.SharedMemory(name=name, create=False)
                shm.close()
                shm.unlink()
            except Exception:
                pass

        return len(expired)

    def cleanup_all(self) -> None:
        """Release all blobs (called on session close)."""
        with self._lock:
            refs = list(self._blobs.values())
            self._blobs.clear()
        for ref in refs:
            ref.release()

    @property
    def active_count(self) -> int:
        with self._lock:
            return len(self._blobs)


def try_create_shm_blob(data: bytes, store: BlobStore) -> Optional[BlobRef]:
    """Try to create a shm blob for data. Returns None if data is too small
    or shm is not available on this platform."""
    if len(data) < SHM_THRESHOLD:
        return None
    return store.create_blob(data)
