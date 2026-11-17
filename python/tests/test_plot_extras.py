"""Step draw styles, dense-line modes and annotation box / halo / rotation from
Python: the request payloads (no backend needed).

Run: python -m pytest tests/test_plot_extras.py -v
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra._easy as easy  # noqa: E402
import spectra.pyplot as plt  # noqa: E402
from spectra import _protocol as P  # noqa: E402
from spectra._axes import Axes  # noqa: E402
from spectra._codec_fb import decode_fb_req_update_property  # noqa: E402
from spectra._series import Series, parse_dense_mode, parse_drawstyle  # noqa: E402

PACKED = 14
PACKED_WITH_STYLE = 35


class _FakeSession:
    def __init__(self):
        self.sent = []

    def _request(self, msg_type, payload):
        if msg_type == P.REQ_UPDATE_PROPERTY:
            self.sent.append(decode_fb_req_update_property(payload))
        return {}

    def props(self, name):
        return [m for m in self.sent if m["prop"] == name]


class _TestSeries(Series):
    """Series proxy that also keeps the data it was created with."""

    __slots__ = ("x", "y")


class _FakeAxes(Axes):
    """Axes proxy with an instance dict, so the test can replace line()."""


@pytest.fixture
def fake(monkeypatch):
    session = _FakeSession()
    ax = _FakeAxes(session, 5, 0)
    ax.lines = []

    def line(x, y, label=""):
        s = _TestSeries(session, 5, len(ax.lines), "line", label)
        s.x, s.y = list(x), list(y)
        ax.lines.append(s)
        return s

    ax.line = line
    st = easy._state
    monkeypatch.setattr(st, "_ensure_axes", lambda: ax)
    monkeypatch.setattr(st, "_show_if_pending", lambda: None)
    monkeypatch.setattr(easy, "_auto_fit_axes", lambda *a, **k: None)
    monkeypatch.setattr(easy, "_mark_datetime_x", lambda *a, **k: None)
    plt._warned.clear()
    yield session, ax
    plt._warned.clear()


def test_parse_drawstyle_and_dense_mode():
    assert parse_drawstyle("default") == 0
    assert parse_drawstyle("steps") == 1 and parse_drawstyle("steps-pre") == 1
    assert parse_drawstyle("steps-mid") == 2
    assert parse_drawstyle("post") == 3
    assert parse_dense_mode("auto") == 0
    assert parse_dense_mode("exact") == 1
    assert parse_dense_mode("density") == 2
    with pytest.raises(ValueError):
        parse_drawstyle("zigzag")
    with pytest.raises(ValueError):
        parse_dense_mode("blob")


def test_easy_plot_drawstyle_and_dense(fake):
    session, ax = fake
    easy.plot([0, 1, 2], [1, 2, 3], drawstyle="steps-post", dense="density")
    (msg,) = session.props("draw_style")
    assert msg["f1"] == 3.0 and msg["f2"] == 2.0
    assert msg["series_index"] == 0


def test_easy_step_defaults_to_pre_and_where(fake):
    session, _ = fake
    easy.step([0, 1, 2], [1, 2, 3])
    easy.step([0, 1, 2], [1, 2, 3], where="mid")
    msgs = session.props("draw_style")
    assert [m["f1"] for m in msgs] == [1.0, 2.0]


def test_easy_stairs_builds_post_steps(fake):
    session, ax = fake
    easy.stairs([3, 5, 2], [0, 1, 2, 4])
    line = ax.lines[-1]
    assert line.x == [0.0, 1.0, 2.0, 4.0]
    assert line.y == [3.0, 5.0, 2.0, 2.0]
    assert session.props("draw_style")[-1]["f1"] == 3.0
    with pytest.raises(ValueError):
        easy.stairs([1, 2], [0, 1])


def test_pyplot_step_and_drawstyle(fake):
    session, _ = fake
    plt.step([0, 1], [1, 2], where="post")
    plt.plot([0, 1], [1, 2], drawstyle="steps-mid")
    plt.plot([0, 1], [1, 2], ds="steps")
    assert [m["f1"] for m in session.props("draw_style")] == [3.0, 2.0, 1.0]


def test_series_setters_send_both_values():
    session = _FakeSession()
    s = Series(session, 1, 4, "line")
    s.set_dense_mode("exact")
    s.set_draw_style("steps-mid")
    first, second = session.props("draw_style")
    assert (first["f1"], first["f2"]) == (0.0, 1.0)
    assert (second["f1"], second["f2"]) == (2.0, 1.0)
    with pytest.raises(ValueError):
        Series(session, 1, 5, "scatter").set_draw_style("steps")


def _annotation_axes():
    session = _FakeSession()
    return session, Axes(session, 3, 1)


def test_text_bbox_halo_rotation_pack_style_tail():
    session, ax = _annotation_axes()
    ax.text(1, 2, "plain")
    assert len(session.sent[-1]["data"]) == PACKED

    ax.text(1, 2, "boxed", bbox={"pad": 6, "facecolor": (1, 1, 0.8, 0.7), "linewidth": 0},
            halo=(0, 0, 0, 1), rotation=30)
    d = session.sent[-1]["data"]
    assert len(d) == PACKED_WITH_STYLE
    assert d[14] == 1.0 and d[15] == pytest.approx(6.0)
    assert d[16] == 1.0 and d[20] == pytest.approx(0.7)
    assert d[21] == 0.0  # theme edge colour
    assert d[26] == 0.0  # no border
    assert d[27] == 1.0 and d[28] == 1.0  # halo with explicit colour
    assert d[33] == pytest.approx(2.0)
    assert d[34] == pytest.approx(30.0)


def test_annotation_setters_and_defaults():
    session, ax = _annotation_axes()
    ann = ax.annotate("max", xy=(3, 1), xytext=(4, 2), bbox=True)
    d = session.sent[-1]["data"]
    assert d[14] == 1.0 and d[15] == pytest.approx(4.0) and d[16] == 0.0
    ann.set_rotation(90)
    assert session.sent[-1]["data"][34] == pytest.approx(90.0)
    ann.set_halo(True, width=3)
    d = session.sent[-1]["data"]
    assert d[14] == 1.0  # box kept
    assert d[27] == 1.0 and d[28] == 0.0 and d[33] == pytest.approx(3.0)
    ann.set_bbox(False)
    assert session.sent[-1]["data"][14] == 0.0


def test_pyplot_text_maps_matplotlib_kwargs(fake):
    session = _FakeSession()
    ax = Axes(session, 1, 0)
    easy._state._ensure_axes = lambda: ax
    plt.text(1, 2, "hi", fontsize=20, rotation="vertical",
             bbox=dict(boxstyle="round", facecolor="white", pad=0.5))
    d = session.sent[-1]["data"]
    assert d[14] == 1.0
    assert d[15] == pytest.approx(10.0)  # 0.5 x font size 20
    assert d[16] == 1.0 and d[17] == pytest.approx(1.0)
    assert d[34] == pytest.approx(90.0)
    plt.annotate("pt", (1, 1), (2, 2), rotation=15)
    assert session.sent[-1]["data"][34] == pytest.approx(15.0)
