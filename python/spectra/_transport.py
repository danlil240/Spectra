"""Socket I/O + message framing for the Spectra IPC protocol."""

import socket
from typing import Optional

from . import _protocol as P
from . import _codec as codec
from ._errors import ConnectionError, ProtocolError


class Transport:
    """Wraps a Unix domain socket connection with framed message send/recv."""

    __slots__ = ("_sock", "_seq")

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._seq = 0

    @staticmethod
    def connect(path: str, timeout: float = 5.0) -> "Transport":
        """Connect to a Unix domain socket at the given path."""
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            sock.connect(path)
            return Transport(sock)
        except OSError as e:
            raise ConnectionError(f"Failed to connect to {path}: {e}") from e

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    @property
    def is_open(self) -> bool:
        return self._sock is not None

    def fileno(self) -> int:
        if self._sock is None:
            return -1
        return self._sock.fileno()

    def send(
        self,
        msg_type: int,
        payload: bytes = b"",
        request_id: int = 0,
        session_id: int = 0,
        window_id: int = 0,
    ) -> int:
        """Send a framed message. Returns the sequence number used."""
        if self._sock is None:
            raise ConnectionError("Not connected")

        self._seq += 1
        seq = self._seq

        header = codec.encode_header(
            msg_type=msg_type,
            payload_len=len(payload),
            seq=seq,
            request_id=request_id,
            session_id=session_id,
            window_id=window_id,
        )

        data = header + payload
        try:
            self._sendall(data)
        except OSError as e:
            self.close()
            raise ConnectionError(f"Send failed: {e}") from e

        return seq

    def recv(self) -> Optional[dict]:
        """Receive a framed message. Returns dict with 'header' and 'payload' keys.

        Returns None if the connection was closed cleanly.
        Raises ConnectionError on I/O error, ProtocolError on framing error.
        """
        if self._sock is None:
            raise ConnectionError("Not connected")

        # Read header
        header_bytes = self._recvall(P.HEADER_SIZE)
        if header_bytes is None:
            return None  # clean close

        hdr = codec.decode_header(header_bytes)
        if hdr is None:
            raise ProtocolError("Invalid message header (bad magic)")

        payload_len = hdr["payload_len"]
        if payload_len > P.MAX_PAYLOAD_SIZE:
            raise ProtocolError(f"Payload too large: {payload_len}")

        # Read payload
        if payload_len > 0:
            payload = self._recvall(payload_len)
            if payload is None:
                raise ConnectionError("Connection closed during payload read")
        else:
            payload = b""

        return {"header": hdr, "payload": payload}

    def _sendall(self, data: bytes) -> None:
        """Send all bytes, retrying on partial writes."""
        view = memoryview(data)
        total = len(data)
        sent = 0
        while sent < total:
            n = self._sock.send(view[sent:])
            if n == 0:
                raise ConnectionError("Connection closed during send")
            sent += n

    def _recvall(self, nbytes: int) -> Optional[bytes]:
        """Receive exactly nbytes. Returns None on clean close."""
        buf = bytearray()
        while len(buf) < nbytes:
            try:
                chunk = self._sock.recv(nbytes - len(buf))
            except OSError as e:
                raise ConnectionError(f"Recv failed: {e}") from e
            if not chunk:
                if len(buf) == 0:
                    return None  # clean close
                raise ConnectionError("Connection closed mid-message")
            buf.extend(chunk)
        return bytes(buf)
