"""Jupyter inline mode, format strings and the vectorised NumPy data path.

These tests need no backend: IPython and the backend are replaced by fakes.
The backend round trips live in test_ipc_roundtrip.py.
"""

import os
import struct
import sys
import time
import types

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp  # noqa: E402
from spectra import _codec_fb, _easy, _inline, _series, _session  # noqa: E402
from spectra._style import (  # noqa: E402
    is_fmt,
    normalize_linestyle,
    normalize_marker,
    parse_fmt,
    resolve_style,
)


# ─── Format strings ───────────────────────────────────────────────────────────

@pytest.mark.parametrize(
    "fmt, expected",
    [
        ("r--o", ("r", "o", "--")),
        ("b:", ("b", None, ":")),
        ("g^", ("g", "^", None)),
        ("k-.", ("k", None, "-.")),
        ("o-r", ("r", "o", "-")),
        ("--", (None, None, "--")),
        ("D", (None, "D", None)),
        ("mH", ("m", "h", None)),
        ("c-..", ("c", None, "-..")),
    ],
)
def test_parse_fmt(fmt, expected):
    assert parse_fmt(fmt) == expected
    assert is_fmt(fmt)


@pytest.mark.parametrize("bad", ["", "q", "rr--", "red", "--:", "r-- "[:2] + "Z"])
def test_parse_fmt_rejects(bad):
    with pytest.raises(ValueError):
        parse_fmt(bad)
    assert not is_fmt(bad)


def test_style_names():
    assert normalize_linestyle("dashed") == "--"
    assert normalize_linestyle("Dotted") == ":"
    assert normalize_linestyle("None") == "none"
    assert normalize_linestyle("") == "none"
    assert normalize_marker("circle") == "o"
    assert normalize_marker("X") == "x"
    assert normalize_marker(None) == "none"
    with pytest.raises(ValueError):
        normalize_linestyle("wavy")
    with pytest.raises(ValueError):
        normalize_marker("8")


def test_resolve_style_keywords_win_and_marker_only_has_no_line():
    assert resolve_style("r--o") == ("r", "--", "o")
    assert resolve_style("g^") == ("g", "none", "^")  # markers only
    assert resolve_style("r") == ("r", None, None)  # colour only: keep defaults
    assert resolve_style("r--", linestyle=":") == ("r", ":", None)
    assert resolve_style("bo", marker="s") == ("b", "none", "s")
    assert resolve_style(None, linestyle="dotted", marker="circle") == (None, ":", "o")


# ─── Easy API with a fake backend ─────────────────────────────────────────────

class _FakeSeries:
    def __init__(self, kind, x, y):
        self.kind, self.x, self.y = kind, x, y
        self.calls = []

    def __getattr__(self, name):
        if name.startswith("set_"):
            return lambda *a: self.calls.append((name, a))
        raise AttributeError(name)


class _FakeAxes:
    _figure_id = 7
    _index = 0

    def __init__(self):
        self.created = []
        self.limits = {}

    def line(self, x, y, label=""):
        s = _FakeSeries("line", x, y)
        self.created.append(s)
        return s

    def scatter(self, x, y, label=""):
        s = _FakeSeries("scatter", x, y)
        self.created.append(s)
        return s

    def set_xlim(self, lo, hi):
        self.limits["x"] = (lo, hi)

    def set_ylim(self, lo, hi):
        self.limits["y"] = (lo, hi)


@pytest.fixture
def fake_axes(monkeypatch):
    ax = _FakeAxes()
    st = _easy._state
    monkeypatch.setattr(st, "_ensure_axes", lambda: ax)
    monkeypatch.setattr(st, "_show_if_pending", lambda: None)
    st._axes_bounds.clear()
    yield ax
    st._axes_bounds.clear()


def test_plot_accepts_format_strings(fake_axes):
    s = sp.plot([1.0, 2.0, 3.0], "r--")
    assert s.kind == "line" and len(s.y) == 3
    assert ("set_color_letter", ("r",)) in s.calls
    assert ("set_line_style", ("--",)) in s.calls

    s = sp.plot([0, 1, 2], [3, 4, 5], "g^")
    assert ("set_line_style", ("none",)) in s.calls
    assert ("set_marker_style", ("^",)) in s.calls

    s = sp.plot([0, 1], [1, 2], "k-.", marker="s", color="b")
    assert ("set_color_letter", ("b",)) in s.calls  # color= wins over the fmt colour
    assert ("set_line_style", ("-.",)) in s.calls
    assert ("set_marker_style", ("s",)) in s.calls

    s = sp.plot([0, 1], [1, 2], ls=":")
    assert ("set_line_style", (":",)) in s.calls

    with pytest.raises(ValueError, match="format string"):
        sp.plot([0, 1], [1, 2], "not-a-format")


def test_scatter_marker(fake_axes):
    s = sp.scatter([0, 1], [1, 2], "rs")
    assert ("set_marker_style", ("s",)) in s.calls
    assert ("set_color_letter", ("r",)) in s.calls
    s = sp.scatter([0, 1], [1, 2], marker="diamond")
    assert ("set_marker_style", ("diamond",)) in s.calls


def test_autofit_is_nan_aware(fake_axes):
    np = pytest.importorskip("numpy")
    y = np.array([np.nan, 1.0, np.inf, 3.0, -np.inf, 2.0])
    sp.plot(y)
    lo, hi = fake_axes.limits["y"]
    assert lo < 1.0 and 3.0 < hi and hi < 4.0
    lo, hi = fake_axes.limits["x"]
    assert lo < 0.0 and 5.0 < hi


def test_plot_keeps_numpy_arrays(fake_axes):
    np = pytest.importorskip("numpy")
    s = sp.plot(np.arange(5, dtype=np.int32), np.linspace(0, 1, 5))
    assert isinstance(s.x, np.ndarray) and s.x.dtype == np.float64
    assert isinstance(s.y, np.ndarray)


# ─── Vectorised encoding ──────────────────────────────────────────────────────

def _decode_fb_set_data(payload):
    from spectra._fb_generated.spectra.ipc.fb.ReqSetDataPayload import ReqSetDataPayload

    fb = ReqSetDataPayload.GetRootAs(bytearray(payload[1:]), 0)
    return fb.FigureId(), fb.SeriesIndex(), [fb.Data(i) for i in range(fb.DataLength())]


def test_fb_float_vector_matches_values():
    values = [0.0, 1.5, -2.25, 1e30, float("inf")]
    fid, sidx, data = _decode_fb_set_data(_codec_fb.encode_fb_req_set_data(3, 4, values))
    assert (fid, sidx) == (3, 4)
    assert data == [struct.unpack("<f", struct.pack("<f", v))[0] for v in values]
    np = pytest.importorskip("numpy")
    _, _, data_np = _decode_fb_set_data(
        _codec_fb.encode_fb_req_set_data(3, 4, np.array(values, dtype=np.float64)))
    assert data_np == data
    _, _, empty = _decode_fb_set_data(_codec_fb.encode_fb_req_set_data(3, 4, []))
    assert empty == []


def test_interleave_without_numpy_matches():
    assert _series._interleave_xy([1, 2, 3], [4, 5, 6]) == [1.0, 4.0, 2.0, 5.0, 3.0, 6.0]


def test_five_million_points_prepare_fast():
    """Python-side cost of sp.plot(x, y) for 5M points (was 7.6 s end to end)."""
    np = pytest.importorskip("numpy")
    n = 5_000_000
    x = np.linspace(0.0, 100.0, n)
    y = np.sin(x)
    t0 = time.perf_counter()
    xv, yv = _easy._parse_xy_args((x, y), as_array=True)
    origin = (_series._choose_data_origin(xv), _series._choose_data_origin(yv))
    raw, count = _series._try_interleave_numpy(_series._shift(xv, origin[0]),
                                               _series._shift(yv, origin[1]))
    _easy._finite_bounds(xv)
    _easy._finite_bounds(yv)
    elapsed = time.perf_counter() - t0
    assert count == 2 * n and len(raw) == 8 * n
    # ~0.3 s on a laptop; generous bound for slow CI runners.
    assert elapsed < 3.0, f"5M-point preparation took {elapsed:.2f} s"


# ─── IPC timeout ──────────────────────────────────────────────────────────────

def test_reply_timeout_scales_with_payload(monkeypatch):
    monkeypatch.delenv("SPECTRA_IPC_TIMEOUT", raising=False)
    s = _session.Session.__new__(_session.Session)
    s._busy_until = 0.0
    s._data_bytes = 0
    small = s._reply_timeout(_session.P.REQ_UPDATE_PROPERTY, 100)
    assert 5.0 <= small < 5.5
    big = s._reply_timeout(_session.P.REQ_SET_DATA, 40_000_000)
    assert big > small + 3.0
    # A request right after a big one still allows for the backend's backlog.
    after = s._reply_timeout(_session.P.REQ_ADD_SERIES, 100)
    assert after > small + 2.0
    export = s._reply_timeout(_session.P.REQ_EXPORT_FIGURE, 100)
    assert export > 60.0
    monkeypatch.setenv("SPECTRA_IPC_TIMEOUT", "30")
    s._busy_until = 0.0
    assert s._reply_timeout(_session.P.REQ_UPDATE_PROPERTY, 0) >= 30.0


def test_timeout_is_a_friendly_connection_error():
    assert issubclass(sp.TimeoutError, sp.ConnectionError)


def test_transport_timeout_raises_timeout_error():
    import socket

    from spectra._transport import Transport

    a, b = socket.socketpair()
    try:
        t = Transport(a)
        t.set_timeout(0.05)
        with pytest.raises(sp.TimeoutError):
            t.recv()
        assert t.is_open  # nothing was read: the stream is still usable
    finally:
        a.close()
        b.close()


# ─── Jupyter inline mode ──────────────────────────────────────────────────────

class _Events:
    def __init__(self):
        self.callbacks = {}

    def register(self, name, fn):
        self.callbacks.setdefault(name, []).append(fn)

    def unregister(self, name, fn):
        self.callbacks[name].remove(fn)

    def trigger(self, name):
        for fn in list(self.callbacks.get(name, [])):
            fn(None)


class ZMQInteractiveShell:  # the class name is what inline detection checks
    def __init__(self):
        self.events = _Events()


class _ColabShell(ZMQInteractiveShell):
    pass


class _FakeFigure:
    def __init__(self, fid):
        self.id = fid
        self.saved = []

    def savefig(self, path, width=0, height=0, format=None, theme=""):
        self.saved.append((width, height, format, theme))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\nfake")

    def _repr_png_(self):
        return _inline.render_png(self)


@pytest.fixture
def jupyter(monkeypatch):
    """A fake Jupyter kernel: IPython.get_ipython() and IPython.display.display()."""
    shell = ZMQInteractiveShell()
    shown = []
    ipy = types.ModuleType("IPython")
    ipy.get_ipython = lambda: shell
    disp = types.ModuleType("IPython.display")

    def display(obj):
        shown.append((obj, obj._repr_png_()))

    disp.display = display
    ipy.display = disp
    monkeypatch.setitem(sys.modules, "IPython", ipy)
    monkeypatch.setitem(sys.modules, "IPython.display", disp)
    monkeypatch.delenv("SPECTRA_DISPLAY", raising=False)
    saved = (_inline._settings.mode, _inline._settings.hook_shell)
    _inline._settings.mode = None
    _inline._settings.hook_shell = None
    _inline._settings.pending.clear()
    _inline._settings.cell_touched = False
    st = _easy._state
    monkeypatch.setattr(st, "_current_fig", None)
    monkeypatch.setattr(st, "_current_axes", None)
    yield shell, shown
    _inline._settings.pending.clear()
    _inline._settings.mode, _inline._settings.hook_shell = saved


def test_inline_detection(jupyter, monkeypatch):
    shell, _ = jupyter
    assert _inline.in_jupyter_kernel()
    assert sp.display_mode() == "inline"
    assert _inline._post_run_cell in shell.events.callbacks["post_run_cell"]
    monkeypatch.setitem(sys.modules["IPython"].__dict__, "get_ipython", lambda: _ColabShell())
    assert _inline.in_jupyter_kernel()  # subclasses (Colab) count too
    monkeypatch.setitem(sys.modules["IPython"].__dict__, "get_ipython", lambda: object())
    assert not _inline.in_jupyter_kernel()  # terminal IPython / plain Python


def test_env_var_and_switches(jupyter, monkeypatch):
    shell, _ = jupyter
    monkeypatch.setenv("SPECTRA_DISPLAY", "window")
    assert sp.display_mode() == "window"
    monkeypatch.delenv("SPECTRA_DISPLAY")
    sp.use_window()
    assert sp.display_mode() == "window"
    assert not shell.events.callbacks.get("post_run_cell")
    sp.use_inline(width=320, height=200, theme="dark")
    assert sp.display_mode() == "inline"
    assert (_inline._settings.width, _inline._settings.height) == (320, 200)
    assert _inline._settings.theme == "dark"
    sp.use_inline(width=_inline.DEFAULT_INLINE_WIDTH, height=_inline.DEFAULT_INLINE_HEIGHT,
                  theme=_inline.DEFAULT_INLINE_THEME)


def test_cell_figures_display_once_and_next_cell_starts_fresh(jupyter, monkeypatch):
    shell, shown = jupyter
    fig = _FakeFigure(1)
    st = _easy._state
    st._current_fig = fig
    shows = []
    monkeypatch.setattr(st, "_pending_show", {id(fig)})
    fig.show = lambda *a, **k: shows.append(1)

    st._show_if_pending()  # what every plot call does
    st._show_if_pending()
    assert shows == []  # no window in inline mode
    assert _inline._settings.pending == [fig]

    shell.events.trigger("post_run_cell")
    assert [f for f, _ in shown] == [fig]
    assert shown[0][1].startswith(b"\x89PNG")
    assert fig.saved == [(_inline._settings.width, _inline._settings.height, "png",
                          _inline._settings.theme)]
    assert st._current_fig is None  # the next plot creates a new figure

    shell.events.trigger("post_run_cell")  # nothing new drawn: nothing shown
    assert len(shown) == 1


def test_repr_of_last_expression_is_not_shown_twice(jupyter):
    shell, shown = jupyter
    fig = _FakeFigure(2)
    _inline.track(fig)
    assert fig._repr_png_().startswith(b"\x89PNG")  # IPython's displayhook
    shell.events.trigger("post_run_cell")
    assert shown == []


def test_figure_shown_by_last_expression_is_still_detached(jupyter):
    """`sp.plot(y); sp.gcf()`: the displayhook renders the figure before
    post_run_cell, and the next cell must still start a new figure."""
    shell, shown = jupyter
    fig = _FakeFigure(6)
    st = _easy._state
    st._current_fig = fig
    st._show_if_pending()  # the plot call
    assert fig._repr_png_().startswith(b"\x89PNG")  # displayhook consumes it
    assert _inline._settings.pending == []
    shell.events.trigger("post_run_cell")
    assert shown == []  # not shown twice
    assert st._current_fig is None  # but detached: the next plot starts fresh
    st._current_fig = fig
    shell.events.trigger("post_run_cell")  # an untouched cell keeps the figure
    assert st._current_fig is fig


def test_show_does_not_block_inline(jupyter, monkeypatch):
    _, shown = jupyter
    fig = _FakeFigure(3)
    st = _easy._state
    st._current_fig = fig
    session = types.SimpleNamespace(show=lambda: pytest.fail("show() blocked"))
    monkeypatch.setattr(st, "_session", session)
    sp.show()
    assert [f for f, _ in shown] == [fig]


def test_explicit_figure_size_is_kept(jupyter):
    fig = _FakeFigure(4)
    fig._size = (400, 300)
    _inline.render_png(fig)
    assert fig.saved[0][:2] == (400, 300)


def test_render_failure_falls_back_to_text(jupyter):
    fig = _FakeFigure(5)

    def boom(*a, **k):
        raise sp.BackendError(503, "no Vulkan device")

    fig.savefig = boom
    _inline._settings.warned_render = False
    with pytest.warns(RuntimeWarning, match="could not render"):
        assert _inline.render_png(fig) is None


def test_window_mode_outside_jupyter(monkeypatch):
    monkeypatch.delenv("SPECTRA_DISPLAY", raising=False)
    monkeypatch.setattr(_inline._settings, "mode", None)
    ipy = types.ModuleType("IPython")
    ipy.get_ipython = lambda: None  # plain python / not in a kernel
    monkeypatch.setitem(sys.modules, "IPython", ipy)
    assert sp.display_mode() == "window"
