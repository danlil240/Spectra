"""Tests for the Python topic publisher reconnect path."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra import _protocol as P
from spectra._codec import PayloadEncoder
from spectra._errors import ConnectionError as SpectraConnectionError
import spectra.topic as topic_mod


def _welcome(session_id=123):
    enc = PayloadEncoder()
    enc.put_u64(P.TAG_SESSION_ID, session_id)
    enc.put_u64(P.TAG_WINDOW_ID, 0)
    return {
        "header": {"type": P.WELCOME, "request_id": 0},
        "payload": enc.take(),
    }


def _ok(request_id):
    return {
        "header": {"type": P.RESP_OK, "request_id": request_id},
        "payload": b"",
    }


class FakeTransport:
    def __init__(self, path, *, alive=True, welcome=True):
        self.path = path
        self.is_open = True
        self.alive = alive
        self.sent = []
        self.closed = False
        self._responses = []
        if welcome:
            self._responses.append(_welcome())

    def is_alive(self):
        return self.alive and self.is_open

    def send(self, msg_type, payload=b"", request_id=0, session_id=0, window_id=0):
        self.sent.append(msg_type)
        if not self.alive:
            raise SpectraConnectionError("stale transport")
        if msg_type in (P.REQ_DECLARE_TOPIC, P.REQ_PUBLISH_TOPIC_SAMPLES):
            self._responses.append(_ok(request_id))

    def recv(self):
        if not self.alive:
            raise SpectraConnectionError("stale transport")
        if self._responses:
            return self._responses.pop(0)
        return None

    def close(self):
        self.closed = True
        self.is_open = False


def test_publisher_retries_next_candidate_when_newest_socket_fails(monkeypatch):
    transports = {}

    def fake_connect(path):
        transport = FakeTransport(path, welcome=(path == "good.sock"))
        transports[path] = transport
        return transport

    monkeypatch.setattr(
        topic_mod,
        "resolve_socket_candidates",
        lambda explicit=None: ["bad.sock", "good.sock"],
        raising=False,
    )
    monkeypatch.setattr(topic_mod, "resolve_socket_path", lambda explicit=None: "bad.sock")
    monkeypatch.setattr(topic_mod, "ensure_backend", lambda path: path)
    monkeypatch.setattr(topic_mod.Transport, "connect", staticmethod(fake_connect))

    pub = topic_mod.Publisher("demo/retry", auto_launch=False)

    assert pub.is_connected
    assert pub._socket_path == "good.sock"
    assert transports["bad.sock"].closed
    pub.close()


def test_publisher_checks_cached_transport_liveness_before_publish(monkeypatch):
    transports = {}
    connect_order = []

    def fake_connect(path):
        connect_order.append(path)
        transport = FakeTransport(path)
        transports[path] = transport
        return transport

    monkeypatch.setattr(
        topic_mod,
        "resolve_socket_candidates",
        lambda explicit=None: ["old.sock"],
        raising=False,
    )
    monkeypatch.setattr(topic_mod, "resolve_socket_path", lambda explicit=None: "old.sock")
    monkeypatch.setattr(topic_mod, "ensure_backend", lambda path: path)
    monkeypatch.setattr(topic_mod.Transport, "connect", staticmethod(fake_connect))

    pub = topic_mod.Publisher("demo/liveness", auto_launch=False)
    old = transports["old.sock"]
    old.alive = False

    monkeypatch.setattr(
        topic_mod,
        "resolve_socket_candidates",
        lambda explicit=None: ["new.sock"],
        raising=False,
    )
    monkeypatch.setattr(topic_mod, "resolve_socket_path", lambda explicit=None: "new.sock")

    pub.publish(1.0, 2.0)

    assert connect_order == ["old.sock", "new.sock"]
    assert old.sent == [P.HELLO, P.REQ_DECLARE_TOPIC]
    assert pub._socket_path == "new.sock"
    pub.close()
