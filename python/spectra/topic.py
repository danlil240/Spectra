"""Topic publisher — ROS-style pub/sub interface for spectra-backend.

A Publisher streams named data samples to the Spectra backend whether or not
a UI window is open. When the UI is open (or opens later), the Topics panel
lists active topics and a user can drag one onto an axes to subscribe live.

Usage::

    import spectra
    pub = spectra.Publisher("demo/sine")
    for t in range(1000):
        pub.publish(t * 0.01, math.sin(t * 0.1))
    pub.close()
"""

from __future__ import annotations

import threading
import time as _time
from typing import Iterable, List, Optional, Sequence

from . import _codec as codec
from . import _protocol as P
from ._errors import BackendError, ConnectionError, ProtocolError
from ._launcher import ensure_backend, resolve_socket_path
from ._log import log
from ._transport import Transport


KIND_SCALAR_2D = P.TOPIC_KIND_SCALAR_2D
KIND_SCALAR_3D = P.TOPIC_KIND_SCALAR_3D

# Rate-limit reconnect attempts (matches src/ipc/publisher_client.cpp).
_RECONNECT_BACKOFF_SEC = 0.5


class Publisher:
    """Stream samples to a named topic on the Spectra backend.

    Parameters
    ----------
    name : str
        Topic name (e.g. ``"sensor/imu/accel_x"``). Must be unique while the
        publisher is connected.
    kind : int
        ``Publisher.SCALAR_2D`` (default) for (x, y) pairs, or
        ``Publisher.SCALAR_3D`` for (x, y, z) triples.
    unit : str
        Optional unit string for display (e.g. ``"m/s^2"``).
    ring_capacity : int
        Daemon-side ring buffer capacity in samples. Defaults to 4096.
    socket : str, optional
        Explicit backend socket path. If omitted, uses ``SPECTRA_SOCKET`` or
        the platform default.
    auto_launch : bool
        Launch ``spectra-backend`` if not already running (default True).
    """

    SCALAR_2D = KIND_SCALAR_2D
    SCALAR_3D = KIND_SCALAR_3D

    def __init__(
        self,
        name: str,
        kind: int = KIND_SCALAR_2D,
        unit: str = "",
        ring_capacity: int = 4096,
        socket: Optional[str] = None,
        auto_launch: bool = True,
    ) -> None:
        if not name:
            raise ValueError("Publisher name must be non-empty")
        if kind not in (KIND_SCALAR_2D, KIND_SCALAR_3D):
            raise ValueError(f"unsupported topic kind: {kind}")

        self._name = name
        self._kind = kind
        self._stride = 3 if kind == KIND_SCALAR_3D else 2
        # Reconnect state. When `_explicit_socket` is set the publisher always
        # reconnects to that exact path (matches the C++ Publisher policy).
        # When it's None we re-resolve on every reconnect so we follow broker
        # restarts (new daemon PID, or a UI app coming up on a fresh socket).
        self._explicit_socket = socket
        self._auto_launch = auto_launch
        self._unit = unit
        self._ring_capacity = ring_capacity
        self._socket_path = ""
        self._next_reconnect_at = 0.0

        self._lock = threading.Lock()
        self._next_request_id = 0
        self._session_id = 0
        self._closed = False
        self._transport: Optional[Transport] = None

        # Initial connection must succeed — otherwise the user's program can't
        # tell whether the publisher is usable. Subsequent broker losses are
        # handled silently by `_maybe_reconnect()` so publish loops keep
        # running across UI restarts.
        if not self._connect_and_declare():
            raise ConnectionError(
                f"Publisher failed to connect / declare topic '{name}'"
            )

    # ── Public API ───────────────────────────────────────────────────────

    @property
    def name(self) -> str:
        return self._name

    @property
    def kind(self) -> int:
        return self._kind

    @property
    def is_connected(self) -> bool:
        return self._transport is not None and self._transport.is_open

    def publish(self, *args) -> None:
        """Publish one or more samples.

        For 2D topics, accepts ``(x, y)`` or an iterable of pairs.
        For 3D topics, accepts ``(x, y, z)`` or an iterable of triples.
        Also accepts a pre-interleaved flat sequence of doubles.

        If the broker has gone away (UI closed, daemon exited), the call
        attempts a rate-limited reconnect.  If reconnect doesn't immediately
        succeed the sample is dropped silently and the publish loop keeps
        running — matching the C++ ``spectra::Publisher`` policy.
        """
        samples = self._flatten(args)
        if not samples:
            return
        if len(samples) % self._stride != 0:
            raise ValueError(
                f"sample count {len(samples)} not a multiple of stride {self._stride}"
            )
        payload = codec.encode_req_publish_topic_samples(self._name, samples)

        # First attempt — on the existing connection if any.
        if self._transport is not None and self._transport.is_open:
            try:
                self._request(P.REQ_PUBLISH_TOPIC_SAMPLES, payload)
                return
            except (ConnectionError, BrokenPipeError, OSError) as e:
                log.debug("publish failed, will try reconnect: %s", e)
                self._drop_transport()

        # Try to reconnect (rate-limited). If the broker isn't back yet, drop
        # this sample and report success so the caller's loop keeps running.
        if not self._maybe_reconnect():
            return

        try:
            self._request(P.REQ_PUBLISH_TOPIC_SAMPLES, payload)
        except (ConnectionError, BrokenPipeError, OSError) as e:
            log.debug("publish failed after reconnect, dropping sample: %s", e)
            self._drop_transport()

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._transport is not None:
            try:
                self._transport.close()
            except Exception:
                pass
            self._transport = None

    def __enter__(self) -> "Publisher":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    # ── Internals ────────────────────────────────────────────────────────

    def _drop_transport(self) -> None:
        if self._transport is not None:
            try:
                self._transport.close()
            except Exception:
                pass
            self._transport = None
        self._session_id = 0

    def _connect_and_declare(self) -> bool:
        """Open a fresh socket, do HELLO/WELCOME, then DECLARE the topic.

        Replaces any existing connection. Returns True on success. On failure
        the publisher is left disconnected (transport is None) so the next
        publish() call will trigger another rate-limited reconnect attempt.
        """
        self._drop_transport()

        # Re-resolve every reconnect when the user didn't pin a path, so we
        # follow broker PID changes across UI restarts.
        path = resolve_socket_path(self._explicit_socket)
        if self._auto_launch:
            try:
                path = ensure_backend(path)
            except Exception as e:
                log.debug("ensure_backend failed during reconnect: %s", e)
                return False
        if not path:
            return False

        try:
            transport = Transport.connect(path)
        except ConnectionError as e:
            log.debug("Transport.connect failed during reconnect: %s", e)
            return False

        try:
            hello = codec.encode_hello(
                client_type="publisher", build="spectra-python/publisher"
            )
            transport.send(msg_type=P.HELLO, payload=hello)
            msg = transport.recv()
            if msg is None or msg["header"]["type"] != P.WELCOME:
                transport.close()
                return False
            welcome = codec.decode_welcome(msg["payload"])
        except (ConnectionError, BrokenPipeError, OSError) as e:
            log.debug("HELLO/WELCOME failed during reconnect: %s", e)
            try:
                transport.close()
            except Exception:
                pass
            return False

        # Adopt the new connection before issuing DECLARE so `_request()`
        # uses it.
        self._socket_path = path
        self._transport = transport
        self._session_id = welcome["session_id"]
        self._next_request_id = 0
        log.debug(
            "publisher connected session=%d socket=%s", self._session_id, path
        )

        try:
            self._request(
                P.REQ_DECLARE_TOPIC,
                codec.encode_req_declare_topic(
                    self._name, self._kind, self._unit, self._ring_capacity
                ),
            )
        except (ConnectionError, BrokenPipeError, OSError, BackendError) as e:
            log.debug("DECLARE failed during reconnect: %s", e)
            self._drop_transport()
            return False
        return True

    def _maybe_reconnect(self) -> bool:
        """Rate-limited reconnect attempt. Returns True when connected."""
        if self._closed:
            return False
        if self._transport is not None and self._transport.is_open:
            return True
        now = _time.monotonic()
        if now < self._next_reconnect_at:
            return False
        self._next_reconnect_at = now + _RECONNECT_BACKOFF_SEC
        return self._connect_and_declare()

    def _flatten(self, args: tuple) -> List[float]:
        if len(args) == self._stride and all(isinstance(a, (int, float)) for a in args):
            return [float(a) for a in args]
        if len(args) == 1:
            arg = args[0]
            # Iterable of tuples or flat iterable of floats.
            if hasattr(arg, "__iter__"):
                out: List[float] = []
                for item in arg:
                    if isinstance(item, (int, float)):
                        out.append(float(item))
                    elif hasattr(item, "__iter__"):
                        sub = list(item)
                        if len(sub) != self._stride:
                            raise ValueError(
                                f"each sample must have {self._stride} components"
                            )
                        out.extend(float(v) for v in sub)
                    else:
                        raise TypeError(f"unsupported sample element: {type(item)}")
                return out
        raise TypeError("publish() arguments do not match topic kind")

    def _request(self, msg_type: int, payload: bytes) -> dict:
        if self._transport is None or not self._transport.is_open:
            raise ConnectionError("Publisher not connected")
        with self._lock:
            self._next_request_id += 1
            req_id = self._next_request_id
            self._transport.send(
                msg_type=msg_type,
                payload=payload,
                request_id=req_id,
                session_id=self._session_id,
            )
            while True:
                msg = self._transport.recv()
                if msg is None:
                    raise ConnectionError("Backend closed connection")
                hdr = msg["header"]
                if hdr["type"] == P.RESP_ERR:
                    rid, code, message = codec.decode_resp_err(msg["payload"])
                    if rid == req_id or rid == 0:
                        raise BackendError(code, message)
                if hdr.get("request_id") == req_id:
                    return msg
                if hdr["type"] == P.RESP_OK:
                    rid = codec.decode_resp_ok(msg["payload"])
                    if rid == req_id:
                        return msg
                # otherwise: stray event, ignore for publisher
