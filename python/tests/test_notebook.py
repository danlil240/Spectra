"""Tests for spectra.notebook (interactive Jupyter canvas), without a browser.

Three layers:
  * FrameController against a fake surface and a fake clock/scheduler: event
    mapping, throttling, frame dropping, JPEG -> PNG upgrade, settling.
  * FrameController against a real EmbedSurface (needs libspectra_embed):
    scroll and drag change the axis limits and produce new frames.
  * The anywidget Canvas model (needs anywidget): custom "events" messages
    are acked and update the synced frame.
"""

import io
import zlib

import pytest

PIL = pytest.importorskip("PIL")
from PIL import Image  # noqa: E402

from spectra import notebook  # noqa: E402
from spectra.notebook import FrameController  # noqa: E402

try:
    from spectra._embed import EmbedSurface, _load_lib

    _load_lib()
    _EMBED = True
except (ImportError, FileNotFoundError, OSError):
    _EMBED = False

_skip_embed = pytest.mark.skipif(not _EMBED, reason="libspectra_embed not built/loadable")
_skip_widget = pytest.mark.skipif(
    not notebook.available(), reason="anywidget not installed (spectra-plot[notebook])"
)


# ─── Fakes ───────────────────────────────────────────────────────────────────


class FakeLoop:
    """Manual clock + scheduler (stands in for the kernel's asyncio loop)."""

    def __init__(self):
        self.now = 0.0
        self.timers = []  # [due, seq, fn, alive]
        self._seq = 0

    def clock(self):
        return self.now

    def schedule(self, delay, fn):
        self._seq += 1
        entry = [self.now + delay, self._seq, fn, True]
        self.timers.append(entry)

        class _H:
            def cancel(_self):
                entry[3] = False

        return _H()

    def pending(self):
        return sum(1 for t in self.timers if t[3])

    def advance(self, dt):
        end = self.now + dt
        while True:
            live = sorted((t for t in self.timers if t[3] and t[0] <= end), key=lambda t: t[:2])
            if not live:
                break
            t = live[0]
            t[3] = False
            self.now = max(self.now, t[0])
            t[2]()
        self.timers = [t for t in self.timers if t[3]]
        self.now = end


class FakeSurface:
    """Records input calls; its pixels encode a 'view' counter."""

    def __init__(self, width=40, height=30, dpi_scale=1.0):
        self.width = width
        self.height = height
        self.dpi_scale = dpi_scale
        self.calls = []
        self.view = 0  # bumped by any input that changes the picture
        self.animating_frames = 0  # update() keeps changing pixels this many times
        self.renders = 0

    def mouse_move(self, x, y):
        self.calls.append(("move", x, y))

    def mouse_button(self, b, a, m, x, y):
        self.calls.append(("button", b, a, m, x, y))
        self.view += 1

    def scroll(self, dx, dy, cx, cy):
        self.calls.append(("scroll", dx, dy, cx, cy))
        self.view += 1

    def key(self, k, a, m):
        self.calls.append(("key", k, a, m))
        self.view += 1

    def update(self, dt):
        if self.animating_frames > 0:
            self.animating_frames -= 1
            self.view += 1

    def resize(self, w, h):
        self.width, self.height = w, h
        self.view += 1
        return True

    def set_dpi_scale(self, s):
        self.dpi_scale = s

    def render(self):
        self.renders += 1
        shade = (self.view * 37) % 256
        return bytes([shade, 255 - shade, 0, 255]) * (self.width * self.height)


def make(surface=None, **kw):
    loop = FakeLoop()
    surface = surface or FakeSurface()
    sent = []
    ctl = FrameController(
        surface,
        lambda data, mime, w, h: sent.append((mime, data, w, h)),
        schedule=loop.schedule,
        clock=loop.clock,
        **kw,
    )
    return ctl, surface, loop, sent


# ─── Event mapping ───────────────────────────────────────────────────────────


def test_buttons_map_browser_to_glfw_and_scale_to_surface_pixels():
    ctl, s, loop, _ = make(FakeSurface(80, 60), hidpi=True)
    ctl.resize(40, 30, dpr=2.0)  # CSS 40x30 at dpr 2 -> 80x60 px
    s.calls.clear()
    ctl.handle_events(
        [
            {"t": "down", "b": 0, "x": 10, "y": 5, "m": 1},
            {"t": "up", "b": 0, "x": 10, "y": 5},
            {"t": "down", "b": 2, "x": 1, "y": 1},  # browser right
            {"t": "down", "b": 1, "x": 1, "y": 1},  # browser middle
            {"t": "down", "b": 7, "x": 1, "y": 1},  # unknown: ignored
        ]
    )
    buttons = [c for c in s.calls if c[0] == "button"]
    assert buttons[0] == ("button", 0, 1, 1, 20.0, 10.0)
    assert buttons[1] == ("button", 0, 0, 0, 20.0, 10.0)
    assert buttons[2][1] == 1  # GLFW right
    assert buttons[3][1] == 2  # GLFW middle
    assert len(buttons) == 4
    assert ctl.events_handled == 4


def test_wheel_is_normalized_to_glfw_notches():
    ctl, s, _, _ = make()
    ctl.handle_events(
        [
            {"t": "wheel", "dx": 0, "dy": -100, "mode": 0, "x": 4, "y": 4},  # pixels, up
            {"t": "wheel", "dx": 0, "dy": 3, "mode": 1, "x": 4, "y": 4},  # lines, down
            {"t": "wheel", "dx": 0, "dy": -1e6, "mode": 0, "x": 4, "y": 4},  # clamped
            {"t": "wheel", "dx": 0, "dy": 0, "mode": 0, "x": 4, "y": 4},  # no-op
        ]
    )
    scrolls = [c for c in s.calls if c[0] == "scroll"]
    assert [c[2] for c in scrolls] == [1.0, -1.0, 10.0]
    assert ctl.events_handled == 3


def test_resize_follows_css_size_and_ignores_dpr_by_default():
    ctl, s, _, sent = make(FakeSurface(40, 30))
    ctl.handle_events([{"t": "resize", "w": 300, "h": 100, "dpr": 2}])
    assert (s.width, s.height, s.dpi_scale) == (300, 100, 1.0)
    ctl.handle_events([{"t": "down", "b": 0, "x": 150, "y": 50}])
    assert s.calls[-1] == ("button", 0, 1, 0, 150.0, 50.0)


def test_resize_follows_css_size_and_device_pixel_ratio_when_hidpi():
    ctl, s, _, sent = make(FakeSurface(40, 30), hidpi=True)
    ctl.handle_events([{"t": "resize", "w": 300, "h": 100, "dpr": 2}])
    assert (s.width, s.height, s.dpi_scale) == (600, 200, 2.0)
    assert ctl.css_size == (300.0, 100.0)
    assert sent and sent[-1][2:] == (600, 200)
    # Same size again: nothing to do, no new frame.
    n = len(sent)
    ctl.handle_events([{"t": "resize", "w": 300, "h": 100, "dpr": 2}])
    assert len(sent) == n
    # Absurd sizes are clamped.
    ctl.resize(1e6, 0.001, dpr=1)
    assert (s.width, s.height) == (8192, 16)


def test_unknown_and_malformed_events_do_not_render():
    ctl, s, _, sent = make()
    ctl.handle_events([{"t": "bogus"}, {}, {"t": "move"}, {"t": "wheel", "dy": "x"}, None])
    assert s.renders == 0 and not sent


# ─── Throttling and frame dropping ───────────────────────────────────────────


def test_first_event_renders_immediately_as_jpeg():
    ctl, s, loop, sent = make()
    ctl.handle_events([{"t": "wheel", "dy": -100, "x": 1, "y": 1}])
    assert s.renders == 1
    assert [m for m, *_ in sent] == ["image/jpeg"]
    Image.open(io.BytesIO(sent[0][1])).verify()


def test_throttle_caps_rate_and_drops_intermediate_frames():
    ctl, s, loop, sent = make(max_fps=30)
    ctl.handle_events([{"t": "wheel", "dy": -100, "x": 1, "y": 1}])
    assert s.renders == 1
    # 10 more batches inside the same 1/30 s slot: no render yet, one pending.
    for _ in range(10):
        loop.now += 0.002
        ctl.handle_events([{"t": "wheel", "dy": -100, "x": 1, "y": 1}])
    assert s.renders == 1
    assert ctl.frames_dropped == 9  # first one scheduled the flush, 9 coalesced
    assert len(s.calls) >= 11  # every event still reached the surface
    loop.advance(1 / 30)
    assert s.renders == 2  # one frame for all ten batches
    assert sent[-1][0] == "image/jpeg"


def test_sustained_input_never_exceeds_max_fps():
    ctl, s, loop, sent = make(max_fps=30)
    for _ in range(300):  # 1 s of events at 300 Hz
        ctl.handle_events([{"t": "move", "x": 1, "y": 1}, {"t": "wheel", "dy": -1, "x": 1, "y": 1}])
        loop.advance(1 / 300)
    assert 25 <= s.renders <= 31
    assert ctl.frames_dropped > 200


def test_idle_upgrades_to_png_once():
    ctl, s, loop, sent = make(idle_delay=0.2)
    ctl.handle_events([{"t": "wheel", "dy": -100, "x": 1, "y": 1}])
    loop.advance(0.1)
    assert [m for m, *_ in sent] == ["image/jpeg"]
    loop.advance(0.2)
    assert [m for m, *_ in sent] == ["image/jpeg", "image/png"]
    png = Image.open(io.BytesIO(sent[-1][1]))
    assert png.format == "PNG" and png.size == (s.width, s.height)
    loop.advance(5.0)
    assert len(sent) == 2 and loop.pending() == 0


def test_unchanged_pixels_are_not_resent():
    ctl, s, loop, sent = make()
    ctl.render_now()
    assert [m for m, *_ in sent] == ["image/png"]
    # Hover moves that do not change the picture: rendered, but nothing sent.
    ctl.handle_events([{"t": "move", "x": 3, "y": 3}])
    loop.advance(1.0)
    assert [m for m, *_ in sent] == ["image/png"]


def test_animation_keeps_streaming_until_it_settles():
    ctl, s, loop, sent = make(idle_delay=0.2)
    ctl.handle_events([{"t": "key", "k": 82, "a": 1}])  # e.g. animated reset
    s.animating_frames = 5
    loop.advance(2.0)
    mimes = [m for m, *_ in sent]
    assert mimes[-1] == "image/png"
    assert mimes.count("image/jpeg") >= 5
    assert s.animating_frames == 0
    assert loop.pending() == 0


def test_settle_gives_up_after_settle_time():
    ctl, s, loop, sent = make(idle_delay=0.2, settle_time=0.5)
    ctl.handle_events([{"t": "key", "k": 82, "a": 1}])
    s.animating_frames = 10**6  # never settles
    loop.advance(3.0)
    assert sent[-1][0] == "image/png"
    assert loop.pending() == 0


def test_without_an_event_loop_frames_are_synchronous():
    s = FakeSurface()
    sent = []
    ctl = FrameController(s, lambda d, m, w, h: sent.append(m), schedule=lambda d, f: None)
    ctl.handle_events([{"t": "wheel", "dy": -100, "x": 1, "y": 1}])
    ctl.handle_events([{"t": "wheel", "dy": -100, "x": 1, "y": 1}])
    assert s.renders == 2
    assert sent == ["image/jpeg", "image/png", "image/jpeg", "image/png"]


def test_max_fps_must_be_positive():
    with pytest.raises(ValueError):
        FrameController(FakeSurface(), lambda *a: None, max_fps=0)


def test_show_rejects_unsupported_targets():
    with pytest.raises(TypeError):
        notebook._surface_of(object())


# ─── Real EmbedSurface ───────────────────────────────────────────────────────


def _real(width=320, height=240):
    surface = EmbedSurface(width, height)
    fig = surface.figure()
    ax = fig.subplot(1, 1, 1)
    ax.line([0.0, 1.0, 2.0, 3.0, 4.0], [0.0, 1.0, 4.0, 9.0, 16.0])
    views = []
    surface.set_on_view_changed(lambda *v: views.append(v))
    return surface, fig, views


@_skip_embed
def test_real_scroll_zooms_and_sends_new_frame():
    surface, _, views = _real()
    ctl, _, loop, sent = make(surface)
    ctl.render_now()
    before = views[-1]
    loop.now = 1.0  # outside the frame budget of the first render
    ctl.handle_events([{"t": "wheel", "dy": -300, "x": 160, "y": 120}])
    after = views[-1]
    assert (after[1] - after[0]) < (before[1] - before[0])  # x range shrank
    assert (after[3] - after[2]) < (before[3] - before[2])  # y range shrank
    assert [m for m, *_ in sent] == ["image/png", "image/jpeg"]
    assert zlib.crc32(sent[0][1]) != zlib.crc32(sent[1][1])
    loop.advance(1.0)
    assert sent[-1][0] == "image/png"
    surface.close()


@_skip_embed
def test_real_drag_pans():
    surface, _, views = _real()
    ctl, _, loop, sent = make(surface)
    ctl.render_now()
    x0 = views[-1][:2]
    events = [{"t": "down", "b": 0, "x": 160, "y": 120}]
    events += [{"t": "move", "x": 160 - 10 * i, "y": 120} for i in range(1, 9)]
    events += [{"t": "up", "b": 0, "x": 80, "y": 120}]
    ctl.handle_events(events)
    loop.advance(2.0)  # let any inertial pan settle
    x1 = views[-1][:2]
    assert x1[0] > x0[0] and x1[1] > x0[1]  # dragged left -> view moved right
    assert sent[-1][0] == "image/png"
    surface.close()


@_skip_embed
def test_real_resize_matches_dpr():
    surface, _, _ = _real()
    ctl, _, _, sent = make(surface, hidpi=True)
    ctl.handle_events([{"t": "resize", "w": 200, "h": 100, "dpr": 2}])
    assert (surface.width, surface.height) == (400, 200)
    assert surface.dpi_scale == pytest.approx(2.0)
    assert Image.open(io.BytesIO(sent[-1][1])).size == (400, 200)
    surface.close()


@_skip_embed
def test_figure_remembers_its_surface():
    surface, fig, _ = _real()
    assert fig.surface is surface
    assert notebook._surface_of(fig) is surface
    assert notebook._surface_of(surface) is surface
    surface.close()


@_skip_embed
def test_ipython_display_falls_back_to_png_outside_a_kernel(monkeypatch):
    pytest.importorskip("IPython")
    import IPython.display

    shown = []
    monkeypatch.setattr(IPython.display, "display", lambda obj, **kw: shown.append((obj, kw)))
    surface, _, _ = _real(64, 48)
    surface._ipython_display_()
    bundle, kw = shown[0]
    assert kw == {"raw": True}
    assert "image/png" in bundle and "text/plain" in bundle
    surface.close()


# ─── anywidget model ─────────────────────────────────────────────────────────


@_skip_embed
@_skip_widget
def test_canvas_model_round_trip():
    surface, _, views = _real()
    w = notebook.Canvas(surface)
    acks = []
    w.send = lambda content, buffers=None: acks.append(content)
    assert w.frame_mime == "image/png" and len(w.frame) > 0
    assert w.height == 240 and (w.frame_width, w.frame_height) == (320, 240)
    first = bytes(w.frame)
    before = views[-1]

    # Exactly what the browser sends (ipywidgets dispatches custom msgs here).
    w._handle_custom_msg(
        {"type": "events", "events": [{"t": "wheel", "dy": -200, "mode": 0, "x": 160, "y": 120}]},
        [],
    )
    assert acks == [{"type": "ack"}]
    # No asyncio loop runs here, so the idle PNG follows the JPEG immediately.
    assert w.frame_mime == "image/png" and bytes(w.frame) != first
    assert (views[-1][1] - views[-1][0]) < (before[1] - before[0])

    data, _meta = w._repr_mimebundle_()
    assert "application/vnd.jupyter.widget-view+json" in data
    assert "image/png" in data  # static fallback for nbconvert / GitHub

    # Malformed messages are still acked and do not raise.
    w._handle_custom_msg({"type": "events", "events": [{"t": "move"}]}, [])
    assert acks[-1] == {"type": "ack"}
    w.close()
    surface.close()


@_skip_embed
@_skip_widget
def test_surface_widget_is_cached():
    surface, _, _ = _real(64, 48)
    w = surface.widget()
    assert surface.widget() is w
    assert surface.widget(height=100) is not w
    w.close()
    surface.close()
