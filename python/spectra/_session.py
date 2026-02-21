"""Session class — manages a single connection to the spectra-backend."""

import threading
from typing import Optional, List

from . import _protocol as P
from . import _codec as codec
from ._transport import Transport
from ._launcher import resolve_socket_path, ensure_backend
from ._errors import (
    ConnectionError,
    ProtocolError,
    BackendError,
)
from ._blob import BlobStore


class Session:
    """A connection to the spectra-backend daemon.

    Usage::

        s = Session()                        # auto-launch backend
        s = Session(socket="/path/to.sock")  # explicit socket
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        line = ax.line(x, y)
        fig.show()
        s.show()                             # block until all windows closed
        s.close()
    """

    def __init__(
        self,
        socket: Optional[str] = None,
        auto_launch: bool = True,
    ) -> None:
        self._socket_path = resolve_socket_path(socket)
        self._transport: Optional[Transport] = None
        self._session_id: int = 0
        self._next_request_id: int = 0
        self._lock = threading.Lock()
        self._figures: List["Figure"] = []
        self._animators: List = []  # BackendAnimator instances
        self._blob_store = BlobStore()
        self._closed = False

        if auto_launch:
            self._socket_path = ensure_backend(self._socket_path)

        self._connect()

    def _connect(self) -> None:
        """Connect to the backend and perform HELLO/WELCOME handshake."""
        self._transport = Transport.connect(self._socket_path)

        # Send HELLO
        hello_payload = codec.encode_hello(client_type="python")
        self._transport.send(
            msg_type=P.HELLO,
            payload=hello_payload,
        )

        # Receive WELCOME
        msg = self._transport.recv()
        if msg is None:
            raise ConnectionError("Backend closed connection during handshake")

        if msg["header"]["type"] != P.WELCOME:
            raise ProtocolError(
                f"Expected WELCOME (0x{P.WELCOME:04X}), "
                f"got 0x{msg['header']['type']:04X}"
            )

        welcome = codec.decode_welcome(msg["payload"])
        self._session_id = welcome["session_id"]

    def _next_req_id(self) -> int:
        with self._lock:
            self._next_request_id += 1
            return self._next_request_id

    def _request(self, msg_type: int, payload: bytes = b"") -> dict:
        """Send a request and wait for the matching response.

        Returns the response message dict.
        Raises BackendError if RESP_ERR is received.
        """
        if self._transport is None or not self._transport.is_open:
            raise ConnectionError("Not connected to backend")

        req_id = self._next_req_id()

        with self._lock:
            self._transport.send(
                msg_type=msg_type,
                payload=payload,
                request_id=req_id,
                session_id=self._session_id,
            )

        # Read responses until we get one matching our request_id
        while True:
            msg = self._transport.recv()
            if msg is None:
                raise ConnectionError("Backend closed connection")

            hdr = msg["header"]

            # Check for error response
            if hdr["type"] == P.RESP_ERR:
                rid, code, message = codec.decode_resp_err(msg["payload"])
                if rid == req_id or rid == 0:
                    raise BackendError(code, message)

            # Check for matching request_id
            if hdr["request_id"] == req_id:
                return msg

            # Check for RESP_OK matching our request
            if hdr["type"] == P.RESP_OK:
                rid = codec.decode_resp_ok(msg["payload"])
                if rid == req_id:
                    return msg

            # Otherwise it's an event or unrelated message — store for later
            self._handle_event(msg)

    def _register_animator(self, animator) -> None:
        """Register a BackendAnimator for ANIM_TICK dispatch."""
        if animator not in self._animators:
            self._animators.append(animator)

    def _unregister_animator(self, animator) -> None:
        """Unregister a BackendAnimator."""
        try:
            self._animators.remove(animator)
        except ValueError:
            pass

    def _handle_event(self, msg: dict) -> None:
        """Handle asynchronous events from the backend."""
        hdr = msg["header"]
        if hdr["type"] == P.EVT_WINDOW_CLOSED:
            figure_id, window_id, reason = codec.decode_evt_window_closed(msg["payload"])
            # Mark figure as not visible
            for fig in self._figures:
                if fig._id == figure_id:
                    fig._visible = False
                    break
        elif hdr["type"] == P.ANIM_TICK:
            tick = codec.decode_anim_tick(msg["payload"])
            for anim in self._animators:
                if anim._figure_id == tick["figure_id"]:
                    anim.handle_tick(tick["t"], tick["dt"], tick["frame_num"])
                    break
        elif hdr["type"] == P.BLOB_RELEASE:
            blob_name = codec.decode_blob_release(msg["payload"])
            if blob_name:
                self._blob_store.release_blob(blob_name)

    def figure(self, title: str = "", width: int = 1280, height: int = 720) -> "Figure":
        """Create a new figure. Returns a Figure proxy."""
        from ._figure import Figure

        payload = codec.encode_req_create_figure(title=title, width=width, height=height)
        resp = self._request(P.REQ_CREATE_FIGURE, payload)

        _, figure_id = codec.decode_resp_figure_created(resp["payload"])
        fig = Figure(self, figure_id, title)
        self._figures.append(fig)
        return fig

    def reconnect(self, session_id: int = 0, session_token: str = "") -> dict:
        """Reconnect to an existing backend session and restore state.

        If session_id is 0, accepts any active session.
        Returns a dict with figure_ids from the restored snapshot.
        Raises BackendError on session mismatch.
        """
        if self._transport is None or not self._transport.is_open:
            # Need to re-establish transport first
            self._transport = Transport.connect(self._socket_path)
            hello_payload = codec.encode_hello(client_type="python")
            self._transport.send(msg_type=P.HELLO, payload=hello_payload)
            msg = self._transport.recv()
            if msg is None:
                raise ConnectionError("Backend closed connection during reconnect handshake")
            if msg["header"]["type"] != P.WELCOME:
                raise ProtocolError(
                    f"Expected WELCOME, got 0x{msg['header']['type']:04X}"
                )
            welcome = codec.decode_welcome(msg["payload"])
            self._session_id = welcome["session_id"]

        sid = session_id if session_id != 0 else self._session_id
        payload = codec.encode_req_reconnect(session_id=sid, session_token=session_token)
        resp = self._request(P.REQ_RECONNECT, payload)

        # Response is RESP_SNAPSHOT — extract figure IDs
        figure_ids = []
        if resp["header"]["type"] == P.RESP_SNAPSHOT:
            # Parse the snapshot to extract figure IDs
            from ._codec import PayloadDecoder
            dec = PayloadDecoder(resp["payload"])
            while dec.next():
                if dec.tag == 0x50:  # TAG_REVISION
                    pass
                elif dec.tag == 0x53:  # TAG_FIGURE_BLOB
                    # Each figure blob is nested TLV; extract figure_id from it
                    blob = dec.as_blob()
                    inner = PayloadDecoder(blob)
                    while inner.next():
                        if inner.tag == P.TAG_FIGURE_ID:
                            figure_ids.append(inner.as_u64())
                            break

        self._closed = False
        return {"session_id": self._session_id, "figure_ids": figure_ids}

    def batch_update(self, updates: list) -> None:
        """Send multiple property updates in a single IPC message.

        Each item in `updates` is a dict with keys:
            figure_id, axes_index, series_index, prop, f1..f4, bool_val, str_val
        """
        if not updates:
            return
        payload = codec.encode_req_update_batch(updates)
        self._request(P.REQ_UPDATE_BATCH, payload)

    def list_figures(self) -> List[int]:
        """Query the backend for all figure IDs in this session."""
        payload = codec.encode_req_list_figures()
        resp = self._request(P.REQ_LIST_FIGURES, payload)
        _, figure_ids = codec.decode_resp_figure_list(resp["payload"])
        return figure_ids

    @property
    def figures(self) -> List["Figure"]:
        """List of all figures created in this session."""
        return list(self._figures)

    @property
    def session_id(self) -> int:
        return self._session_id

    def show(self) -> None:
        """Block until all visible figure windows are closed.

        Drains events while waiting.
        """
        if self._transport is None or not self._transport.is_open:
            return

        # Show all figures that haven't been shown yet
        for fig in self._figures:
            if not fig._visible and not fig._shown_once:
                fig.show()

        # Poll for events until all figures are not visible
        import select

        while any(f._visible for f in self._figures):
            if self._transport is None or not self._transport.is_open:
                break

            # Use select with timeout to avoid busy-waiting
            try:
                ready, _, _ = select.select([self._transport.fileno()], [], [], 0.1)
            except (ValueError, OSError):
                break

            if ready:
                msg = self._transport.recv()
                if msg is None:
                    break
                self._handle_event(msg)

    @property
    def blob_store(self) -> BlobStore:
        """Access the blob store for shared memory management."""
        return self._blob_store

    def close(self) -> None:
        """Gracefully disconnect from the backend."""
        if self._closed:
            return
        self._closed = True

        # Stop all animators
        for anim in list(self._animators):
            anim._running = False
        self._animators.clear()

        # Cleanup blob store
        self._blob_store.cleanup_all()

        if self._transport is not None and self._transport.is_open:
            try:
                self._transport.send(
                    msg_type=P.REQ_DISCONNECT,
                    session_id=self._session_id,
                )
            except Exception:
                pass
            self._transport.close()
        self._transport = None

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, *args) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass
