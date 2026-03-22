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
    """Check if a backend is already listening and responsive on the socket.

    Connects, then does a non-blocking recv peek to detect backends that
    accept-then-immediately-close (shutting down).
    """
    import socket as _socket
    import select as _select

    try:
        s = _socket.socket(_socket.AF_UNIX, _socket.SOCK_STREAM)
        s.settimeout(1.0)
        s.connect(path)
        # Peek for immediate close/reset — a healthy backend won't send
        # anything until it receives HELLO, so readable here means EOF/reset.
        ready, _, _ = _select.select([s], [], [], 0.05)
        if ready:
            data = s.recv(1, _socket.MSG_PEEK)
            if not data:
                # Server accepted then immediately closed — dying backend
                s.close()
                return False
        s.close()
        return True
    except OSError:
        return False


def _is_native_binary(path: str) -> bool:
    """Return True if *path* looks like a compiled (ELF / Mach-O) executable
    rather than a script wrapper (e.g. the pip-installed console_scripts shim).
    """
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
        # ELF: \x7fELF  |  Mach-O: \xfe\xed\xfa\xce / \xcf\xfa\xed\xfe
        return magic[:4] == b"\x7fELF" or magic[:4] in (
            b"\xfe\xed\xfa\xce",
            b"\xfe\xed\xfa\xcf",
            b"\xcf\xfa\xed\xfe",
            b"\xce\xfa\xed\xfe",
        )
    except OSError:
        return False


def _find_backend_binary() -> Optional[str]:
    """Find the spectra-backend binary.

    Search order:
    1. $SPECTRA_BACKEND_PATH env var
    2. Bundled binary inside pip-installed package (_bin/spectra-backend)
    3. Heuristic: common build directories relative to project root
    4. System PATH (filtered — skips script wrappers to avoid exec loops)
    5. Previously downloaded binary in user cache
    """
    # 1. Explicit env var
    env_path = os.environ.get("SPECTRA_BACKEND_PATH")
    if env_path and os.path.isfile(env_path) and os.access(env_path, os.X_OK):
        return env_path

    # 2. Bundled binary (pip install spectra-plot ships backend in _bin/)
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    bundled = os.path.join(pkg_dir, "_bin", "spectra-backend")
    if os.path.isfile(bundled) and os.access(bundled, os.X_OK):
        return bundled

    # 3. Heuristic: look in common build directories relative to the project root.
    # The Python package lives at <project>/python/spectra/, so project root is ../../
    # Checked BEFORE system PATH so a freshly-built local binary is preferred
    # over a potentially stale installed one.
    project_root = os.path.normpath(os.path.join(pkg_dir, "..", ".."))
    for build_dir in ("build", "build_release", "build_debug", "build_asan",
                       "cmake-build-debug", "cmake-build-release", "out/build"):
        candidate = os.path.join(project_root, build_dir, "spectra-backend")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    # 4. System PATH (fallback — may find stale installed binaries).
    #    Skip script wrappers (e.g. the pip console_scripts shim) to avoid
    #    an infinite os.execv loop where the wrapper calls _find_backend_binary
    #    which finds the wrapper again.
    found = shutil.which("spectra-backend")
    if found and _is_native_binary(found):
        return found

    # 5. Previously downloaded binary in user cache
    from ._download import find_cached_backend
    cached = find_cached_backend()
    if cached:
        return cached

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
        # No binary found locally — try downloading from GitHub Releases
        if os.environ.get("SPECTRA_NO_DOWNLOAD", "").lower() not in ("1", "true", "yes"):
            try:
                from ._download import download_backend
                download_backend()
                binary = _find_backend_binary()
            except Exception as dl_err:
                # Download failed — fall through to the error below with a hint
                _dl_hint = f"\nAuto-download failed: {dl_err}"
            else:
                _dl_hint = ""
        else:
            _dl_hint = "\n(Auto-download disabled by SPECTRA_NO_DOWNLOAD)"
        if binary is None:
            raise RuntimeError(
                "spectra-backend binary not found. "
                "Install the spectra-plot wheel (includes the binary), "
                "set SPECTRA_BACKEND_PATH, or build with CMake "
                "(cmake -B build -DSPECTRA_RUNTIME_MODE=multiproc)."
                + _dl_hint
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
    proc = subprocess.Popen(
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
        # If the process already exited, don't keep waiting
        if proc.poll() is not None:
            break
        time.sleep(0.05)

    # Collect stderr for the error message if available
    hint = ""
    if proc.poll() is not None and _stderr_target == subprocess.PIPE:
        stderr_bytes = proc.stderr.read() if proc.stderr else b""
        if stderr_bytes:
            hint = f"\nBackend stderr:\n{stderr_bytes.decode(errors='replace').rstrip()}"

    raise RuntimeError(
        f"spectra-backend did not start within {timeout}s. "
        f"Binary: {binary}\n"
        f"Socket: {socket_path}{hint}"
    )
