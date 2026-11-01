"""Native bar/hist/band/stem series, axis scales, datetime axes, savefig and
spectra.pyplot — without a backend.

The easy state's axes/figure are replaced by fakes that record calls; the
proxy-level payload layout is checked with a fake session that captures the
IPC requests.  (test_ipc_roundtrip.py covers the same calls against a real
spectra-backend.)
"""

import datetime as dt
import math
import os
import sys
import warnings

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp  # noqa: E402
import spectra._easy as easy  # noqa: E402
import spectra.pyplot as plt  # noqa: E402
from spectra import _codec as codec  # noqa: E402
from spectra import _codec_fb as fb  # noqa: E402
from spectra import _protocol as P  # noqa: E402
from spectra._axes import Axes  # noqa: E402


# ─── Fakes ────────────────────────────────────────────────────────────────────

class _FakeSeries:
    def __init__(self, kind, **kw):
        self.kind = kind
        self.kw = kw
        self.letter = None
        self.rgb = None
        self.width = None
        self.marker_size = None
        self.opacity = None

    def set_color_letter(self, letter):
        self.letter = letter

    def set_color(self, r, g, b, a=1.0):
        self.rgb = (r, g, b, a)

    def set_line_width(self, w):
        self.width = w

    def set_marker_size(self, s):
        self.marker_size = s

    def set_opacity(self, o):
        self.opacity = o


class _FakeAxes:
    def __init__(self, index=0):
        self._figure_id = 1
        self._index = index
        self.created = []
        self.limits = [None, None]
        self.scales = {}
        self.datetime_calls = []

    def _add(self, kind, **kw):
        s = _FakeSeries(kind, **kw)
        self.created.append(s)
        return s

    def line(self, x, y, label=""):
        return self._add("line", x=list(x), y=list(y), label=label)

    def scatter(self, x, y, label=""):
        return self._add("scatter", x=list(x), y=list(y), label=label)

    def bar(self, x, heights, width=0.8, bottom=0.0, horizontal=False, label=""):
        return self._add("bar", x=list(x), heights=list(heights), width=width, bottom=bottom,
                         horizontal=horizontal, label=label)

    def histogram(self, values, bins=30, density=False, cumulative=False, label=""):
        return self._add("histogram", values=list(values), bins=bins, density=density,
                         cumulative=cumulative, label=label)

    def band(self, x, lower, upper, fill_opacity=0.25, edge_width=1.0, show_edges=True,
             label=""):
        return self._add("band", x=list(x), lower=list(lower), upper=list(upper),
                         fill_opacity=fill_opacity, show_edges=show_edges, label=label)

    def stem(self, x, y, baseline=0.0, stem_width=1.5, show_heads=True, label=""):
        return self._add("stem", x=list(x), y=list(y), baseline=baseline, label=label)

    def set_xlim(self, a, b):
        self.limits[0] = (a, b)

    def set_ylim(self, a, b):
        self.limits[1] = (a, b)

    def set_xscale(self, s):
        self.scales["x"] = s

    def set_yscale(self, s):
        self.scales["y"] = s

    def set_xaxis_datetime(self, on=True):
        self.datetime_calls.append(on)

    def set_title(self, t):
        self.title = t

    def set_xlabel(self, t):
        self.xlabel = t

    def set_ylabel(self, t):
        self.ylabel = t

    def grid(self, v=True):
        self.grid_on = v

    def legend(self, v=True):
        self.legend_on = v


class _FakeFigure:
    def __init__(self):
        self.saved = []
        self.title = ""
        self.axes = {}

    def savefig(self, path, **kw):
        self.saved.append((path, kw))
        return os.path.abspath(str(path))

    def subplot(self, rows, cols, index):
        return self.axes.setdefault((rows, cols, index), _FakeAxes(index - 1))


@pytest.fixture
def fake(monkeypatch):
    ax = _FakeAxes()
    fig = _FakeFigure()
    st = easy._state
    monkeypatch.setattr(st, "_ensure_axes", lambda: ax)
    monkeypatch.setattr(st, "_show_if_pending", lambda: None)
    monkeypatch.setattr(st, "_current_fig", fig)
    for d in (st._axes_bounds, st._axes_scales, st._errorbar_counts):
        d.clear()
    st._datetime_axes.clear()
    yield ax, fig
    for d in (st._axes_bounds, st._axes_scales, st._errorbar_counts):
        d.clear()
    st._datetime_axes.clear()


# ─── Native statistical series (easy API) ─────────────────────────────────────

def test_hist_uses_native_histogram_and_fits_counts(fake):
    ax, _ = fake
    s = sp.hist([1, 2, 2, 3, 3, 3, float("nan")], bins=3, color="r", label="h")
    assert s.kind == "histogram"
    assert s.kw["values"] == [1, 2, 2, 3, 3, 3]  # NaN dropped
    assert (s.kw["bins"], s.kw["label"], s.letter) == (3, "h", "r")
    lo, hi = ax.limits[1]
    assert lo < 0 < 3 < hi  # y fits 0..max count (3)
    assert ax.limits[0][0] < 1 and ax.limits[0][1] > 3


def test_hist_density_and_validation(fake):
    ax, _ = fake
    lo, hi, counts = easy._histogram_counts([0.0, 1.0, 1.0, 2.0], 2, True, False)
    assert (lo, hi) == (0.0, 2.0)
    assert math.isclose(sum(c * 1.0 for c in counts), 1.0)  # bin width 1
    _, _, cdf = easy._histogram_counts([0.0, 1.0, 1.0, 2.0], 2, True, True)
    assert cdf[-1] == 1.0
    s = sp.hist([1, 2, 3], bins=4, density=True, cumulative=True)
    assert s.kw["density"] and s.kw["cumulative"]
    with pytest.raises(TypeError):
        sp.hist([1, 2], bins=[0, 1, 2])
    with pytest.raises(ValueError):
        sp.hist([1, 2], bins=0)
    assert sp.hist([]) is None


def test_bar_positions_categories_and_horizontal(fake):
    ax, _ = fake
    s = sp.bar([1, 2, 3], [10, 20, 15], color="g", label="b")
    assert s.kind == "bar" and s.kw["x"] == [1.0, 2.0, 3.0] and s.kw["width"] == 0.8
    assert ax.limits[0][0] < 0.6 and ax.limits[0][1] > 3.4
    c = sp.bar(["a", "b"], [1, 2], width=0.5)
    assert c.kw["x"] == ["a", "b"] and c.kw["width"] == 0.5
    h = sp.barh([1, 2], [5, 6], height=0.4, left=1.0)
    assert h.kw["horizontal"] and h.kw["bottom"] == 1.0 and h.kw["width"] == 0.4
    with pytest.raises(ValueError):
        sp.bar([1, 2], [1])


def test_fill_between_orders_bounds_and_accepts_scalar(fake):
    ax, _ = fake
    s = sp.fill_between([0, 1, 2], [1, 5, 2], [3, 0, 2], alpha=0.4, color="b", label="ci")
    assert s.kind == "band"
    assert s.kw["lower"] == [1, 0, 2] and s.kw["upper"] == [3, 5, 2]
    assert s.kw["fill_opacity"] == 0.4 and s.letter == "b"
    z = sp.fill_between([0, 1], [2, 3])
    assert z.kw["lower"] == [0.0, 0.0] and z.kw["upper"] == [2, 3]
    with pytest.raises(ValueError):
        sp.fill_between([0, 1], [1, 2, 3])


def test_stem_is_native(fake):
    ax, _ = fake
    s = sp.stem([0, 1, 2], [3, -1, 2], color="m", baseline=0.5)
    assert s.kind == "stem" and s.kw["baseline"] == 0.5 and s.letter == "m"
    assert len(ax.created) == 1  # one series, not line + scatter
    assert ax.limits[1][0] < -1 and ax.limits[1][1] > 3


# ─── Scales ──────────────────────────────────────────────────────────────────

def test_log_scale_refits_to_positive_data(fake):
    ax, _ = fake
    sp.bar([1, 2, 3], [1, 10, 1000])  # bars start at 0
    assert ax.limits[1][0] < 0
    sp.yscale("log")
    assert ax.scales["y"] == "log"
    lo, hi = ax.limits[1]
    assert 0 < lo < 1 and hi > 1000
    sp.xscale("sqrt")
    assert ax.limits[0][0] >= 0
    with pytest.raises(ValueError):
        sp.xscale("symlog")


def test_semilog_and_loglog(fake):
    ax, _ = fake
    sp.semilogy([1, 2, 3], [1, 100, 10000])
    assert ax.scales == {"y": "log"}
    sp.semilogx([1, 10], [1, 2])
    assert ax.scales == {"x": "log", "y": "log"}
    ax.scales.clear()
    sp.loglog([1, 10], [1, 10])
    assert ax.scales == {"x": "log", "y": "log"}


def test_fit_range_helpers():
    assert easy._fit_range(0.0, 10.0, 1.0, "linear") == (-0.5, 10.5)
    lo, hi = easy._fit_range(-5.0, 100.0, 1.0, "log")
    assert 0 < lo < 1 and hi > 100
    assert easy._fit_range(-5.0, -1.0, None, "log") is None
    assert easy._fit_range(0.0, 4.0, 1.0, "sqrt")[0] == 0.0


# ─── Datetime x data ─────────────────────────────────────────────────────────

def test_datetime_list_enables_datetime_axis_once(fake):
    ax, _ = fake
    t0 = dt.datetime(2026, 1, 1, tzinfo=dt.timezone.utc)
    ts = [t0 + dt.timedelta(hours=i) for i in range(3)]
    s = sp.plot(ts, [1, 2, 3])
    assert s.kw["x"][0] == t0.timestamp()
    sp.scatter(ts, [3, 2, 1])
    assert ax.datetime_calls == [True]
    sp.plot([1, 2], [1, 2])  # numeric data does not toggle anything
    assert ax.datetime_calls == [True]


def test_numpy_and_pandas_datetimes(fake):
    np = pytest.importorskip("numpy")
    ax, _ = fake
    assert easy._is_datetime_like(np.array(["2026-01-01", "2026-01-02"], dtype="datetime64[D]"))
    assert not easy._is_datetime_like(np.array([1.0, 2.0]))
    assert not easy._is_datetime_like([1.0, 2.0])
    pd = pytest.importorskip("pandas")
    idx = pd.date_range("2026-01-01", periods=4, freq="D")
    ser = pd.Series([1.0, 2.0, 3.0, 4.0], index=idx)
    sp.plot(ser)
    assert ax.datetime_calls == [True]
    easy._state._datetime_axes.clear()
    sp.fill_between(idx, [0, 0, 0, 0], [1, 2, 3, 4])
    assert ax.datetime_calls == [True, True]
    assert easy._is_datetime_like(pd.Series(idx))


# ─── savefig ─────────────────────────────────────────────────────────────────

def test_savefig_delegates_to_current_figure(fake):
    _, fig = fake
    out = sp.savefig("x.png", width=640, height=480)
    assert out == os.path.abspath("x.png")
    assert fig.saved == [("x.png", {"width": 640, "height": 480, "format": None,
                                    "theme": "", "dpi": None})]


def test_savefig_without_figure(monkeypatch):
    monkeypatch.setattr(easy._state, "_current_fig", None)
    with pytest.raises(RuntimeError):
        sp.savefig("x.png")


# ─── Proxy payloads (fake session) ────────────────────────────────────────────

class _CaptureSession:
    """Records requests; answers REQ_ADD_SERIES with increasing indices."""

    def __init__(self):
        self.requests = []
        self.next_index = 0

    def _request(self, msg_type, payload=b""):
        self.requests.append((msg_type, payload))
        if msg_type == P.REQ_ADD_SERIES:
            import flatbuffers
            from spectra._fb_generated.spectra.ipc.fb import RespSeriesAddedPayload as R

            b = flatbuffers.Builder(32)
            R.Start(b)
            R.AddRequestId(b, 1)
            R.AddSeriesIndex(b, self.next_index)
            b.Finish(R.End(b))
            self.next_index += 1
            return {"payload": fb.FB_PREFIX + bytes(b.Output()), "header": {}}
        return {"payload": b"", "header": {}}

    def of_type(self, t):
        return [p for mt, p in self.requests if mt == t]


def _set_data_floats(payload):
    import struct

    if payload[:1] == bytes([P.PAYLOAD_FORMAT_FLATBUFFERS]):
        from spectra._fb_generated.spectra.ipc.fb.ReqSetDataPayload import ReqSetDataPayload

        t = ReqSetDataPayload.GetRootAs(bytearray(payload[1:]), 0)
        return [t.Data(i) for i in range(t.DataLength())]
    dec = codec.PayloadDecoder(payload)
    while dec.next():
        if dec.tag == P.TAG_BLOB_INLINE:
            blob = dec.as_blob()
            (n,) = struct.unpack_from("<I", blob, 0)
            return list(struct.unpack_from(f"<{n}f", blob, 4))
    raise AssertionError("no data in REQ_SET_DATA")


def test_axes_stats_payload_layout():
    sess = _CaptureSession()
    ax = Axes(sess, 5, 0)
    bar = ax.bar(["x", "y"], [2.0, 4.0], width=0.5, bottom=1.0, label="b")
    ax.histogram([1.0, float("nan"), 3.0], bins=7, density=True)
    ax.band([0.0, 1.0], [0.0, -1.0], [1.0, 2.0], fill_opacity=0.3, show_edges=False)
    ax.stem([0.0, 1.0], [5.0, 6.0], baseline=0.5, stem_width=2.0)

    from spectra._fb_generated.spectra.ipc.fb.ReqAddSeriesPayload import ReqAddSeriesPayload

    types = [ReqAddSeriesPayload.GetRootAs(bytearray(p[1:]), 0).SeriesType().decode()
             for p in sess.of_type(P.REQ_ADD_SERIES)]
    assert types == ["bar", "histogram", "band", "stem"]
    assert [s.series_type for s in ax.series] == ["bar", "histogram", "band", "stem"]

    data = [_set_data_floats(p) for p in sess.of_type(P.REQ_SET_DATA)]
    assert data[0] == [0.5, 1.0, 0.0, 0.0, 2.0, 0.0, 1.0, 2.0, 4.0]
    assert data[1] == [7.0, 0.0, 1.0, 0.0, 2.0, 1.0, 3.0]
    assert data[2] == pytest.approx([0.3, 1.0, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, -1.0, 1.0, 2.0])
    assert data[3] == [0.5, 2.0, 1.0, 0.0, 2.0, 0.0, 1.0, 5.0, 6.0]

    props = [codec.decode_req_update_property(p) for p in sess.of_type(P.REQ_UPDATE_PROPERTY)]
    (cats,) = [p for p in props if p["prop"] == "categories"]
    assert cats["series_index"] == bar.index
    assert cats["str_val"].split("\x1f") == ["x", "y"] and cats["f1"] == 2.0

    with pytest.raises(TypeError):
        bar.append([1.0], [1.0])
    with pytest.raises(ValueError):
        ax.band([0.0], [0.0, 1.0], [1.0])


def test_axes_scale_properties():
    sess = _CaptureSession()
    ax = Axes(sess, 5, 2)
    ax.set_xscale("log")
    ax.set_yscale("sqrt")
    ax.set_xaxis_datetime(True)
    props = [codec.decode_req_update_property(p) for p in sess.of_type(P.REQ_UPDATE_PROPERTY)]
    assert [(p["prop"], p["axes_index"]) for p in props] == [
        ("xscale", 2), ("yscale", 2), ("xaxis_datetime", 2)]
    assert props[0]["f1"] == 1.0 and props[1]["f1"] == 3.0 and props[2]["bool_val"] is True
    with pytest.raises(ValueError):
        ax.set_xscale("logit")
    assert len(sess.requests) == 3


def test_figure_savefig_request(tmp_path):
    from spectra._figure import Figure

    sess = _CaptureSession()
    fig = Figure(sess, 9)
    out = fig.savefig(tmp_path / "a.svg", width=10, height=20, theme="light")
    assert out == str(tmp_path / "a.svg")
    ((mt, payload),) = sess.requests
    assert mt == P.REQ_EXPORT_FIGURE
    d = codec.decode_req_export_figure(payload)
    assert d == {"figure_id": 9, "path": out, "format": "", "width": 10, "height": 20,
                 "theme": "light"}
    with pytest.raises(ValueError):
        fig.savefig("a.pdf", format="pdf")
    with pytest.raises(ValueError):
        fig.savefig("a.png", width=-1)


# ─── spectra.pyplot ──────────────────────────────────────────────────────────

def test_parse_fmt():
    assert plt._parse_fmt("r--o") == ("r", "o", "--")
    assert plt._parse_fmt("o") == (None, "o", None)
    assert plt._parse_fmt("k:") == ("k", None, ":")
    assert plt._parse_fmt("-.b") == ("b", None, "-.")
    with pytest.raises(ValueError):
        plt._parse_fmt("zz")


def test_split_plot_args():
    assert plt._split_plot_args(([1, 2],)) == [(None, [1, 2], "")]
    assert plt._split_plot_args(([1], [2], "r", [3], [4])) == [([1], [2], "r"), ([3], [4], "")]
    assert plt._split_plot_args(([1], "o")) == [(None, [1], "o")]


@pytest.fixture
def fake_plt(fake):
    plt._warned.clear()
    yield fake
    plt._warned.clear()


def test_pyplot_plot_routes_fmt(fake_plt):
    ax, _ = fake_plt
    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter("always")
        lines = plt.plot([0, 1], [1, 2], "r-", [0, 1], [2, 3], "bo", lw=3, label="L")
        plt.plot([0, 1], [1, 2], "g--")
        plt.plot([0, 1], [1, 2], "g--")
    assert [s.kind for s in lines] == ["line", "scatter"]
    assert lines[0].letter == "r" and lines[0].width == 3.0
    assert lines[1].letter == "b" and lines[1].marker_size == 6.0
    msgs = [str(x.message) for x in w]
    assert len([m for m in msgs if "--" in m]) == 1  # warned once


def test_pyplot_unknown_kwargs_warn_once(fake_plt):
    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter("always")
        plt.scatter([1], [2], s=25, c="k", zorder=3)
        plt.scatter([1], [2], zorder=4)
    assert [str(x.message) for x in w] == [
        "spectra.pyplot.scatter: keyword 'zorder' is not supported and was ignored"]
    ax, _ = fake_plt
    assert ax.created[0].marker_size == 5.0 and ax.created[0].letter == "k"


def test_pyplot_hist_returns_counts_and_edges(fake_plt):
    counts, edges, series = plt.hist([0.0, 1.0, 1.0, 2.0], bins=2)
    assert counts == [1.0, 3.0] and edges == [0.0, 1.0, 2.0]
    assert series.kind == "histogram" and series.kw["bins"] == 2


def test_pyplot_bar_fill_errorbar_stem(fake_plt):
    ax, _ = fake_plt
    plt.bar(["a", "b"], [1, 2], width=0.5, color="c", edgecolor="k")
    plt.barh([1, 2], [3, 4], height=0.3)
    plt.fill_between([0, 1], [0, 1], [1, 2], alpha=0.5, color="g")
    plt.errorbar([0, 1, 2], [1, 2, 3], yerr=0.1, fmt="o", capsize=3)
    plt.stem([0, 1], [1, 2], linefmt="r-")
    kinds = [s.kind for s in ax.created]
    assert kinds[:3] == ["bar", "bar", "band"]
    assert "scatter" in kinds and kinds[-1] == "stem"
    assert ax.created[-1].letter == "r"
    assert ax.created[2].kw["fill_opacity"] == 0.5


def test_pyplot_labels_limits_scales(fake_plt):
    ax, fig = fake_plt
    plt.plot([1, 2, 3], [1, 2, 3])
    plt.xlabel("x")
    plt.ylabel("y")
    plt.title("t")
    plt.xlim(0, 5)
    assert ax.limits[0] == (0.0, 5.0)
    plt.ylim(bottom=-1)
    assert ax.limits[1][0] == -1.0
    assert plt.xlim() == (1, 3)  # data bounds
    plt.yscale("log")
    assert ax.scales["y"] == "log"
    plt.tight_layout()
    assert (ax.xlabel, ax.ylabel, ax.title) == ("x", "y", "t")


def test_pyplot_subplots_shapes_and_axes_methods(monkeypatch, fake_plt):
    np = pytest.importorskip("numpy")
    created = []

    def fake_figure(title="", width=0, height=0):
        f = _FakeFigure()
        f.size = (width, height)
        created.append(f)
        easy._state._current_fig = f
        return f

    monkeypatch.setattr(easy, "figure", fake_figure)
    monkeypatch.setattr(easy._state, "_ensure_axes", lambda: easy._state._current_axes)
    monkeypatch.setattr(easy, "subplot",
                        lambda r, c, i: easy._state._current_fig.subplot(r, c, i))
    fig, ax = plt.subplots()
    assert isinstance(ax, plt.Axes)
    fig, axs = plt.subplots(1, 3, figsize=(6, 2))
    assert axs.shape == (3,) and created[-1].size == (600, 200)
    fig, axs = plt.subplots(2, 2)
    assert axs.shape == (2, 2)
    axs[1, 0].plot([0, 1], [0, 1], "k")
    under = axs[1, 0].spectra_axes
    assert under.created[0].kind == "line" and under.created[0].letter == "k"
    axs[0, 1].set_title("top right")
    assert axs[0, 1].spectra_axes.title == "top right"
    with pytest.raises(AttributeError):
        axs[0, 0].imshow
    fig.suptitle("S")
    assert fig.spectra_figure.title == "S"


def test_pyplot_savefig_dpi_scales_figsize(monkeypatch, fake_plt):
    def fake_figure(title="", width=0, height=0):
        f = _FakeFigure()
        easy._state._current_fig = f
        return f

    monkeypatch.setattr(easy, "figure", fake_figure)
    fig = plt.figure(figsize=(4, 3))
    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter("always")
        plt.savefig("o.png", dpi=200, bbox_inches="tight", transparent=True)
    assert fig.spectra_figure.saved[-1][1]["width"] == 800
    assert fig.spectra_figure.saved[-1][1]["height"] == 600
    assert [str(x.message) for x in w] == [
        "spectra.pyplot.savefig: keyword 'transparent' is not supported and was ignored"]
    fig.savefig("p.svg")
    assert fig.spectra_figure.saved[-1][0] == "p.svg"
