"""Low-level client for connecting to Spectra's automation socket."""

import json
import os
import socket
from typing import Any


def default_socket_path() -> str:
    """Return the default automation socket path for the current user's Spectra instance."""
    # Try SPECTRA_AUTO_SOCKET env var first
    env = os.environ.get("SPECTRA_AUTO_SOCKET", "")
    if env:
        return env

    # Scan /tmp for spectra-auto-*.sock
    import glob

    candidates = sorted(glob.glob("/tmp/spectra-auto-*.sock"))
    if candidates:
        return candidates[0]

    return ""


class SpectraClient:
    """Connects to Spectra's automation Unix socket and sends JSON commands."""

    def __init__(self, socket_path: str | None = None, timeout: float = 30.0):
        self._socket_path = socket_path or default_socket_path()
        self._timeout = timeout
        self._sock: socket.socket | None = None

    @property
    def socket_path(self) -> str:
        return self._socket_path

    def connect(self) -> None:
        """Connect to the Spectra automation socket."""
        if self._sock is not None:
            return

        if not self._socket_path:
            raise ConnectionError(
                "No Spectra automation socket found. "
                "Is Spectra running? Set SPECTRA_AUTO_SOCKET or start Spectra first."
            )

        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(self._timeout)
        try:
            self._sock.connect(self._socket_path)
        except (ConnectionRefusedError, FileNotFoundError) as e:
            self._sock = None
            raise ConnectionError(
                f"Cannot connect to Spectra at {self._socket_path}: {e}"
            ) from e

    def close(self) -> None:
        """Close the connection."""
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def send(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        """Send a JSON command and return the parsed response."""
        if self._sock is None:
            self.connect()

        request = {"method": method, "params": params or {}}
        line = json.dumps(request, separators=(",", ":")) + "\n"

        assert self._sock is not None
        self._sock.sendall(line.encode("utf-8"))

        # Read newline-delimited response
        buf = b""
        while b"\n" not in buf:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise ConnectionError("Connection closed by Spectra")
            buf += chunk

        response_line = buf.split(b"\n", 1)[0]
        return json.loads(response_line)

    def __enter__(self) -> "SpectraClient":
        self.connect()
        return self

    def __exit__(self, *args: Any) -> None:
        self.close()
