#!/usr/bin/env python3
"""Tests for Windows compatibility in launcher, transport, and CLI modules."""
import os
import sys
import tempfile
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "python"))

from spectra._launcher import (
    resolve_socket_path,
    _is_native_binary,
    _find_backend_binary,
    _can_connect,
)


class TestResolveSocketPath:
    """Test socket path resolution across platforms."""

    def test_explicit_path_takes_priority(self):
        assert resolve_socket_path("/custom/path.sock") == "/custom/path.sock"

    def test_env_var_takes_priority(self):
        with mock.patch.dict(os.environ, {"SPECTRA_SOCKET": "/env/path.sock"}):
            assert resolve_socket_path() == "/env/path.sock"

    @mock.patch("spectra._launcher._IS_WINDOWS", True)
    def test_windows_default_uses_temp(self):
        env = {"TEMP": r"C:\Users\test\AppData\Local\Temp"}
        # Remove SPECTRA_SOCKET to avoid interference
        env_clean = {k: v for k, v in os.environ.items()
                     if k not in ("SPECTRA_SOCKET",)}
        env_clean.update(env)
        with mock.patch.dict(os.environ, env_clean, clear=True):
            path = resolve_socket_path()
            assert "spectra" in path
            assert path.startswith(r"C:\Users\test\AppData\Local\Temp")
            assert path.endswith("spectra.sock")

    @mock.patch("spectra._launcher._IS_WINDOWS", True)
    def test_windows_uses_tmp_fallback(self):
        env_clean = {k: v for k, v in os.environ.items()
                     if k not in ("SPECTRA_SOCKET", "TEMP", "TMP")}
        with mock.patch.dict(os.environ, env_clean, clear=True):
            path = resolve_socket_path()
            assert "spectra" in path
            assert path.endswith("spectra.sock")

    @mock.patch("spectra._launcher._IS_WINDOWS", False)
    def test_linux_xdg_runtime_dir(self):
        env = {"XDG_RUNTIME_DIR": "/run/user/1000"}
        env_clean = {k: v for k, v in os.environ.items()
                     if k not in ("SPECTRA_SOCKET",)}
        env_clean.update(env)
        with mock.patch.dict(os.environ, env_clean, clear=True):
            path = resolve_socket_path()
            assert path == "/run/user/1000/spectra/spectra.sock"

    @mock.patch("spectra._launcher._IS_WINDOWS", False)
    def test_linux_tmp_fallback(self):
        env = {"USER": "testuser"}
        env_clean = {k: v for k, v in os.environ.items()
                     if k not in ("SPECTRA_SOCKET", "XDG_RUNTIME_DIR")}
        env_clean.update(env)
        with mock.patch.dict(os.environ, env_clean, clear=True):
            path = resolve_socket_path()
            assert path == "/tmp/spectra-testuser/spectra.sock"


class TestIsNativeBinary:
    """Test native binary detection including PE (Windows) format."""

    def test_elf_binary(self):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(b"\x7fELF" + b"\x00" * 100)
            f.flush()
            assert _is_native_binary(f.name)
        os.unlink(f.name)

    def test_pe_binary(self):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(b"MZ" + b"\x00" * 100)
            f.flush()
            assert _is_native_binary(f.name)
        os.unlink(f.name)

    def test_script_file(self):
        with tempfile.NamedTemporaryFile(delete=False, mode="w") as f:
            f.write("#!/usr/bin/env python3\nprint('hello')\n")
            f.flush()
            assert not _is_native_binary(f.name)
        os.unlink(f.name)


class TestFindBackendBinary:
    """Test binary discovery with platform-specific extensions."""

    @mock.patch("spectra._launcher._IS_WINDOWS", True)
    def test_windows_uses_exe_suffix(self):
        """On Windows, _find_backend_binary should look for .exe files."""
        with tempfile.TemporaryDirectory() as tmpdir:
            pkg_dir = os.path.join(tmpdir, "spectra")
            bin_dir = os.path.join(pkg_dir, "_bin")
            os.makedirs(bin_dir)

            # Create a fake .exe binary
            exe_path = os.path.join(bin_dir, "spectra-backend.exe")
            with open(exe_path, "wb") as f:
                f.write(b"MZ" + b"\x00" * 100)
            os.chmod(exe_path, 0o755)

            with mock.patch.dict(os.environ, {}, clear=False):
                # Remove env var to avoid short-circuit
                os.environ.pop("SPECTRA_BACKEND_PATH", None)
                with mock.patch("spectra._launcher.os.path.dirname",
                                return_value=pkg_dir):
                    result = _find_backend_binary()
                    assert result == exe_path

    @mock.patch("spectra._launcher._IS_WINDOWS", False)
    def test_unix_no_exe_suffix(self):
        """On Unix, _find_backend_binary should look for files without .exe."""
        with tempfile.TemporaryDirectory() as tmpdir:
            pkg_dir = os.path.join(tmpdir, "spectra")
            bin_dir = os.path.join(pkg_dir, "_bin")
            os.makedirs(bin_dir)

            # Create a fake binary (no .exe)
            bin_path = os.path.join(bin_dir, "spectra-backend")
            with open(bin_path, "wb") as f:
                f.write(b"\x7fELF" + b"\x00" * 100)
            os.chmod(bin_path, 0o755)

            with mock.patch.dict(os.environ, {}, clear=False):
                os.environ.pop("SPECTRA_BACKEND_PATH", None)
                with mock.patch("spectra._launcher.os.path.dirname",
                                return_value=pkg_dir):
                    result = _find_backend_binary()
                    assert result == bin_path


class TestCanConnect:
    """Test _can_connect handles missing AF_UNIX gracefully."""

    def test_no_af_unix_returns_false(self):
        """When AF_UNIX is not available, _can_connect should return False."""
        import socket as _socket
        original = getattr(_socket, "AF_UNIX", None)
        try:
            if hasattr(_socket, "AF_UNIX"):
                delattr(_socket, "AF_UNIX")
            assert _can_connect("/nonexistent/path.sock") is False
        finally:
            if original is not None:
                _socket.AF_UNIX = original

    def test_nonexistent_path_returns_false(self):
        """Connecting to a nonexistent path should return False."""
        assert _can_connect("/tmp/spectra-test-nonexistent-12345.sock") is False


class TestTransportConnect:
    """Test Transport.connect handles missing AF_UNIX."""

    def test_no_af_unix_raises_connection_error(self):
        """When AF_UNIX is not available, connect should raise ConnectionError."""
        import socket as _socket
        from spectra._transport import Transport
        from spectra._errors import ConnectionError as SpectraConnectionError

        original = getattr(_socket, "AF_UNIX", None)
        try:
            if hasattr(_socket, "AF_UNIX"):
                delattr(_socket, "AF_UNIX")
            try:
                Transport.connect("/nonexistent/path.sock")
                assert False, "Should have raised ConnectionError"
            except SpectraConnectionError as e:
                assert "AF_UNIX" in str(e)
        finally:
            if original is not None:
                _socket.AF_UNIX = original
