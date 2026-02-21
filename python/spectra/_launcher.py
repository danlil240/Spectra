"""Auto-launch spectra-backend daemon if not already running."""

import os
import shutil
import subprocess
import time
from typing import Optional


def resolve_socket_path(explicit: Optional[str] = None) -> str:
    """Resolve the socket path using the priority order from the architecture plan.

    1. Explicit path passed to Session(socket=...)
    2. $SPECTRA_SOCKET environment variable
    3. $XDG_RUNTIME_DIR/spectra/spectra.sock
    4. /tmp/spectra-$USER/spectra.sock
    """
    if explicit:
        return explicit

    env_sock = os.environ.get("SPECTRA_SOCKET")
    if env_sock:
        return env_sock

    xdg = os.environ.get("XDG_RUNTIME_DIR")
    user = os.environ.get("USER", os.environ.get("LOGNAME", "unknown"))

    if xdg:
        return os.path.join(xdg, "spectra", "spectra.sock")

    return f"/tmp/spectra-{user}/spectra.sock"


def _can_connect(path: str) -> bool:
    """Check if a backend is already listening on the socket."""
    import socket as _socket

    try:
        s = _socket.socket(_socket.AF_UNIX, _socket.SOCK_STREAM)
        s.settimeout(0.5)
        s.connect(path)
        s.close()
        return True
    except OSError:
        return False


def _find_backend_binary() -> Optional[str]:
    """Find the spectra-backend binary."""
    # Check SPECTRA_BACKEND_PATH env var
    env_path = os.environ.get("SPECTRA_BACKEND_PATH")
    if env_path and os.path.isfile(env_path) and os.access(env_path, os.X_OK):
        return env_path

    # Check PATH
    found = shutil.which("spectra-backend")
    if found:
        return found

    # Heuristic: look in common build directories relative to the project root.
    # The Python package lives at <project>/python/spectra/, so project root is ../../
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.normpath(os.path.join(pkg_dir, "..", ".."))
    for build_dir in ("build", "cmake-build-debug", "cmake-build-release", "out/build"):
        candidate = os.path.join(project_root, build_dir, "spectra-backend")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    return None


def ensure_backend(socket_path: str, timeout: float = 5.0) -> str:
    """Ensure a backend is running. Returns the socket path.

    If a backend is already listening, returns immediately.
    Otherwise, launches one and waits for it to be ready.
    """
    if _can_connect(socket_path):
        return socket_path

    binary = _find_backend_binary()
    if binary is None:
        raise RuntimeError(
            "spectra-backend binary not found. "
            "Set SPECTRA_BACKEND_PATH or add it to PATH."
        )

    # Ensure socket directory exists
    sock_dir = os.path.dirname(socket_path)
    if sock_dir:
        os.makedirs(sock_dir, mode=0o700, exist_ok=True)

    # Remove stale socket file
    if os.path.exists(socket_path):
        try:
            os.unlink(socket_path)
        except OSError:
            pass

    # Launch backend
    _debug_log = os.environ.get("SPECTRA_DEBUG_LOG")
    _stderr_target = open(_debug_log, "w") if _debug_log else subprocess.PIPE
    subprocess.Popen(
        [binary, "--socket", socket_path],
        stdout=subprocess.DEVNULL,
        stderr=_stderr_target,
        start_new_session=True,
    )

    # Wait for socket to appear and be connectable
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _can_connect(socket_path):
            return socket_path
        time.sleep(0.05)

    raise RuntimeError(
        f"spectra-backend did not start within {timeout}s. "
        f"Socket: {socket_path}"
    )
