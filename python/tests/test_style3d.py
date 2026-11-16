"""3D styles from Python: depth shade, surface edges / floor contours and the
3D light (mode, angles, fog) are sent as REQ_UPDATE_PROPERTY requests.

No backend needed: the session is replaced by a fake that records requests.
The C++ side (daemon model, diff ops, window) is covered by
tests/unit/test_series_style_transport.cpp.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra import _codec as codec  # noqa: E402
from spectra import _protocol as P  # noqa: E402
from spectra._axes import Axes  # noqa: E402
from spectra._series import Series  # noqa: E402


class _FakeSession:
    def __init__(self):
        self.requests = []

    def _request(self, msg_type, payload):
        assert msg_type == P.REQ_UPDATE_PROPERTY
        self.requests.append(codec.decode_req_update_property(payload))
        return {"payload": b""}


def test_series_3d_styles():
    sess = _FakeSession()
    s = Series(sess, 7, 2, "surface", "s")
    s.set_depth_shade(False)
    s.set_show_edges()
    s.set_contour_projection(True, 12)
    s.set_contour_projection(False)
    props = [(r["prop"], r["bool_val"], r["f1"], r["series_index"]) for r in sess.requests]
    assert props[0] == ("depth_shade", False, 0.0, 2)
    assert props[1] == ("show_edges", True, 0.0, 2)
    assert props[2] == ("contour_projection", True, 12.0, 2)
    assert props[3][:2] == ("contour_projection", False)
    with pytest.raises(ValueError):
        s.set_contour_projection(True, 0)


def test_axes_3d_light():
    sess = _FakeSession()
    ax = Axes(sess, 7, 1, is_3d=True)
    ax.set_light_mode("Headlight")
    ax.set_light_angles(30, 45)
    ax.set_fog(0.3)
    r0, r1, r2 = sess.requests
    assert (r0["prop"], r0["str_val"], r0["axes_index"]) == ("light_mode", "headlight", 1)
    assert (r1["prop"], r1["f1"], r1["f2"]) == ("light_angles", 30.0, 45.0)
    assert r2["prop"] == "fog" and r2["f1"] == pytest.approx(0.3)
    with pytest.raises(ValueError):
        ax.set_light_mode("spot")
