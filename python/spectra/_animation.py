"""Animation utilities for the Spectra Python client.

Provides IPC-aware sleep and frame pacing helpers that drain events
while waiting, preventing the backend from stalling on a blocked client.

Two animation modes:
  1. Python-driven: Python controls the loop with FramePacer
  2. Backend-driven: Backend sends ANIM_TICK at fixed rate, Python responds
"""

import time
from typing import TYPE_CHECKING, Callable, Optional

if TYPE_CHECKING:
    from ._session import Session


def ipc_sleep(session: "Session", duration: float) -> None:
    """Sleep for `duration` seconds while draining IPC events.

    Unlike time.sleep(), this keeps the IPC connection alive by
    processing incoming events (e.g. EVT_WINDOW_CLOSED) during the wait.

    Thread-safe: acquires the session lock before reading from the socket.
    Falls back to plain sleep if the lock is held by another thread
    (e.g. the main thread doing a _request() or show() recv).
    """
    import select

    if session._transport is None or not session._transport.is_open:
        time.sleep(duration)
        return

    deadline = time.monotonic() + duration
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break

        timeout = min(remaining, 0.05)  # poll at 20 Hz
        try:
            ready, _, _ = select.select([session._transport.fileno()], [], [], timeout)
        except (ValueError, OSError):
            break

        if ready:
            # Try to acquire the lock without blocking — if another thread
            # is doing a _request() send+recv, just skip this read.
            acquired = session._lock.acquire(blocking=False)
            if not acquired:
                time.sleep(min(remaining, 0.01))
                continue
            try:
                msg = session._transport.recv()
            finally:
                session._lock.release()
            if msg is None:
                break
            session._handle_event(msg)


class FramePacer:
    """Simple frame pacer for Python-driven animation loops.

    Usage::

        pacer = FramePacer(fps=30)
        while running:
            update_data()
            series.set_data(x, y)
            pacer.pace(session)
    """

    __slots__ = ("_interval", "_last_time")

    def __init__(self, fps: float = 30.0) -> None:
        self._interval = 1.0 / max(fps, 1.0)
        self._last_time = time.monotonic()

    @property
    def fps(self) -> float:
        return 1.0 / self._interval

    @fps.setter
    def fps(self, value: float) -> None:
        self._interval = 1.0 / max(value, 1.0)

    def pace(self, session: "Session") -> float:
        """Wait until the next frame time, draining events. Returns dt since last call."""
        now = time.monotonic()
        elapsed = now - self._last_time
        remaining = self._interval - elapsed
        if remaining > 0:
            ipc_sleep(session, remaining)
        now = time.monotonic()
        dt = now - self._last_time
        self._last_time = now
        return dt


class BackendAnimator:
    """Backend-driven animation: the backend controls the clock and sends
    ANIM_TICK events at a fixed rate. Python registers a callback that
    receives (t, dt, frame_num) and updates data accordingly.

    Usage::

        def on_tick(t, dt, frame_num):
            y = [math.sin(x_i + t) for x_i in x]
            series.set_data(x, y)

        animator = BackendAnimator(session, figure, fps=60)
        animator.on_tick = on_tick
        animator.start()
        session.show()  # blocks until windows closed
        animator.stop()
    """

    __slots__ = (
        "_session", "_figure_id", "_fps", "_duration",
        "_running", "_on_tick",
    )

    def __init__(
        self,
        session: "Session",
        figure_id: int,
        fps: float = 60.0,
        duration: float = 0.0,
    ) -> None:
        self._session = session
        self._figure_id = figure_id
        self._fps = fps
        self._duration = duration
        self._running = False
        self._on_tick: Optional[Callable[[float, float, int], None]] = None

    @property
    def on_tick(self) -> Optional[Callable[[float, float, int], None]]:
        return self._on_tick

    @on_tick.setter
    def on_tick(self, callback: Optional[Callable[[float, float, int], None]]) -> None:
        self._on_tick = callback

    @property
    def is_running(self) -> bool:
        return self._running

    def start(self) -> None:
        """Request the backend to start sending ANIM_TICK events."""
        from . import _protocol as P
        from . import _codec as codec

        if self._running:
            return
        payload = codec.encode_req_anim_start(
            self._figure_id, self._fps, self._duration
        )
        self._session._request(P.REQ_ANIM_START, payload)
        self._running = True
        # Register this animator with the session for tick dispatch
        self._session._register_animator(self)

    def stop(self) -> None:
        """Request the backend to stop sending ANIM_TICK events."""
        from . import _protocol as P
        from . import _codec as codec

        if not self._running:
            return
        payload = codec.encode_req_anim_stop(self._figure_id)
        self._session._request(P.REQ_ANIM_STOP, payload)
        self._running = False
        self._session._unregister_animator(self)

    def handle_tick(self, t: float, dt: float, frame_num: int) -> None:
        """Called by Session when an ANIM_TICK is received for this figure."""
        if self._on_tick is not None:
            self._on_tick(t, dt, frame_num)
