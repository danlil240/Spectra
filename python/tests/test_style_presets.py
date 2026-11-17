"""Plot style presets — spectra.style.use / style.context and Figure.apply_style
send the figure "style" property (see src/ipc/style_state.hpp).

Run: python -m pytest tests/test_style_presets.py -v
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra
from spectra import _protocol as P
from spectra import style
from spectra._codec_fb import decode_fb_req_update_property
from spectra._figure import Figure


class _FakeSession:
    def __init__(self):
        self.sent = []

    def _request(self, msg_type, payload):
        assert msg_type == P.REQ_UPDATE_PROPERTY
        self.sent.append(decode_fb_req_update_property(payload))
        return {}


@pytest.fixture(autouse=True)
def _reset_style():
    style.use("default")
    yield
    style.use("default")


def test_available_matches_the_backend_presets():
    assert style.available[0] == "default"
    for name in ("paper", "talk", "minimal", "dark-paper"):
        assert name in style.available


def test_apply_style_sends_the_style_property():
    session = _FakeSession()
    fig = Figure(session, 5)
    fig.apply_style("paper")
    (msg,) = session.sent
    assert msg["prop"] == "style"
    assert msg["figure_id"] == 5
    assert msg["str_val"] == "paper"
    assert fig.style == "paper"


def test_unknown_style_raises_and_sends_nothing():
    session = _FakeSession()
    fig = Figure(session, 1)
    with pytest.raises(ValueError):
        fig.apply_style("comic-sans")
    with pytest.raises(ValueError):
        style.use("comic-sans")
    assert session.sent == []


def test_use_and_context_set_the_default_for_new_figures():
    assert style.current() is None
    style.use("talk")
    assert style.current() == "talk"
    with style.context("minimal"):
        assert style.current() == "minimal"
        with style.context("default"):
            assert style.current() is None
        assert style.current() == "minimal"
    assert style.current() == "talk"


def test_context_restores_on_error():
    style.use("paper")
    with pytest.raises(RuntimeError):
        with style.context("talk"):
            raise RuntimeError("boom")
    assert style.current() == "paper"


def test_style_is_exported_like_matplotlib():
    import spectra.pyplot as plt

    assert spectra.style is style
    assert plt.style is style
    assert "style" in spectra.__all__
