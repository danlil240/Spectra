"""Interactive Jupyter canvas for in-process :class:`~spectra._embed.EmbedSurface` plots.

Gives notebooks a live, pannable, zoomable view instead of a static PNG::

    import spectra.notebook as snb
    from spectra._embed import EmbedSurface

    surface = EmbedSurface(800, 450)
    ax = surface.figure().subplot(1, 1, 1)
    ax.line(x, y, label="signal")
    snb.show(surface)          # or just leave ``surface`` as the cell's last line

The browser side is a small `anywidget <https://anywidget.dev>`_ module, so the
same widget works in JupyterLab, Notebook 7, VS Code and Colab. Install the
optional dependencies with ``pip install "spectra-plot[notebook]"``.

How it works: the browser sends pointer / wheel / key / resize events to the
kernel. :class:`FrameController` forwards them to the surface, renders only
when something changed, caps the frame rate (30 fps by default) and drops
intermediate frames so a slow kernel never builds up a backlog. Frames are JPEG
while the user interacts and a lossless PNG once input stops. The controller
does not depend on anywidget, so it can be driven (and tested) without a
browser.
"""

from __future__ import annotations

import base64
import io
import time
import zlib
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Optional, Tuple

from ._embed import ACTION_PRESS, ACTION_RELEASE, EmbedFigure, EmbedSurface

try:  # optional: the widget itself needs anywidget (and traitlets, its dependency)
    import anywidget as _anywidget
    import traitlets as _traitlets

    _HAS_ANYWIDGET = True
except ImportError:  # pragma: no cover - exercised only without the extra
    _anywidget = None
    _traitlets = None
    _HAS_ANYWIDGET = False

__all__ = ["Canvas", "FrameController", "show", "available"]

_INSTALL_HINT = (
    'spectra.notebook needs anywidget and Pillow: pip install "spectra-plot[notebook]"'
)

# Browser MouseEvent.button -> GLFW button (0 left, 1 right, 2 middle).
_BROWSER_TO_GLFW_BUTTON = {0: 0, 1: 2, 2: 1}

# WheelEvent.deltaMode -> browser units per GLFW wheel notch.
_WHEEL_UNITS_PER_NOTCH = {0: 100.0, 1: 3.0, 2: 1.0}

_MIN_SURFACE_PX = 16
_MAX_SURFACE_PX = 8192


def available() -> bool:
    """True when the interactive widget can be used (anywidget + Pillow importable)."""
    if not _HAS_ANYWIDGET:
        return False
    try:
        import PIL  # noqa: F401
    except ImportError:
        return False
    return True


class _Timer:
    """Cancellable handle returned by a scheduler."""

    def __init__(self, cancel: Callable[[], None]) -> None:
        self._cancel = cancel

    def cancel(self) -> None:
        self._cancel()


def _asyncio_scheduler(delay: float, fn: Callable[[], None]) -> Optional[_Timer]:
    """Run ``fn`` after ``delay`` seconds on the running asyncio loop (the kernel's).

    Returns None when no loop is running (plain Python, no kernel); the
    controller then renders synchronously instead of deferring.
    """
    import asyncio

    try:
        loop = asyncio.get_running_loop()
    except RuntimeError:
        return None
    handle = loop.call_later(max(0.0, delay), fn)
    return _Timer(handle.cancel)


def _encode(rgba: bytes, width: int, height: int, mime: str, jpeg_quality: int) -> bytes:
    from PIL import Image

    img = Image.frombytes("RGBA", (width, height), rgba)
    out = io.BytesIO()
    if mime == "image/jpeg":
        img.convert("RGB").save(out, format="JPEG", quality=jpeg_quality)
    else:
        # compress_level 1: notebooks favour latency over a slightly smaller file.
        img.save(out, format="PNG", compress_level=1)
    return out.getvalue()


class FrameController:
    """Event forwarding, change detection and frame throttling for one surface.

    Transport-agnostic: ``send_frame(data, mime, width, height)`` receives each
    encoded frame, ``schedule(delay, fn)`` defers work (returns a handle with
    ``cancel()`` or None when deferring is impossible) and ``clock()`` returns
    seconds. The widget wires these to the anywidget model and the kernel's
    event loop; tests pass fakes.

    Events are dicts with a ``"t"`` key, coordinates in CSS pixels:

    * ``{"t": "move", "x", "y"}``
    * ``{"t": "down" | "up", "b": browser_button, "x", "y", "m": glfw_mods}``
    * ``{"t": "wheel", "dx", "dy", "mode": deltaMode, "x", "y"}`` (browser deltas)
    * ``{"t": "key", "k": glfw_key, "a": glfw_action, "m": glfw_mods}``
    * ``{"t": "resize", "w": css_width, "h": css_height, "dpr": devicePixelRatio}``
    """

    def __init__(
        self,
        surface: EmbedSurface,
        send_frame: Callable[[bytes, str, int, int], None],
        *,
        schedule: Callable[[float, Callable[[], None]], Optional[_Timer]] = _asyncio_scheduler,
        clock: Callable[[], float] = time.monotonic,
        max_fps: float = 30.0,
        idle_delay: float = 0.2,
        settle_time: float = 1.0,
        jpeg_quality: int = 80,
        hidpi: bool = False,
    ) -> None:
        if max_fps <= 0:
            raise ValueError("max_fps must be > 0")
        self.surface = surface
        self._send_frame = send_frame
        self._schedule = schedule
        self._clock = clock
        self.frame_interval = 1.0 / max_fps
        self.idle_delay = idle_delay
        self.settle_time = settle_time
        self.jpeg_quality = jpeg_quality
        # Render at devicePixelRatio x CSS size. Off by default: the embed
        # renderer does not scale plot text, ticks and margins with
        # dpi_scale yet, so a 2x surface would show half-size labels.
        self.hidpi = hidpi

        self._base_dpi = surface.dpi_scale or 1.0
        self._dpr = 1.0
        self._css_w = surface.width / self._base_dpi
        self._css_h = surface.height / self._base_dpi

        self._flush_timer: Optional[_Timer] = None
        self._idle_timer: Optional[_Timer] = None
        self._last_render_t = -1e9
        self._last_update_t: Optional[float] = None
        self._settle_start: Optional[float] = None
        self._last_render_crc: Optional[int] = None
        self._last_sent: Optional[Tuple[int, str]] = None  # (crc, mime)

        # Counters (read by tests and handy when tuning).
        self.events_handled = 0
        self.frames_rendered = 0
        self.frames_sent = 0
        self.frames_dropped = 0

    # ── Input ────────────────────────────────────────────────────────────

    @property
    def css_size(self) -> Tuple[float, float]:
        return self._css_w, self._css_h

    def _to_px(self, x: float, y: float) -> Tuple[float, float]:
        sx = self.surface.width / self._css_w if self._css_w > 0 else 1.0
        sy = self.surface.height / self._css_h if self._css_h > 0 else 1.0
        return float(x) * sx, float(y) * sy

    def handle_events(self, events: Iterable[Dict[str, Any]]) -> None:
        """Forward a batch of browser events to the surface, then request one frame."""
        changed = False
        for ev in events:
            try:
                applied = isinstance(ev, dict) and self._apply(ev)
            except (KeyError, TypeError, ValueError):
                applied = False  # malformed event from the front end: skip it
            if applied:
                changed = True
                self.events_handled += 1
        if changed:
            self.request_frame()

    def _apply(self, ev: Dict[str, Any]) -> bool:
        kind = ev.get("t")
        s = self.surface
        if kind == "move":
            s.mouse_move(*self._to_px(ev["x"], ev["y"]))
        elif kind in ("down", "up"):
            button = _BROWSER_TO_GLFW_BUTTON.get(int(ev.get("b", 0)))
            if button is None:
                return False
            action = ACTION_PRESS if kind == "down" else ACTION_RELEASE
            x, y = self._to_px(ev["x"], ev["y"])
            s.mouse_move(x, y)
            s.mouse_button(button, action, int(ev.get("m", 0)), x, y)
        elif kind == "wheel":
            units = _WHEEL_UNITS_PER_NOTCH.get(int(ev.get("mode", 0)), 100.0)
            # Browser deltaY > 0 scrolls down; GLFW yoffset > 0 scrolls up (zoom in).
            dy = max(-10.0, min(10.0, -float(ev.get("dy", 0.0)) / units))
            dx = max(-10.0, min(10.0, -float(ev.get("dx", 0.0)) / units))
            if dx == 0.0 and dy == 0.0:
                return False
            x, y = self._to_px(ev["x"], ev["y"])
            s.mouse_move(x, y)
            s.scroll(dx, dy, x, y)
        elif kind == "key":
            s.key(int(ev["k"]), int(ev.get("a", ACTION_PRESS)), int(ev.get("m", 0)))
        elif kind == "resize":
            return self.resize(ev["w"], ev["h"], ev.get("dpr", 1.0))
        else:
            return False
        return True

    def resize(self, css_width: float, css_height: float, dpr: float = 1.0) -> bool:
        """Match the surface to the output's CSS size (times devicePixelRatio if ``hidpi``)."""
        css_width = float(css_width)
        css_height = float(css_height)
        dpr = float(dpr) if self.hidpi and dpr and dpr > 0 else 1.0
        if css_width <= 0 or css_height <= 0:
            return False

        def px(v: float) -> int:
            return int(max(_MIN_SURFACE_PX, min(_MAX_SURFACE_PX, round(v * dpr))))

        w, h = px(css_width), px(css_height)
        same = (w, h) == (self.surface.width, self.surface.height) and dpr == self._dpr
        self._css_w, self._css_h = css_width, css_height
        if same:
            return False
        if (w, h) != (self.surface.width, self.surface.height):
            if not self.surface.resize(w, h):
                return False
        if dpr != self._dpr:
            self.surface.set_dpi_scale(self._base_dpi * dpr)
            self._dpr = dpr
        return True

    # ── Frames ───────────────────────────────────────────────────────────

    def request_frame(self) -> None:
        """Render soon: now if the frame budget allows, else once, at the next slot."""
        self._settle_start = None
        if self._flush_timer is not None:
            self.frames_dropped += 1  # coalesced into the frame already pending
            return
        wait = self.frame_interval - (self._clock() - self._last_render_t)
        if wait > 0:
            timer = self._schedule(wait, self._on_flush)
            if timer is not None:
                self._flush_timer = timer
                return
        self._render_and_send(final=False)

    def _on_flush(self) -> None:
        self._flush_timer = None
        self._render_and_send(final=False)

    def _on_idle(self) -> None:
        self._idle_timer = None
        now = self._clock()
        if self._settle_start is None:
            self._settle_start = now
        before = self._last_render_crc
        crc = self._render_and_send(final=False, reschedule_idle=False, send=False)
        animating = crc != before and now - self._settle_start < self.settle_time
        if animating:
            # Something still moves on its own (inertial pan, animated reset):
            # keep streaming JPEG frames until it settles.
            self._send_current("image/jpeg")
            self._idle_timer = self._schedule(self.frame_interval, self._on_idle)
            if self._idle_timer is None:
                self._send_current("image/png")
            return
        self._send_current("image/png")

    def render_now(self, final: bool = True) -> None:
        """Render and send one frame immediately (PNG when ``final``)."""
        self._render_and_send(final=final)

    def _render_and_send(
        self, final: bool, reschedule_idle: bool = True, send: bool = True
    ) -> Optional[int]:
        now = self._clock()
        dt = 0.0 if self._last_update_t is None else now - self._last_update_t
        self._last_update_t = now
        self.surface.update(max(0.0, min(dt, 0.1)))
        self._rgba = self.surface.render()
        self._rgba_size = (self.surface.width, self.surface.height)
        self._last_render_t = now
        self.frames_rendered += 1
        self._last_render_crc = zlib.crc32(self._rgba)
        if send:
            self._send_current("image/png" if final else "image/jpeg")
        if reschedule_idle and not final:
            self._arm_idle()
        return self._last_render_crc

    def _arm_idle(self) -> None:
        if self._idle_timer is not None:
            self._idle_timer.cancel()
        self._idle_timer = self._schedule(self.idle_delay, self._on_idle)
        if self._idle_timer is None:
            # Nothing can call us back later: upgrade to the final PNG now.
            self._send_current("image/png")

    def _send_current(self, mime: str) -> None:
        crc = self._last_render_crc
        if self._last_sent is not None and self._last_sent[0] == crc:
            # Same pixels as the last frame sent: nothing to do, unless this
            # upgrades a lossy JPEG to the final PNG.
            if self._last_sent[1] == "image/png" or mime == "image/jpeg":
                return
        w, h = self._rgba_size
        data = _encode(self._rgba, w, h, mime, self.jpeg_quality)
        self._last_sent = (crc, mime)
        self.frames_sent += 1
        self._send_frame(data, mime, w, h)

    def close(self) -> None:
        for timer in (self._flush_timer, self._idle_timer):
            if timer is not None:
                timer.cancel()
        self._flush_timer = self._idle_timer = None


# ─── anywidget front end ─────────────────────────────────────────────────────

if _HAS_ANYWIDGET:

    class Canvas(_anywidget.AnyWidget):
        """Live, pannable, zoomable notebook view of an :class:`EmbedSurface`.

        Scroll to zoom, drag to pan, right-drag to zoom an axis, double-click or
        press ``R`` to reset the view. The canvas follows the output width; set
        :attr:`height` (CSS pixels) to change its height.

        ``hidpi=True`` renders at the browser's devicePixelRatio (sharper on
        Retina screens, but plot text does not scale with it yet).
        """

        _esm = Path(__file__).with_name("_notebook_canvas.js")
        _css = Path(__file__).with_name("_notebook_canvas.css")

        frame = _traitlets.Bytes(b"").tag(sync=True)
        frame_mime = _traitlets.Unicode("image/png").tag(sync=True)
        frame_width = _traitlets.Int(0).tag(sync=True)
        frame_height = _traitlets.Int(0).tag(sync=True)
        height = _traitlets.Int(0).tag(sync=True)

        def __init__(
            self,
            surface: EmbedSurface,
            *,
            height: Optional[int] = None,
            max_fps: float = 30.0,
            jpeg_quality: int = 80,
            hidpi: bool = False,
            **kwargs: Any,
        ) -> None:
            if not available():
                raise ImportError(_INSTALL_HINT)
            if height is None:
                height = round(surface.height / (surface.dpi_scale or 1.0))
            super().__init__(height=int(height), **kwargs)
            self.surface = surface
            self._last_png: Optional[bytes] = None
            self.controller = FrameController(
                surface,
                self._push_frame,
                max_fps=max_fps,
                jpeg_quality=jpeg_quality,
                hidpi=hidpi,
            )
            self.on_msg(self._on_custom_msg)
            # A first PNG so the output shows the plot before any event arrives
            # (and in saved widget state).
            self.controller.render_now(final=True)

        def refresh(self) -> None:
            """Re-render after changing the plot from Python (new data, limits, ...)."""
            self.controller.render_now(final=True)

        def _push_frame(self, data: bytes, mime: str, width: int, height: int) -> None:
            if mime == "image/png":
                self._last_png = data
            with self.hold_sync():
                self.frame_mime = mime
                self.frame_width = width
                self.frame_height = height
                self.frame = data

        def _repr_mimebundle_(self, **kwargs: Any) -> Any:
            # Add the last lossless frame next to the widget view, so outputs
            # still show the plot where widgets cannot render (nbconvert,
            # GitHub, an untrusted notebook, a closed kernel).
            bundle = super()._repr_mimebundle_(**kwargs)
            png = getattr(self, "_last_png", None)
            if bundle is None or not png:
                return bundle
            data, meta = bundle if isinstance(bundle, tuple) else (bundle, {})
            data = dict(data)
            data["image/png"] = base64.b64encode(png).decode("ascii")
            return data, meta

        def _on_custom_msg(self, _widget: Any, content: Any, _buffers: Any) -> None:
            if not isinstance(content, dict):
                return
            if content.get("type") == "events":
                try:
                    self.controller.handle_events(content.get("events") or [])
                finally:
                    # Flow control: the browser sends its next batch only after
                    # this ack, so input never queues up behind a slow kernel.
                    self.send({"type": "ack"})

        def close(self) -> None:
            self.controller.close()
            super().close()

else:

    class Canvas:  # type: ignore[no-redef]
        """Placeholder used when anywidget is not installed."""

        def __init__(self, *args: Any, **kwargs: Any) -> None:
            raise ImportError(_INSTALL_HINT)


def show(target: Any, **kwargs: Any) -> "Canvas":
    """Display ``target`` (an EmbedSurface or a figure created from one) as a live canvas.

    Returns the :class:`Canvas` so it can be displayed again or closed.
    Keyword arguments go to :class:`Canvas`.
    """
    surface = _surface_of(target)
    widget = Canvas(surface, **kwargs)
    try:
        from IPython.display import display
    except ImportError:  # pragma: no cover - outside IPython there is nothing to show
        return widget
    display(widget)
    return widget


def _surface_of(target: Any) -> EmbedSurface:
    if isinstance(target, EmbedSurface):
        return target
    if isinstance(target, EmbedFigure) and target.surface is not None:
        return target.surface
    raise TypeError(
        "spectra.notebook.show() takes an EmbedSurface or a figure created with "
        f"EmbedSurface.figure(), not {type(target).__name__}"
    )
