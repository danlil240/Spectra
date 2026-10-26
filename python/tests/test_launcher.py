"""Tests for spectra._launcher: socket-dir fallback and startup error reporting."""
import os
import stat
import sys

import pytest

from spectra import _launcher

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="UNIX sockets / shebang scripts")


def _fake_backend(tmp_path, body):
    script = tmp_path / "fake-backend"
    script.write_text(f"#!{sys.executable}\nimport sys, time\n{body}\n")
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    return str(script)


@pytest.fixture
def isolated_runtime(tmp_path, monkeypatch):
    run = tmp_path / "run"
    run.mkdir()
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(run))
    monkeypatch.delenv("SPECTRA_SOCKET", raising=False)
    monkeypatch.delenv("SPECTRA_DEBUG_LOG", raising=False)
    monkeypatch.setenv("SPECTRA_NO_DOWNLOAD", "1")
    return run


def test_runtime_dir_uses_short_xdg(monkeypatch):
    monkeypatch.setenv("XDG_RUNTIME_DIR", "/run/user/1000")
    assert _launcher._runtime_socket_dir() == "/run/user/1000"


def test_runtime_dir_falls_back_when_xdg_too_long(monkeypatch):
    long_dir = "/tmp/" + "x" * 120
    monkeypatch.setenv("XDG_RUNTIME_DIR", long_dir)
    assert _launcher._runtime_socket_dir() == "/tmp"


def test_socket_path_fits_limit():
    limit = _launcher._sun_path_max()
    assert _launcher._socket_path_fits("/" + "a" * (limit - 2))
    assert not _launcher._socket_path_fits("/" + "a" * (limit - 1))


def test_startup_failure_surfaces_backend_stderr(tmp_path, isolated_runtime, monkeypatch):
    backend = _fake_backend(
        tmp_path,
        'sys.stderr.write("[spectra-backend] Failed to listen on /some/socket\\n"); sys.exit(3)',
    )
    monkeypatch.setenv("SPECTRA_BACKEND_PATH", backend)
    with pytest.raises(RuntimeError) as info:
        _launcher.ensure_backend("", timeout=5.0)
    msg = str(info.value)
    assert "exited during startup with code 3" in msg
    assert "Failed to listen on /some/socket" in msg
    assert "did not start within" not in msg


def test_startup_timeout_surfaces_backend_stderr(tmp_path, isolated_runtime, monkeypatch):
    backend = _fake_backend(
        tmp_path, 'sys.stderr.write("still booting\\n"); sys.stderr.flush(); time.sleep(30)'
    )
    monkeypatch.setenv("SPECTRA_BACKEND_PATH", backend)
    with pytest.raises(RuntimeError) as info:
        _launcher.ensure_backend("", timeout=1.0)
    msg = str(info.value)
    assert "did not start within 1.0s" in msg
    assert "still booting" in msg


def test_long_xdg_pins_short_socket(tmp_path, monkeypatch):
    long_dir = tmp_path / ("d" * 100)
    long_dir.mkdir()
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(long_dir))
    monkeypatch.delenv("SPECTRA_SOCKET", raising=False)
    monkeypatch.delenv("SPECTRA_DEBUG_LOG", raising=False)
    argv_file = tmp_path / "argv.txt"
    backend = _fake_backend(
        tmp_path,
        f'open({str(argv_file)!r}, "w").write("\\n".join(sys.argv[1:])); sys.exit(1)',
    )
    monkeypatch.setenv("SPECTRA_BACKEND_PATH", backend)
    with pytest.raises(RuntimeError):
        _launcher.ensure_backend("", timeout=5.0)
    args = argv_file.read_text().splitlines()
    assert args[0] == "--socket"
    assert os.path.dirname(args[1]) == "/tmp"
    assert _launcher._socket_path_fits(args[1])


def test_explicit_socket_too_long_is_rejected(tmp_path, isolated_runtime, monkeypatch):
    backend = _fake_backend(tmp_path, "sys.exit(0)")
    monkeypatch.setenv("SPECTRA_BACKEND_PATH", backend)
    with pytest.raises(RuntimeError, match="too long"):
        _launcher.ensure_backend("/tmp/" + "s" * 200 + ".sock", timeout=1.0)


def test_missing_binary_suggests_verified_download(isolated_runtime, monkeypatch):
    monkeypatch.delenv("SPECTRA_BACKEND_PATH", raising=False)
    monkeypatch.delenv("SPECTRA_NO_DOWNLOAD", raising=False)
    monkeypatch.delenv("SPECTRA_AUTO_DOWNLOAD", raising=False)
    monkeypatch.setattr(_launcher, "_find_backend_binary", lambda: None)

    import spectra._download as dl

    def _no_network(*_a, **_k):
        raise AssertionError("must not download without explicit opt-in")

    monkeypatch.setattr(dl, "download_backend", _no_network)
    with pytest.raises(RuntimeError, match="spectra-backend --download"):
        _launcher.ensure_backend("", timeout=1.0)
