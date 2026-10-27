"""Text annotations over IPC — Axes.text()/annotate() send the "annotation"
property with the packed layout of src/ipc/annotation_state.hpp.

Run: python -m pytest tests/test_text_annotations.py -v
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra import _protocol as P
from spectra._axes import Axes, TextAnnotation
from spectra._codec_fb import decode_fb_req_update_property

APPEND = 0xFFFFFFFF
PACKED = 14


class _FakeSession:
    def __init__(self):
        self.sent = []

    def _request(self, msg_type, payload):
        assert msg_type == P.REQ_UPDATE_PROPERTY
        self.sent.append(decode_fb_req_update_property(payload))
        return {}


def _axes():
    session = _FakeSession()
    return session, Axes(session, 3, 1)


def test_text_appends_with_defaults():
    session, ax = _axes()
    ann = ax.text(1.5, 2.0, "peak")
    assert isinstance(ann, TextAnnotation)
    assert ann.index == 0
    (msg,) = session.sent
    assert msg["prop"] == "annotation"
    assert msg["figure_id"] == 3 and msg["axes_index"] == 1
    assert msg["series_index"] == APPEND
    assert msg["str_val"] == "peak"
    d = msg["data"]
    assert len(d) == PACKED
    assert d[0] == pytest.approx(1.5) and d[1] == pytest.approx(2.0)
    assert d[2] == 0.0  # theme colour
    assert d[7] == 0.0  # default font size
    assert d[8] == 0.0 and d[9] == 2.0  # left / bottom
    assert d[10] == 0.0  # no arrow
    assert d[13] == 1.0  # visible


def test_text_style_and_setters_address_index():
    session, ax = _axes()
    ax.text(0, 0, "a")
    ann = ax.text(1, 1, "b", color=(1.0, 0.0, 0.0, 0.5), fontsize=18, ha="right", va="top")
    assert ann.index == 1
    d = session.sent[-1]["data"]
    assert d[2] == 1.0 and d[3] == pytest.approx(1.0) and d[6] == pytest.approx(0.5)
    assert d[7] == pytest.approx(18.0)
    assert d[8] == 2.0 and d[9] == 0.0

    ann.set_text("b2").set_position(4, 5).set_visible(False)
    last = session.sent[-1]
    assert last["series_index"] == 1
    assert last["str_val"] == "b2"
    assert last["data"][0] == pytest.approx(4.0)
    assert last["data"][13] == 0.0


def test_annotate_sets_arrow_and_centres_text():
    session, ax = _axes()
    ax.annotate("max", xy=(3.0, 0.9), xytext=(4.0, 1.2))
    d = session.sent[-1]["data"]
    assert d[0] == pytest.approx(4.0) and d[1] == pytest.approx(1.2)
    assert d[8] == 1.0 and d[9] == 1.0
    assert d[10] == 1.0
    assert d[11] == pytest.approx(3.0) and d[12] == pytest.approx(0.9)


def test_clear_annotations_invalidates_proxies():
    session, ax = _axes()
    ann = ax.text(0, 0, "x")
    ax.clear_annotations()
    assert session.sent[-1]["prop"] == "clear_annotations"
    assert ax.annotations == []
    with pytest.raises(RuntimeError):
        ann.set_text("gone")


def test_bad_alignment_rejected():
    _, ax = _axes()
    with pytest.raises(ValueError):
        ax.text(0, 0, "x", ha="middle-ish")


def test_easy_api_exports():
    import spectra

    assert callable(spectra.text)
    assert callable(spectra.annotate)
    assert spectra.TextAnnotation is TextAnnotation
