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
from typing import Iterable, List, Optional, Sequence

from . import _codec as codec
from . import _protocol as P
from ._errors import BackendError, ConnectionError, ProtocolError
from ._launcher import ensure_backend, resolve_socket_path
from ._log import log
from ._transport import Transport


KIND_SCALAR_2D = P.TOPIC_KIND_SCALAR_2D
KIND_SCALAR_3D = P.TOPIC_KIND_SCALAR_3D


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
        self._socket_path = resolve_socket_path(socket)
        if auto_launch:
            self._socket_path = ensure_backend(self._socket_path)

        self._lock = threading.Lock()
        self._next_request_id = 0
        self._session_id = 0
        self._closed = False
        self._transport: Optional[Transport] = Transport.connect(self._socket_path)

        # HELLO/WELCOME
        hello = codec.encode_hello(client_type="publisher", build="spectra-python/publisher")
        self._transport.send(msg_type=P.HELLO, payload=hello)
        msg = self._transport.recv()
        if msg is None or msg["header"]["type"] != P.WELCOME:
            self._transport.close()
            self._transport = None
            raise ConnectionError("Did not receive WELCOME from backend")
        welcome = codec.decode_welcome(msg["payload"])
        self._session_id = welcome["session_id"]
        log.debug("publisher connected session=%d", self._session_id)

        # DECLARE
        self._request(
            P.REQ_DECLARE_TOPIC,
            codec.encode_req_declare_topic(name, kind, unit, ring_capacity),
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
        """
        samples = self._flatten(args)
        if not samples:
            return
        if len(samples) % self._stride != 0:
            raise ValueError(
                f"sample count {len(samples)} not a multiple of stride {self._stride}"
            )
        self._request(
            P.REQ_PUBLISH_TOPIC_SAMPLES,
            codec.encode_req_publish_topic_samples(self._name, samples),
        )

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
