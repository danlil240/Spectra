"""Auto-launch spectra-backend daemon if not already running."""

import os
import platform as _platform
import shutil
import subprocess
import sys
import tempfile
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
    # Mirrors spectra::ipc::runtime_dir() in src/ipc/transport.cpp:
    # $XDG_RUNTIME_DIR when a `spectra-<pid>.sock` path fits in
    # sockaddr_un.sun_path, else std::filesystem::temp_directory_path()
    # ($TMPDIR, falling back to /tmp).
    xdg = os.environ.get("XDG_RUNTIME_DIR")
    if xdg:
        worst_case = os.path.join(xdg, "spectra-4194304.sock")
        if _socket_path_fits(worst_case):
            return xdg
        log.debug(
            "launcher XDG_RUNTIME_DIR too long for a UNIX socket (%d bytes), using temp dir",
            len(xdg),
        )
    return _posix_temp_dir()


def _posix_temp_dir() -> str:
    """Equivalent of libstdc++/libc++ std::filesystem::temp_directory_path()."""
    for var in ("TMPDIR", "TMP", "TEMP", "TEMPDIR"):
        val = os.environ.get(var)
        if val:
            if os.path.isdir(val):
                return val.rstrip("/") or "/"
            break  # C++ reports an error for an invalid first match; fall back
    return "/tmp" if os.path.isdir("/tmp") else tempfile.gettempdir()


def _sun_path_max() -> int:
    """Usable bytes in sockaddr_un.sun_path (incl. NUL): 108 Linux, 104 BSD/macOS."""
    return 108 if sys.platform.startswith("linux") else 104


def _socket_path_fits(path: str) -> bool:
    if _IS_WINDOWS:
        return True
    return len(os.fsencode(path)) < _sun_path_max()


def _discover_live_brokers() -> list[str]:
    """Return live `spectra-*.sock` entries in the runtime dir, newest first.

    Mirrors the C++ Publisher discovery (`src/ipc/publisher_client.cpp`) and
    the inproc daemon-attach logic (`src/app/main.cpp`): scan the flat runtime
    directory for `spectra-<pid>.sock` entries newest first and keep entries
    that respond to a connect probe. The publisher still performs the full
    HELLO/WELCOME/DECLARE handshake on each candidate before trusting it.
    """
    dir_ = _runtime_socket_dir()
    try:
        entries = os.listdir(dir_)
    except OSError:
        return []
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
    live = []
    for _, path in candidates:
        if _can_connect(path):
            log.debug("launcher discovered live broker at %s", path)
            live.append(path)
    return live


def _discover_live_broker() -> Optional[str]:
    """Return the newest live `spectra-*.sock` in the runtime dir, or None."""
    brokers = _discover_live_brokers()
    return brokers[0] if brokers else None


def resolve_socket_candidates(explicit: Optional[str] = None) -> list[str]:
    """Resolve candidate broker socket paths, ordered by preference."""
    if explicit:
        return [explicit]

    env_sock = os.environ.get("SPECTRA_SOCKET")
    if env_sock:
        return [env_sock]

    return _discover_live_brokers()


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
    candidates = resolve_socket_candidates(explicit)
    if candidates:
        return candidates[0]

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
    5. Previously downloaded binary in the user cache, only if it matches the
       installed package version and passes its SHA-256 manifest check
       (see ``spectra._download``)
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
        _dl_hint = ""
        from ._download import auto_download_enabled

        if auto_download_enabled():
            # Explicit opt-in (SPECTRA_AUTO_DOWNLOAD=1): fetch the release that
            # matches this package version, verified against SHA256SUMS.
            try:
                from ._download import download_backend

                log.info("launcher attempting verified backend download")
                download_backend()
                binary = _find_backend_binary()
            except Exception as dl_err:
                _dl_hint = f"\nVerified download failed: {dl_err}"
        else:
            _dl_hint = (
                "\nTo fetch a checksum-verified pre-built backend for this version, run "
                "`spectra-backend --download` (or set SPECTRA_AUTO_DOWNLOAD=1)."
            )
        if binary is None:
            raise RuntimeError(
                "spectra-backend binary not found. "
                "Install a spectra-plot platform wheel (bundles the binaries), "
                "set SPECTRA_BACKEND_PATH, or build with CMake "
                "(cmake -B build -DSPECTRA_RUNTIME_MODE=multiproc)."
                + _dl_hint
            )

    if socket_path and not _socket_path_fits(socket_path):
        raise RuntimeError(
            f"Socket path is too long for a UNIX domain socket "
            f"({len(os.fsencode(socket_path))} bytes, limit {_sun_path_max() - 1}): "
            f"{socket_path}\nUse a shorter SPECTRA_SOCKET / XDG_RUNTIME_DIR."
        )

    if not socket_path and not _IS_WINDOWS:
        xdg = os.environ.get("XDG_RUNTIME_DIR")
        if xdg and _runtime_socket_dir() != xdg:
            # XDG_RUNTIME_DIR is too long: the daemon's default path would not
            # fit sun_path. Pin a short path in the fallback dir instead.
            socket_path = os.path.join(_runtime_socket_dir(), f"spectra-{os.getpid()}.sock")
            log.info("launcher XDG_RUNTIME_DIR too long, pinning socket=%s", socket_path)

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

    # Launch backend. Its stderr goes to a file (never an unread PIPE: a
    # long-lived daemon would block once the pipe buffer fills) so that a
    # startup failure can be reported with the backend's own error message.
    _debug_log = os.environ.get("SPECTRA_DEBUG_LOG")
    if _debug_log:
        stderr_path = _debug_log
        _stderr_target = open(_debug_log, "w")
        _owns_stderr_file = False
    else:
        import tempfile

        fd, stderr_path = tempfile.mkstemp(prefix="spectra-backend-", suffix=".log")
        _stderr_target = os.fdopen(fd, "w")
        _owns_stderr_file = True
    popen_kwargs: dict = dict(
        stdin=subprocess.DEVNULL,
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
    try:
        proc = subprocess.Popen(argv, **popen_kwargs)
    finally:
        _stderr_target.close()  # the child holds its own descriptor
    log.info(
        "launcher started backend pid=%s binary=%s socket=%s",
        proc.pid, binary, socket_path or "<default>",
    )

    def _started(path: str) -> str:
        log.info("launcher backend ready socket=%s", path)
        if _owns_stderr_file and not _IS_WINDOWS:
            # The daemon keeps its descriptor; drop the name so temp logs do
            # not pile up (set SPECTRA_DEBUG_LOG to keep the backend log).
            try:
                os.unlink(stderr_path)
            except OSError:
                pass
        return path

    # Wait for socket to appear and be connectable.
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if socket_path:
            if _can_connect(socket_path):
                return _started(socket_path)
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
                    return _started(p)
        # If the process already exited, don't keep waiting
        if proc.poll() is not None:
            break
        time.sleep(0.05)

    exit_code = proc.poll()
    stderr_text = _read_tail(stderr_path)
    if _owns_stderr_file:
        try:
            os.unlink(stderr_path)
        except OSError:
            pass

    if exit_code is not None:
        what = f"spectra-backend exited during startup with code {exit_code}"
    else:
        what = f"spectra-backend did not start within {timeout}s"
    hint = ""
    if stderr_text:
        hint = f"\nBackend stderr:\n{stderr_text}"
    shown_socket = socket_path or os.path.join(_runtime_socket_dir(), "spectra-<pid>.sock")
    raise RuntimeError(f"{what}.\nBinary: {binary}\nSocket: {shown_socket}{hint}")


def _read_tail(path: str, max_bytes: int = 8192) -> str:
    """Return the last *max_bytes* of a text file (best effort)."""
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(0, size - max_bytes))
            data = f.read()
    except OSError:
        return ""
    return data.decode(errors="replace").rstrip()
