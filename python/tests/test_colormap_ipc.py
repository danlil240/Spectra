"""Series colormaps from Python: ``Series.set_colormap`` / ``set_colormap_range``
and ``surf(cmap=..., vmin=..., vmax=...)`` send REQ_UPDATE_PROPERTY requests
("colormap", "colormap_range").

No backend needed: the session is replaced by a fake that records requests.
The C++ side (daemon model, SET_SERIES_COLORMAP, window) is covered by
tests/unit/test_series_style_transport.cpp.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra import _codec as codec  # noqa: E402
from spectra import _protocol as P  # noqa: E402
from spectra._series import Series  # noqa: E402


class _FakeSession:
    def __init__(self):
        self.requests = []

    def _request(self, msg_type, payload):
        assert msg_type == P.REQ_UPDATE_PROPERTY
        self.requests.append(codec.decode_req_update_property(payload))
        return {"payload": b""}


class _NamedCmap:
    name = "magma"


def test_colormap_by_name_and_object():
    sess = _FakeSession()
    s = Series(sess, 7, 3, "surface", "s")
    s.set_colormap("RdBu_r")
    s.set_colormap(_NamedCmap())
    r0, r1 = sess.requests
    assert (r0["prop"], r0["str_val"], r0["series_index"]) == ("colormap", "RdBu_r", 3)
    assert (r1["prop"], r1["str_val"]) == ("colormap", "magma")


def test_colormap_from_colours():
    sess = _FakeSession()
    s = Series(sess, 7, 0, "surface", "s")
    s.set_colormap(["#ff0000", (0.0, 0.0, 1.0), "#00ff0080"])
    req = sess.requests[0]
    assert (req["prop"], req["str_val"]) == ("colormap", "custom")
    assert list(req["data"]) == pytest.approx(
        [1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 128 / 255], abs=1e-3
    )
    with pytest.raises(ValueError):
        s.set_colormap([])
    with pytest.raises(ValueError):
        s.set_colormap(["not-a-colour"])


def test_colormap_range():
    sess = _FakeSession()
    s = Series(sess, 7, 1, "surface", "s")
    s.set_colormap_range(-2, 3)
    s.set_colormap_range()
    r0, r1 = sess.requests
    assert (r0["prop"], r0["bool_val"], r0["f1"], r0["f2"]) == ("colormap_range", True, -2.0, 3.0)
    assert (r1["prop"], r1["bool_val"]) == ("colormap_range", False)
    with pytest.raises(ValueError):
        s.set_colormap_range(1, None)
    with pytest.raises(ValueError):
        s.set_colormap_range(2, 1)
