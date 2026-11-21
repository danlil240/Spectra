"""Session must not attach to an in-process app's topic broker (QA 2026-11-21 H1).

An in-process Spectra app serves ``spectra-<pid>.sock`` for topic publishers but
never answers figure requests. A Session that discovers it has to start its own
backend instead of timing out on REQ_CREATE_FIGURE.
"""

import pytest

from spectra import _session
from spectra._errors import ConnectionError as SpectraConnectionError


def _install_fakes(monkeypatch, modes, launched):
    """modes: socket path -> WELCOME mode. launched: list of ensure_backend calls."""

    def fake_ensure_backend(path, timeout=5.0, exclude=None):
        exclude = set(exclude or ())
        launched.append((path, frozenset(exclude)))
        if path and path not in exclude:
            return path
        return "/run/spectra-backend.sock"  # a freshly launched daemon

    def fake_connect(self):
        self._transport = object()
        self._backend_mode = modes[self._socket_path]

    monkeypatch.setattr(_session, "ensure_backend", fake_ensure_backend)
    monkeypatch.setattr(_session, "resolve_socket_path", lambda s=None: s or "/run/app.sock")
    monkeypatch.setattr(_session.Session, "_connect", fake_connect)
    monkeypatch.setattr(_session.Session, "_disconnect_quietly", lambda self: None)
    monkeypatch.delenv("SPECTRA_SOCKET", raising=False)


def test_skips_inproc_broker_and_launches_backend(monkeypatch):
    launched = []
    _install_fakes(
        monkeypatch,
        {"/run/app.sock": "inproc", "/run/spectra-backend.sock": "multiproc"},
        launched,
    )
    s = _session.Session()
    assert s._socket_path == "/run/spectra-backend.sock"
    assert launched[-1][1] == frozenset({"/run/app.sock"})


def test_daemon_is_used_directly(monkeypatch):
    launched = []
    _install_fakes(monkeypatch, {"/run/app.sock": "multiproc"}, launched)
    s = _session.Session()
    assert s._socket_path == "/run/app.sock"
    assert len(launched) == 1


def test_explicit_socket_is_respected_even_if_inproc(monkeypatch):
    launched = []
    _install_fakes(monkeypatch, {"/run/pinned.sock": "inproc"}, launched)
    s = _session.Session(socket="/run/pinned.sock")
    assert s._socket_path == "/run/pinned.sock"


def test_no_auto_launch_reports_inproc_only(monkeypatch):
    launched = []
    _install_fakes(monkeypatch, {"/run/app.sock": "inproc"}, launched)
    with pytest.raises(SpectraConnectionError, match="in-process"):
        _session.Session(auto_launch=False)
