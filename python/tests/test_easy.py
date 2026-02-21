"""Tests for the easy API (_easy.py).

These tests verify the pure-Python logic without requiring a backend connection.
They test argument parsing, color parsing, data conversion, and API surface.
"""

import pytest
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._easy import (
    _to_list,
    _parse_color,
    _parse_xy_args,
    _EasyState,
)


# ─── _to_list ────────────────────────────────────────────────────────────────

class TestToList:
    def test_list_of_ints(self):
        assert _to_list([1, 2, 3]) == [1.0, 2.0, 3.0]

    def test_list_of_floats(self):
        assert _to_list([1.5, 2.5]) == [1.5, 2.5]

    def test_tuple(self):
        assert _to_list((10, 20, 30)) == [10.0, 20.0, 30.0]

    def test_generator(self):
        result = _to_list(x * 2 for x in range(3))
        assert result == [0.0, 2.0, 4.0]

    def test_range(self):
        assert _to_list(range(4)) == [0.0, 1.0, 2.0, 3.0]

    def test_empty(self):
        assert _to_list([]) == []

    def test_single(self):
        assert _to_list([42]) == [42.0]

    def test_numpy_array(self):
        try:
            import numpy as np
            arr = np.array([1.0, 2.0, 3.0])
            result = _to_list(arr)
            assert result == [1.0, 2.0, 3.0]
        except ImportError:
            pytest.skip("numpy not installed")

    def test_numpy_2d(self):
        try:
            import numpy as np
            arr = np.array([[1, 2], [3, 4]])
            result = _to_list(arr)
            assert result == [1.0, 2.0, 3.0, 4.0]
        except ImportError:
            pytest.skip("numpy not installed")


# ─── _parse_color ─────────────────────────────────────────────────────────────

class TestParseColor:
    def test_none(self):
        assert _parse_color(None) is None

    def test_named_red(self):
        r, g, b, a = _parse_color("red")
        assert r == 1.0
        assert a == 1.0

    def test_named_shorthand(self):
        assert _parse_color("r") is not None
        assert _parse_color("g") is not None
        assert _parse_color("b") is not None
        assert _parse_color("c") is not None
        assert _parse_color("m") is not None
        assert _parse_color("y") is not None
        assert _parse_color("k") is not None
        assert _parse_color("w") is not None

    def test_named_extended(self):
        assert _parse_color("orange") is not None
        assert _parse_color("purple") is not None
        assert _parse_color("pink") is not None
        assert _parse_color("gray") is not None
        assert _parse_color("grey") is not None

    def test_case_insensitive(self):
        assert _parse_color("RED") == _parse_color("red")
        assert _parse_color("Blue") == _parse_color("blue")

    def test_hex_6(self):
        r, g, b, a = _parse_color("#FF0000")
        assert r == 1.0
        assert g == 0.0
        assert b == 0.0
        assert a == 1.0

    def test_hex_8(self):
        r, g, b, a = _parse_color("#FF000080")
        assert r == 1.0
        assert g == 0.0
        assert b == 0.0
        assert abs(a - 128 / 255.0) < 0.01

    def test_tuple_rgb(self):
        assert _parse_color((0.5, 0.6, 0.7)) == (0.5, 0.6, 0.7, 1.0)

    def test_tuple_rgba(self):
        assert _parse_color((0.1, 0.2, 0.3, 0.4)) == (0.1, 0.2, 0.3, 0.4)

    def test_list_rgb(self):
        assert _parse_color([1.0, 0.0, 0.0]) == (1.0, 0.0, 0.0, 1.0)

    def test_unknown_name(self):
        assert _parse_color("notacolor") is None

    def test_empty_string(self):
        assert _parse_color("") is None

    def test_bad_hex(self):
        assert _parse_color("#ZZ") is None


# ─── _parse_xy_args ──────────────────────────────────────────────────────────

class TestParseXYArgs:
    def test_empty(self):
        x, y = _parse_xy_args(())
        assert x == []
        assert y == []

    def test_y_only(self):
        x, y = _parse_xy_args(([10, 20, 30],))
        assert x == [0.0, 1.0, 2.0]
        assert y == [10.0, 20.0, 30.0]

    def test_x_and_y(self):
        x, y = _parse_xy_args(([1, 2, 3], [4, 5, 6]))
        assert x == [1.0, 2.0, 3.0]
        assert y == [4.0, 5.0, 6.0]

    def test_single_value_y(self):
        x, y = _parse_xy_args(([42],))
        assert x == [0.0]
        assert y == [42.0]

    def test_numpy_y_only(self):
        try:
            import numpy as np
            x, y = _parse_xy_args((np.array([1.0, 2.0, 3.0]),))
            assert x == [0.0, 1.0, 2.0]
            assert y == [1.0, 2.0, 3.0]
        except ImportError:
            pytest.skip("numpy not installed")


# ─── _EasyState ──────────────────────────────────────────────────────────────

class TestEasyState:
    def test_initial_state(self):
        state = _EasyState()
        assert state._session is None
        assert state._current_fig is None
        assert state._current_axes is None
        assert state._figures == []
        assert state._live_threads == []
        assert state._shutting_down is False

    def test_shutdown_without_session(self):
        state = _EasyState()
        state.shutdown()  # should not raise
        assert state._shutting_down is True
        assert state._session is None

    def test_double_shutdown(self):
        state = _EasyState()
        state.shutdown()
        state.shutdown()  # should not raise


# ─── API Surface ─────────────────────────────────────────────────────────────

class TestAPISurface:
    """Verify all expected functions are importable."""

    def test_plot_functions(self):
        from spectra import plot, scatter, stem, hist, bar, hline, vline
        assert callable(plot)
        assert callable(scatter)
        assert callable(stem)
        assert callable(hist)
        assert callable(bar)
        assert callable(hline)
        assert callable(vline)

    def test_3d_functions(self):
        from spectra import plot3, scatter3, surf
        assert callable(plot3)
        assert callable(scatter3)
        assert callable(surf)

    def test_axes_config(self):
        from spectra import title, xlabel, ylabel, xlim, ylim, grid, legend
        assert callable(title)
        assert callable(xlabel)
        assert callable(ylabel)
        assert callable(xlim)
        assert callable(ylim)
        assert callable(grid)
        assert callable(legend)

    def test_figure_management(self):
        from spectra import figure, subplot, gcf, gca, subplots
        assert callable(figure)
        assert callable(subplot)
        assert callable(gcf)
        assert callable(gca)
        assert callable(subplots)

    def test_live_functions(self):
        from spectra import live, stop_live, append
        assert callable(live)
        assert callable(stop_live)
        assert callable(append)

    def test_lifecycle(self):
        from spectra import show, close, clear
        assert callable(show)
        assert callable(close)
        assert callable(clear)

    def test_plotn(self):
        from spectra import plotn
        assert callable(plotn)

    def test_line_alias(self):
        from spectra import line, plot
        assert line is plot

    def test_all_in_all(self):
        import spectra
        expected = [
            "plot", "scatter", "stem", "hist", "bar", "hline", "vline",
            "plot3", "scatter3", "surf", "plotn", "subplots",
            "figure", "subplot", "gcf", "gca",
            "title", "xlabel", "ylabel", "xlim", "ylim", "grid", "legend",
            "live", "stop_live", "append", "show", "close", "clear",
            "line",
            "Session", "Figure", "Axes", "Series",
            "SpectraError", "ConnectionError", "ProtocolError",
            "TimeoutError", "FigureNotFoundError", "BackendError",
            "ipc_sleep", "FramePacer", "BackendAnimator",
        ]
        for name in expected:
            assert name in spectra.__all__, f"{name} not in __all__"


# ─── Backward Compatibility ──────────────────────────────────────────────────

class TestBackwardCompat:
    """Ensure old API patterns still work."""

    def test_default_session_attr(self):
        import spectra as sp
        assert hasattr(sp, "_default_session")

    def test_current_figure_attr(self):
        import spectra as sp
        assert hasattr(sp, "_current_figure")

    def test_current_axes_attr(self):
        import spectra as sp
        assert hasattr(sp, "_current_axes")

    def test_line_is_plot(self):
        import spectra as sp
        assert sp.line is sp.plot

    def test_close_without_session(self):
        """close() should not raise even if no session exists."""
        import spectra as sp
        sp._default_session = None
        sp._current_figure = None
        sp._current_axes = None
        sp.close()  # should not raise

    def test_show_without_session(self):
        """show() should not raise even if no session exists."""
        from spectra._easy import _state
        old_session = _state._session
        _state._session = None
        try:
            from spectra import show
            show()  # should not raise
        finally:
            _state._session = old_session


# ─── Edge Cases ──────────────────────────────────────────────────────────────

class TestEdgeCases:
    def test_parse_xy_large(self):
        data = list(range(10000))
        x, y = _parse_xy_args((data,))
        assert len(x) == 10000
        assert len(y) == 10000

    def test_parse_color_whitespace(self):
        assert _parse_color("  red  ") is not None

    def test_parse_color_hex_lowercase(self):
        r, g, b, a = _parse_color("#ff0000")
        assert r == 1.0

    def test_to_list_mixed_types(self):
        result = _to_list([1, 2.5, True, 0])
        assert result == [1.0, 2.5, 1.0, 0.0]
