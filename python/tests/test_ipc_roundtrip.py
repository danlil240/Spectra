"""Python IPC round trip against a real spectra-backend.

Data and properties sent from Python must arrive in the backend's figure model
unchanged: the test sends them over the socket, asks the backend for its state
snapshot (REQ_GET_SNAPSHOT) and checks the decoded values, not just that a
capture happened.

Needs a built backend: set SPECTRA_BACKEND_PATH (CI does) or build it at
build/spectra-backend.  No window is opened (no figure is shown), so no
display or GPU is needed.  Skipped on Windows (UNIX socket path) and when no
backend binary is found.

Run: python -m pytest python/tests/test_ipc_roundtrip.py -v
"""

import math
import os
import subprocess
import sys
import tempfile
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra import _protocol as P  # noqa: E402
from spectra._session import Session  # noqa: E402

_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def _backend_binary():
    env = os.environ.get("SPECTRA_BACKEND_PATH")
    if env and os.path.isfile(env):
        return env
    local = os.path.join(_REPO, "build", "spectra-backend")
    return local if os.path.isfile(local) else None


pytestmark = [
    pytest.mark.skipif(sys.platform == "win32", reason="UNIX socket backend"),
    pytest.mark.skipif(_backend_binary() is None, reason="spectra-backend not built"),
]


@pytest.fixture
def session():
    # Short private socket path (sun_path limit), own backend process.
    sock_dir = tempfile.mkdtemp(prefix="spx")
    sock = os.path.join(sock_dir, "b.sock")
    log = open(os.path.join(sock_dir, "backend.log"), "wb")
    proc = subprocess.Popen([_backend_binary(), "--socket", sock], stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 10.0
        while not os.path.exists(sock):
            if proc.poll() is not None or time.monotonic() > deadline:
                pytest.fail(f"backend did not start (exit={proc.poll()})")
            time.sleep(0.05)
        s = Session(socket=sock, auto_launch=False)
        try:
            yield s
        finally:
            s.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


def _snapshot(session):
    """The backend's figure model, decoded into plain dicts."""
    from spectra._fb_generated.spectra.ipc.fb.StateSnapshotPayload import (
        StateSnapshotPayload,
    )

    resp = session._request(P.REQ_GET_SNAPSHOT, b"")
    assert resp["header"]["type"] == P.RESP_SNAPSHOT
    payload = resp["payload"]
    assert payload[0] == P.PAYLOAD_FORMAT_FLATBUFFERS
    snap = StateSnapshotPayload.GetRootAs(bytearray(payload[1:]), 0)

    def s(raw):
        return raw.decode("utf-8") if raw else ""

    figures = {}
    for i in range(snap.FiguresLength()):
        f = snap.Figures(i)
        axes = []
        for a in range(f.AxesLength()):
            ax = f.Axes(a)
            axes.append(
                {
                    "xlim": (ax.XMin(), ax.XMax()),
                    "ylim": (ax.YMin(), ax.YMax()),
                    "grid": ax.GridVisible(),
                    "xlabel": s(ax.XLabel()),
                    "ylabel": s(ax.YLabel()),
                    "title": s(ax.Title()),
                }
            )
        series = []
        for k in range(f.SeriesLength()):
            se = f.Series(k)
            series.append(
                {
                    "name": s(se.Name()),
                    "type": s(se.SeriesType()),
                    "axes": se.AxesIndex(),
                    "points": se.PointCount(),
                    # Logical values (the model may store data relative to an origin).
                    "data": [se.Data(j) for j in range(se.DataLength())],
                    "origin": (se.XOrigin(), se.YOrigin()),
                    "color": (se.ColorR(), se.ColorG(), se.ColorB(), se.ColorA()),
                    "line_width": se.LineWidth(),
                    "visible": se.Visible(),
                }
            )
        figures[f.FigureId()] = {
            "title": s(f.Title()),
            "grid": (f.GridRows(), f.GridCols()),
            "axes": axes,
            "series": series,
        }
    return figures


def _xy(series):
    """Interleaved [x0, y0, x1, y1, ...] -> absolute (x, y) pairs."""
    d = series["data"]
    ox, oy = series["origin"]
    return [(d[i] + ox, d[i + 1] + oy) for i in range(0, len(d) - 1, 2)]


def _close(a, b, tol=1e-5):
    return all(math.isclose(p, q, rel_tol=tol, abs_tol=tol) for p, q in zip(a, b))


def test_series_data_round_trips(session):
    fig = session.figure(title="IPC round trip")
    ax = fig.subplot(1, 1, 1)
    x = [0.0, 0.5, 1.0, 1.5, 2.0]
    y = [3.25, -1.0, 0.0, 1e-3, 42.0]
    line = ax.line(x, y, label="signal")
    pts = ax.scatter([10.0, 20.0], [-5.5, 7.75], label="events")

    snap = _snapshot(session)
    assert fig.id in snap
    f = snap[fig.id]
    assert f["title"] == "IPC round trip"
    assert [s["name"] for s in f["series"]] == ["signal", "events"]
    sig, ev = f["series"]
    assert sig["type"] == "line" and ev["type"] == "scatter"
    assert sig["points"] == len(x)
    pairs = _xy(sig)
    assert len(pairs) == len(x)
    for (px, py), ex, ey in zip(pairs, x, y):
        assert _close((px, py), (ex, ey)), (px, py, ex, ey)
    assert _close([c for p in _xy(ev) for c in p], [10.0, -5.5, 20.0, 7.75])

    # Replacing and appending data reaches the model too.
    line.set_data([1.0, 2.0], [9.0, 8.0])
    pts.append([30.0], [1.25])
    sig, ev = _snapshot(session)[fig.id]["series"]
    assert _close([c for p in _xy(sig) for c in p], [1.0, 9.0, 2.0, 8.0])
    assert ev["points"] == 3
    assert _close(_xy(ev)[-1], (30.0, 1.25))


def test_large_epoch_values_keep_precision(session):
    """Epoch-second timestamps survive the trip without float32 rounding."""
    fig = session.figure()
    ax = fig.subplot(1, 1, 1)
    t0 = 1_700_000_000.0
    x = [t0, t0 + 0.25, t0 + 0.5]
    ax.line(x, [1.0, 2.0, 3.0], label="epoch")
    (sig,) = _snapshot(session)[fig.id]["series"]
    xs = [p[0] for p in _xy(sig)]
    # Differences of a quarter second must be preserved (float32 alone has
    # 128 s resolution at this magnitude).
    assert math.isclose(xs[1] - xs[0], 0.25, abs_tol=1e-3), xs
    assert math.isclose(xs[0], t0, rel_tol=0.0, abs_tol=1e-3), xs
    assert _close([p[1] for p in _xy(sig)], [1.0, 2.0, 3.0])


def test_streamed_epoch_values_keep_precision(session):
    """append() into an empty series picks an origin from the first point."""
    fig = session.figure()
    ax = fig.subplot(1, 1, 1)
    live = ax.line([], [], label="live")
    t0 = 1_700_000_000.0
    live.append([t0, t0 + 0.1], [5.0, 6.0])
    live.append([t0 + 0.2], [7.0])
    (sig,) = _snapshot(session)[fig.id]["series"]
    assert sig["points"] == 3
    xs = [p[0] for p in _xy(sig)]
    assert all(math.isclose(a, b, rel_tol=0.0, abs_tol=1e-3) for a, b in zip(xs, [t0, t0 + 0.1, t0 + 0.2])), xs
    assert _close([p[1] for p in _xy(sig)], [5.0, 6.0, 7.0])

    # Replacing with small values drops the origin again.
    live.set_data([0.0, 1.0], [2.0, 3.0])
    (sig,) = _snapshot(session)[fig.id]["series"]
    assert sig["origin"] == (0.0, 0.0)
    assert _close([c for p in _xy(sig) for c in p], [0.0, 2.0, 1.0, 3.0])


def test_numpy_epoch_values_keep_precision(session):
    np = pytest.importorskip("numpy")
    fig = session.figure()
    ax = fig.subplot(1, 1, 1)
    t = 1_700_000_000.0 + np.arange(4, dtype=np.float64) * 0.05
    ax.line(t, np.array([1.0, 2.0, 3.0, 4.0]), label="np")
    (sig,) = _snapshot(session)[fig.id]["series"]
    xs = [p[0] for p in _xy(sig)]
    assert all(math.isclose(a, b, rel_tol=0.0, abs_tol=1e-3) for a, b in zip(xs, t.tolist())), xs


def test_axes_and_series_properties_round_trip(session):
    fig = session.figure()
    ax = fig.subplot(1, 1, 1)
    line = ax.line([0.0, 1.0], [0.0, 1.0], label="before")
    ax.set_xlim(-2.0, 8.5)
    ax.set_ylim(-1.0, 3.0)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("volts")
    ax.set_title("Battery")
    ax.grid(True)
    line.set_label("after")
    line.set_color(0.25, 0.5, 0.75, 1.0)
    line.set_line_width(3.5)
    line.set_visible(False)

    f = _snapshot(session)[fig.id]
    a = f["axes"][0]
    assert _close(a["xlim"], (-2.0, 8.5))
    assert _close(a["ylim"], (-1.0, 3.0))
    assert (a["xlabel"], a["ylabel"], a["title"]) == ("time (s)", "volts", "Battery")
    assert a["grid"] is True
    (s,) = f["series"]
    assert s["name"] == "after"
    assert _close(s["color"], (0.25, 0.5, 0.75, 1.0))
    assert math.isclose(s["line_width"], 3.5)
    assert s["visible"] is False


def test_subplots_and_series_removal(session):
    fig = session.figure()
    top = fig.subplot(2, 1, 1)
    bottom = fig.subplot(2, 1, 2)
    top.line([0.0, 1.0], [1.0, 2.0], label="top")
    bottom.line([0.0, 1.0], [5.0, 6.0], label="bottom-a")
    bottom.line([0.0, 1.0], [7.0, 8.0], label="bottom-b")

    f = _snapshot(session)[fig.id]
    assert f["grid"] == (2, 1)
    assert len(f["axes"]) == 2
    by_name = {s["name"]: s for s in f["series"]}
    assert by_name["top"]["axes"] == 0
    assert by_name["bottom-a"]["axes"] == 1
    assert by_name["bottom-b"]["axes"] == 1

    bottom.remove_series(0)
    names = [s["name"] for s in _snapshot(session)[fig.id]["series"]]
    assert names == ["top", "bottom-b"], "remove_series(i) addresses the axes' own series"
    with pytest.raises(IndexError):
        bottom.remove_series(5)
    top.remove_series(0)
    assert [s["name"] for s in _snapshot(session)[fig.id]["series"]] == ["bottom-b"]
