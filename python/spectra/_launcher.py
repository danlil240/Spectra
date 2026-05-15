"""Auto-launch spectra-backend daemon if not already running."""

import os
import platform as _platform
import shutil
import subprocess
import sys
import time
from typing import Optional

from ._log import log

_IS_WINDOWS = _platform.system() == "Windows"


def _runtime_socket_dir() -> str:
    """Directory the C++ side (daemon + inproc broker) places broker sockets in."""
    if _IS_WINDOWS:
        temp = os.environ.get(
            "TEMP",
            os.environ.get(
                "TMP",
                os.path.join(os.path.expanduser("~"), "AppData", "Local", "Temp"),
            ),
        )
        return os.path.join(temp, "spectra")
    xdg = os.environ.get("XDG_RUNTIME_DIR")
    if xdg:
        return xdg
    return "/tmp"


def _discover_live_broker() -> Optional[str]:
    """Return the newest live `spectra-*.sock` in the runtime dir, or None.

    Mirrors the C++ Publisher discovery (`src/ipc/publisher_client.cpp`) and
    the inproc daemon-attach logic (`src/app/main.cpp`): scan the flat runtime
    directory for `spectra-<pid>.sock` entries newest first and pick the first
    that responds to a connect probe. This is what lets a Python publisher
    converge on the same broker as an inproc C++ app.
    """
    dir_ = _runtime_socket_dir()
    try:
        entries = os.listdir(dir_)
    except OSError:
        return None
    candidates = []
    for name in entries:
        if not name.startswith("spectra-") or not name.endswith(".sock"):
            continue
        # Skip the automation socket (`spectra-auto-<pid>.sock`) — that's the
        # UI automation server, not the topic broker.
        if name.startswith("spectra-auto-"):
            continue
        full = os.path.join(dir_, name)
        try:
            mtime = os.path.getmtime(full)
        except OSError:
            continue
        candidates.append((mtime, full))
    candidates.sort(reverse=True)
    for _, path in candidates:
        if _can_connect(path):
            log.debug("launcher discovered live broker at %s", path)
            return path
    return None


def resolve_socket_path(explicit: Optional[str] = None) -> str:
    """Resolve the broker socket path.

    Priority (matches the C++ Publisher in ``src/ipc/publisher_client.cpp``):

    1. Explicit path passed by the caller.
    2. ``$SPECTRA_SOCKET`` environment variable.
    3. Newest live ``spectra-<pid>.sock`` in the runtime dir (daemon *or*
       in-process app broker).
    4. Empty string sentinel — meaning "no broker yet". ``ensure_backend()``
       will launch one and discover its socket path.
    """
    if explicit:
        return explicit

    env_sock = os.environ.get("SPECTRA_SOCKET")
    if env_sock:
        return env_sock

    discovered = _discover_live_broker()
    if discovered:
        return discovered

    # No live broker yet. Return an empty sentinel; ensure_backend() will
    # launch one and report the path it ended up on.
    return ""


def _can_connect(path: str) -> bool:
    """Check if a backend is already listening and responsive on the socket.

    Connects, then does a non-blocking recv peek to detect backends that
    accept-then-immediately-close (shutting down).
    """
    import socket as _socket
    import select as _select

    af_unix = getattr(_socket, "AF_UNIX", None)
    if af_unix is None:
        log.debug("launcher _can_connect: AF_UNIX not available")
        return False

    try:
        s = _socket.socket(af_unix, _socket.SOCK_STREAM)
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
        log.debug("launcher _can_connect: healthy backend at %s", path)
        return True
    except OSError:
        log.debug("launcher _can_connect: unavailable %s", path)
        return False


def _is_native_binary(path: str) -> bool:
    """Return True if *path* looks like a compiled (ELF / Mach-O / PE) executable
    rather than a script wrapper (e.g. the pip-installed console_scripts shim).
    """
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
        # ELF: \x7fELF  |  Mach-O: \xfe\xed\xfa\xce / \xcf\xfa\xed\xfe
        # PE (Windows): MZ
        return (
            magic[:4] == b"\x7fELF"
            or magic[:4] in (
                b"\xfe\xed\xfa\xce",
                b"\xfe\xed\xfa\xcf",
                b"\xcf\xfa\xed\xfe",
                b"\xce\xfa\xed\xfe",
            )
            or magic[:2] == b"MZ"
        )
    except OSError:
        return False


def _find_backend_binary() -> Optional[str]:
    """Find the spectra-backend binary.

    Search order:
    1. $SPECTRA_BACKEND_PATH env var
    2. Bundled binary inside pip-installed package (_bin/spectra-backend[.exe])
    3. Heuristic: common build directories relative to project root
    4. System PATH (filtered — skips script wrappers to avoid exec loops)
    5. Previously downloaded binary in user cache
    """
    _exe = ".exe" if _IS_WINDOWS else ""

    # 1. Explicit env var
    env_path = os.environ.get("SPECTRA_BACKEND_PATH")
    if env_path and os.path.isfile(env_path) and os.access(env_path, os.X_OK):
        log.debug("launcher backend binary via env: %s", env_path)
        return env_path

    # 2. Bundled binary (pip install spectra-plot ships backend in _bin/)
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    bundled = os.path.join(pkg_dir, "_bin", f"spectra-backend{_exe}")
    if os.path.isfile(bundled) and os.access(bundled, os.X_OK):
        log.debug("launcher backend binary bundled: %s", bundled)
        return bundled

    # 3. Heuristic: look in common build directories relative to the project root.
    # The Python package lives at <project>/python/spectra/, so project root is ../../
    # Checked BEFORE system PATH so a freshly-built local binary is preferred
    # over a potentially stale installed one.
    project_root = os.path.normpath(os.path.join(pkg_dir, "..", ".."))
    for build_dir in ("build", "build_release", "build_debug", "build_asan",
                       "cmake-build-debug", "cmake-build-release", "out/build"):
        candidate = os.path.join(project_root, build_dir, f"spectra-backend{_exe}")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            log.debug("launcher backend binary from build dir: %s", candidate)
            return candidate

    # 4. System PATH (fallback — may find stale installed binaries).
    #    Skip script wrappers (e.g. the pip console_scripts shim) to avoid
    #    an infinite os.execv loop where the wrapper calls _find_backend_binary
    #    which finds the wrapper again.
    found = shutil.which("spectra-backend")
    if found and _is_native_binary(found):
        log.debug("launcher backend binary from PATH: %s", found)
        return found

    # 5. Previously downloaded binary in user cache
    from ._download import find_cached_backend
    cached = find_cached_backend()
    if cached:
        log.debug("launcher backend binary from cache: %s", cached)
        return cached

    log.debug("launcher backend binary not found")
    return None


def ensure_backend(socket_path: str, timeout: float = 5.0) -> str:
    """Ensure a backend is running. Returns the socket path actually in use.

    Behaviour:

    * If ``socket_path`` is non-empty and a backend is already listening
      there, return it unchanged.
    * If ``socket_path`` is non-empty but nothing is listening, launch
      ``spectra-backend --socket <socket_path>`` and wait for it.
    * If ``socket_path`` is empty (no pinned path and no live broker was
      discovered), launch ``spectra-backend`` without a ``--socket`` argument
      so it picks its default ``$XDG_RUNTIME_DIR/spectra-<daemon_pid>.sock``,
      poll the runtime directory until a live broker appears, and return
      its path.
    """
    if socket_path and _can_connect(socket_path):
        log.info("launcher reusing existing backend socket=%s", socket_path)
        return socket_path

    # A live broker may have appeared between resolve_socket_path() and now —
    # e.g. the user just launched an inproc Spectra app in another terminal.
    rediscovered = _discover_live_broker()
    if rediscovered and (not socket_path or rediscovered == socket_path):
        log.info("launcher attaching to live broker socket=%s", rediscovered)
        return rediscovered

    binary = _find_backend_binary()
    if binary is None:
        # No binary found locally — try downloading from GitHub Releases
        if os.environ.get("SPECTRA_NO_DOWNLOAD", "").lower() not in ("1", "true", "yes"):
            try:
                from ._download import download_backend
                log.info("launcher attempting backend auto-download")
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

    # Ensure socket directory exists (only meaningful when we have a path).
    if socket_path:
        sock_dir = os.path.dirname(socket_path)
        if sock_dir:
            os.makedirs(sock_dir, mode=0o700, exist_ok=True)

        # Remove stale socket file
        if os.path.exists(socket_path):
            try:
                log.debug("launcher removing stale socket: %s", socket_path)
                os.unlink(socket_path)
            except OSError:
                pass

    # Launch backend
    _debug_log = os.environ.get("SPECTRA_DEBUG_LOG")
    _stderr_target = open(_debug_log, "w") if _debug_log else subprocess.PIPE
    popen_kwargs: dict = dict(
        stdout=subprocess.DEVNULL,
        stderr=_stderr_target,
    )
    if _IS_WINDOWS:
        # CREATE_NO_WINDOW (0x08000000) hides the console window.
        # CREATE_NEW_PROCESS_GROUP (0x200) detaches from the parent group.
        popen_kwargs["creationflags"] = 0x08000000 | 0x00000200
    else:
        popen_kwargs["start_new_session"] = True

    # Snapshot existing broker sockets so we can identify the newly-launched
    # daemon's socket when --socket is not pinned.
    pre_existing: set = set()
    if not socket_path:
        try:
            pre_existing = {
                os.path.join(_runtime_socket_dir(), n)
                for n in os.listdir(_runtime_socket_dir())
                if n.startswith("spectra-") and n.endswith(".sock")
                and not n.startswith("spectra-auto-")
            }
        except OSError:
            pre_existing = set()

    argv = [binary]
    if socket_path:
        argv += ["--socket", socket_path]
    proc = subprocess.Popen(argv, **popen_kwargs)
    log.info(
        "launcher started backend pid=%s binary=%s socket=%s",
        proc.pid, binary, socket_path or "<default>",
    )

    # Wait for socket to appear and be connectable.
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if socket_path:
            if _can_connect(socket_path):
                log.info("launcher backend ready socket=%s", socket_path)
                return socket_path
        else:
            # Find a newly-created spectra-*.sock that wasn't there before.
            try:
                current = {
                    os.path.join(_runtime_socket_dir(), n)
                    for n in os.listdir(_runtime_socket_dir())
                    if n.startswith("spectra-") and n.endswith(".sock")
                    and not n.startswith("spectra-auto-")
                }
            except OSError:
                current = set()
            for p in sorted(current - pre_existing):
                if _can_connect(p):
                    log.info("launcher backend ready socket=%s", p)
                    return p
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
        f"Socket: {socket_path or '<default>'}{hint}"
    )
