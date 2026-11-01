"""Tests for DataFrame/array-like input and errorbar() in the easy API.

They run without a backend: the easy state's axes is replaced by a fake that
records the series it is asked to create.
"""

import math
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra._easy as easy  # noqa: E402
from spectra._easy import _to_list  # noqa: E402

np = pytest.importorskip("numpy")


class _FakeSeries:
    def __init__(self, kind, x, y, label):
        self.kind, self.x, self.y, self.label = kind, list(x), list(y), label
        self.letter = None
        self.rgb = None
        self.width = None
        self.marker_size = None

    def set_color_letter(self, letter):
        self.letter = letter

    def set_color(self, r, g, b, a=1.0):
        self.rgb = (r, g, b, a)

    def set_line_width(self, w):
        self.width = w

    def set_marker_size(self, s):
        self.marker_size = s


class _FakeAxes:
    _figure_id = 1
    _index = 0

    def __init__(self):
        self.created = []
        self.limits = None

    def line(self, x, y, label=""):
        s = _FakeSeries("line", x, y, label)
        self.created.append(s)
        return s

    def scatter(self, x, y, label=""):
        s = _FakeSeries("scatter", x, y, label)
        self.created.append(s)
        return s

    def set_xlim(self, a, b):
        self.limits = (a, b, self.limits[2] if self.limits else None,
                       self.limits[3] if self.limits else None)

    def set_ylim(self, a, b):
        self.limits = (self.limits[0], self.limits[1], a, b)


@pytest.fixture
def fake_axes(monkeypatch):
    ax = _FakeAxes()
    monkeypatch.setattr(easy._state, "_ensure_axes", lambda: ax)
    monkeypatch.setattr(easy._state, "_show_if_pending", lambda: None)
    easy._state._axes_bounds.clear()
    easy._state._errorbar_counts.clear()
    yield ax
    easy._state._axes_bounds.clear()
    easy._state._errorbar_counts.clear()


def _nan_eq(a, b):
    assert len(a) == len(b)
    for u, v in zip(a, b):
        if isinstance(v, float) and math.isnan(v):
            assert math.isnan(u)
        else:
            assert u == pytest.approx(v)


# ─── _to_list conversions ────────────────────────────────────────────────────

class TestToListArrayLikes:
    def test_datetime64_becomes_epoch_seconds(self):
        arr = np.array(["1970-01-01T00:00:01", "2024-01-01T00:00:00"], dtype="datetime64[s]")
        assert _to_list(arr) == [1.0, 1704067200.0]

    def test_nat_becomes_nan(self):
        arr = np.array(["2024-01-01", "NaT"], dtype="datetime64[D]")
        out = _to_list(arr)
        assert out[0] == 1704067200.0
        assert math.isnan(out[1])

    def test_timedelta64_becomes_seconds(self):
        arr = np.array([1500, 250], dtype="timedelta64[ms]")
        assert _to_list(arr) == [1.5, 0.25]

    def test_array_protocol_object(self):
        class Wrapped:
            def __array__(self, dtype=None, copy=None):
                return np.array([1, 2, 3])

        assert _to_list(Wrapped()) == [1.0, 2.0, 3.0]

    def test_list_with_none_becomes_nan(self):
        out = _to_list([1, None, 3])
        assert out[0] == 1.0 and math.isnan(out[1]) and out[2] == 3.0

    def test_generator_still_works(self):
        assert _to_list(v for v in (1, 2)) == [1.0, 2.0]


class TestToListPandas:
    pd = pytest.importorskip("pandas")

    def test_series(self):
        assert _to_list(self.pd.Series([1, 2, 3])) == [1.0, 2.0, 3.0]

    def test_nullable_int_with_na(self):
        out = _to_list(self.pd.Series([1, None, 3], dtype="Int64"))
        assert out[0] == 1.0 and math.isnan(out[1]) and out[2] == 3.0

    def test_datetime_index(self):
        idx = self.pd.date_range("2024-01-01", periods=2, freq="h")
        assert _to_list(idx) == [1704067200.0, 1704070800.0]

    def test_tz_aware_series(self):
        s = self.pd.Series(self.pd.date_range("2024-01-01", periods=1, tz="UTC"))
        assert _to_list(s) == [1704067200.0]

    def test_dataframe_rejected_with_hint(self):
        with pytest.raises(TypeError, match="sp.plot\\(df\\)"):
            _to_list(self.pd.DataFrame({"a": [1]}))


# ─── plot() with pandas ──────────────────────────────────────────────────────

class TestPlotPandas:
    pd = pytest.importorskip("pandas")

    def test_series_uses_index_and_name(self, fake_axes):
        s = self.pd.Series([5.0, 6.0], index=[10, 20], name="temp")
        series = easy.plot(s)
        assert series.x == [10.0, 20.0]
        assert series.y == [5.0, 6.0]
        assert series.label == "temp"

    def test_explicit_label_wins(self, fake_axes):
        series = easy.plot(self.pd.Series([1.0], name="temp"), label="mine")
        assert series.label == "mine"

    def test_string_index_falls_back_to_row_numbers(self, fake_axes):
        s = self.pd.Series([1.0, 2.0], index=["a", "b"])
        assert easy.plot(s).x == [0.0, 1.0]

    def test_dataframe_one_line_per_numeric_column(self, fake_axes):
        df = self.pd.DataFrame(
            {"a": [1, 2], "b": [3.0, 4.0], "name": ["x", "y"]},
            index=self.pd.date_range("2024-01-01", periods=2, freq="D"),
        )
        out = easy.plot(df, color="r")
        assert [s.label for s in out] == ["a", "b"]
        assert out[0].x == [1704067200.0, 1704153600.0]
        assert out[1].y == [3.0, 4.0]
        assert all(s.letter == "r" for s in out)

    def test_dataframe_label_is_prefix(self, fake_axes):
        df = self.pd.DataFrame({"a": [1.0]})
        assert easy.plot(df, label="run1/")[0].label == "run1/a"

    def test_dataframe_without_numeric_columns(self, fake_axes):
        with pytest.raises(ValueError, match="no numeric columns"):
            easy.plot(self.pd.DataFrame({"s": ["x"]}))

    def test_plain_numpy_still_uses_row_numbers(self, fake_axes):
        series = easy.plot(np.array([3.0, 4.0]))
        assert series.x == [0.0, 1.0]
        assert series.label == ""


# ─── errorbar() ──────────────────────────────────────────────────────────────

class TestErrorbar:
    def test_symmetric_yerr_draws_vertical_segments(self, fake_axes):
        data, bars = easy.errorbar([0, 1], [1, 2], yerr=0.5, label="m")
        assert data.kind == "line" and data.label == "m"
        assert bars.label == ""
        nan = float("nan")
        _nan_eq(bars.x, [0, 0, nan, 1, 1, nan])
        _nan_eq(bars.y, [0.5, 1.5, nan, 1.5, 2.5, nan])

    def test_asymmetric_pair(self, fake_axes):
        _, bars = easy.errorbar([0, 1, 2], [0, 0, 0], yerr=([1, 1, 1], [2, 2, 2]))
        assert bars.y[0] == -1 and bars.y[1] == 2

    def test_asymmetric_numpy_2xn(self, fake_axes):
        err = np.array([[1.0, 1.0], [3.0, 3.0]])
        _, bars = easy.errorbar([0, 1], [0, 0], yerr=err)
        assert bars.y[:2] == [-1.0, 3.0]

    def test_two_points_two_scalars_is_symmetric(self, fake_axes):
        # With n == 2 a length-2 list of numbers is per-point, not (lower, upper).
        _, bars = easy.errorbar([0, 1], [0, 0], yerr=[1, 2])
        assert bars.y[:2] == [-1.0, 1.0]
        assert bars.y[3:5] == [-2.0, 2.0]

    def test_two_points_pair_of_lists_is_asymmetric(self, fake_axes):
        _, bars = easy.errorbar([0, 1], [0, 0], yerr=([1, 1], [2, 2]))
        assert bars.y[:2] == [-1.0, 2.0]

    def test_xerr_and_caps(self, fake_axes):
        _, bars = easy.errorbar([5], [1], xerr=1, capsize=0.4)
        nan = float("nan")
        _nan_eq(bars.x, [4, 6, nan, 4, 4, nan, 6, 6, nan])
        _nan_eq(bars.y, [1, 1, nan, 0.8, 1.2, nan, 0.8, 1.2, nan])

    def test_marker_fmt_uses_scatter(self, fake_axes):
        data, _ = easy.errorbar([0], [0], yerr=1, fmt="o")
        assert data.kind == "scatter" and data.marker_size == 6.0

    def test_fmt_none_puts_label_on_bars(self, fake_axes):
        data, bars = easy.errorbar([0], [0], yerr=1, fmt="none", label="err")
        assert data is None and bars.label == "err"

    def test_shared_default_colour_cycles_per_call(self, fake_axes):
        d1, b1 = easy.errorbar([0], [0], yerr=1)
        d2, b2 = easy.errorbar([0], [0], yerr=1)
        assert d1.letter == b1.letter == "b"
        assert d2.letter == b2.letter == "r"

    def test_explicit_colour(self, fake_axes):
        d, b = easy.errorbar([0], [0], yerr=1, color="#ff0000")
        assert d.rgb == b.rgb and d.rgb[0] == pytest.approx(1.0)

    def test_autofit_includes_bars(self, fake_axes):
        easy.errorbar([0, 10], [0, 0], yerr=5)
        xmin, xmax, ymin, ymax = fake_axes.limits
        assert ymin < -5 and ymax > 5

    def test_length_mismatch(self, fake_axes):
        with pytest.raises(ValueError, match="yerr has 1 values"):
            easy.errorbar([0, 1, 2], [0, 0, 0], yerr=[1])

    def test_bad_fmt(self, fake_axes):
        with pytest.raises(ValueError, match="fmt"):
            easy.errorbar([0], [0], fmt="r--")

    def test_exported(self):
        import spectra

        assert spectra.errorbar is easy.errorbar
        assert "errorbar" in spectra.__all__
