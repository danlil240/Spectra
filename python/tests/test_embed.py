"""Tests for the EmbedSurface Python ctypes wrapper."""

import pytest
import ctypes

# Probe for libspectra_embed.so at import time WITHOUT raising a module-level
# skip (which would break launch_testing's pytest_pycollect_makemodule hook).
# Use a flag + @pytest.mark.skipif on each test class instead.
try:
    from spectra._embed import (
        _load_lib,
        EmbedSurface,
        EmbedFigure,
        EmbedAxes,
        EmbedSeries,
        MOUSE_LEFT,
        MOUSE_RIGHT,
        ACTION_PRESS,
        ACTION_RELEASE,
        MOD_SHIFT,
        MOD_CONTROL,
        KEY_R,
        KEY_ESCAPE,
    )
    _load_lib()  # probe load (not just path lookup) so missing deps skip cleanly
    _EMBED_AVAILABLE = True
except (ImportError, FileNotFoundError, OSError):
    _EMBED_AVAILABLE = False

_skip_embed = pytest.mark.skipif(
    not _EMBED_AVAILABLE,
    reason="libspectra_embed.so not found — build with -DSPECTRA_BUILD_EMBED_SHARED=ON",
)


# ─── Construction ────────────────────────────────────────────────────────────


@_skip_embed
class TestConstruction:
    def test_default(self):
        s = EmbedSurface(800, 600)
        assert s.is_valid
        assert s.width == 800
        assert s.height == 600

    def test_small(self):
        s = EmbedSurface(64, 64)
        assert s.is_valid
        assert s.width == 64
        assert s.height == 64

    def test_cleanup(self):
        """Surface cleans up without crash on __del__."""
        s = EmbedSurface(64, 64)
        assert s.is_valid
        del s  # should not raise


# ─── Figure Management ───────────────────────────────────────────────────────


@_skip_embed
class TestFigure:
    def test_create_figure(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        assert isinstance(fig, EmbedFigure)

    def test_subplot(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        assert isinstance(ax, EmbedAxes)

    def test_subplot3d(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot3d(1, 1, 1)
        assert isinstance(ax, EmbedAxes)


# ─── Series ──────────────────────────────────────────────────────────────────


@_skip_embed
class TestSeries:
    def test_line(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.line([0, 1, 2], [0, 1, 4])
        assert isinstance(series, EmbedSeries)

    def test_line_with_label(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.line([0, 1, 2], [0, 1, 4], label="my data")
        assert isinstance(series, EmbedSeries)

    def test_scatter(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.scatter([0, 1, 2], [0, 1, 4])
        assert isinstance(series, EmbedSeries)

    def test_set_data(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.line([0, 1, 2], [0, 1, 4])
        series.set_x([10, 20, 30])
        series.set_y([100, 200, 300])

    def test_numpy_data(self):
        """Verify numpy arrays work if numpy is available."""
        try:
            import numpy as np
        except ImportError:
            pytest.skip("numpy not installed")

        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        x = np.linspace(0, 10, 100)
        y = np.sin(x)
        series = ax.line(x, y, label="np.sin")
        assert isinstance(series, EmbedSeries)

    def test_length_mismatch(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        with pytest.raises(AssertionError):
            ax.line([0, 1, 2], [0, 1])


# ─── Rendering ───────────────────────────────────────────────────────────────


@_skip_embed
class TestRendering:
    def test_render_returns_bytes(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        fig.subplot(1, 1, 1)
        pixels = s.render()
        assert isinstance(pixels, bytes)
        assert len(pixels) == 64 * 64 * 4

    def test_render_with_data(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0, 1, 2, 3], [0, 1, 4, 9])
        pixels = s.render()
        nonzero = sum(1 for b in pixels if b != 0)
        assert nonzero > 100

    def test_render_into(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        fig.subplot(1, 1, 1)
        buf = (ctypes.c_uint8 * (64 * 64 * 4))()
        ok = s.render_into(buf)
        assert ok

    def test_multiple_renders(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        fig.subplot(1, 1, 1)
        for _ in range(5):
            pixels = s.render()
            assert len(pixels) == 64 * 64 * 4


# ─── Resize ──────────────────────────────────────────────────────────────────


@_skip_embed
class TestResize:
    def test_resize(self):
        s = EmbedSurface(100, 100)
        fig = s.figure()
        fig.subplot(1, 1, 1)
        ok = s.resize(200, 150)
        assert ok
        assert s.width == 200
        assert s.height == 150

    def test_render_after_resize(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0, 1, 2], [0, 1, 4])
        s.render()
        s.resize(128, 96)
        pixels = s.render()
        assert len(pixels) == 128 * 96 * 4


# ─── Input Forwarding ───────────────────────────────────────────────────────


@_skip_embed
class TestInput:
    def test_mouse_move(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)
        s.mouse_move(64.0, 64.0)  # should not crash

    def test_mouse_button(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)
        s.mouse_button(MOUSE_LEFT, ACTION_PRESS, 0, 64.0, 64.0)
        s.mouse_button(MOUSE_LEFT, ACTION_RELEASE, 0, 64.0, 64.0)

    def test_scroll(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)
        s.scroll(0.0, 1.0, 64.0, 64.0)
        s.scroll(0.0, -1.0, 64.0, 64.0)

    def test_key(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)
        s.key(KEY_R, ACTION_PRESS, 0)
        s.key(KEY_R, ACTION_RELEASE, 0)

    def test_update(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)
        s.update(0.016)
        s.update(0.016)

    def test_pan_workflow(self):
        """Simulate a full pan interaction."""
        s = EmbedSurface(200, 200)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0, 1, 2, 3], [0, 1, 4, 9])
        s.render()

        # Press, drag, release
        s.mouse_button(MOUSE_LEFT, ACTION_PRESS, 0, 100.0, 100.0)
        s.mouse_move(120.0, 110.0)
        s.mouse_move(140.0, 120.0)
        s.mouse_button(MOUSE_LEFT, ACTION_RELEASE, 0, 140.0, 120.0)
        s.render()  # should render with panned view

    def test_zoom_workflow(self):
        """Simulate scroll-to-zoom."""
        s = EmbedSurface(200, 200)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0, 1, 2, 3, 4, 5], [0, 1, 4, 9, 16, 25])
        s.render()

        # Zoom in
        for _ in range(3):
            s.scroll(0.0, 1.0, 100.0, 100.0)
        s.render()

        # Zoom out
        for _ in range(3):
            s.scroll(0.0, -1.0, 100.0, 100.0)
        s.render()


# ─── Extended Construction ───────────────────────────────────────────────────


@_skip_embed
class TestExtendedConstruction:
    def test_with_theme(self):
        s = EmbedSurface(64, 64, theme="dark")
        assert s.is_valid

    def test_with_theme_light(self):
        s = EmbedSurface(64, 64, theme="light")
        assert s.is_valid

    def test_with_dpi_scale(self):
        s = EmbedSurface(64, 64, dpi_scale=2.0)
        assert s.is_valid

    def test_with_background_alpha(self):
        s = EmbedSurface(64, 64, background_alpha=0.5)
        assert s.is_valid

    def test_background_alpha_property_get_set(self):
        s = EmbedSurface(64, 64)
        s.background_alpha = 0.25
        assert abs(s.background_alpha - 0.25) < 1e-5

    def test_background_alpha_init_preserved(self):
        s = EmbedSurface(64, 64, background_alpha=0.75)
        assert abs(s.background_alpha - 0.75) < 1e-5


# ─── New Series Types ────────────────────────────────────────────────────────


@_skip_embed
class TestNewSeriesTypes:
    def test_histogram(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.histogram([1, 2, 2, 3, 3, 3, 4, 4, 5], bins=5)
        assert isinstance(series, EmbedSeries)

    def test_histogram_with_label(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.histogram([1, 2, 3, 4, 5], bins=3, label="dist")
        assert isinstance(series, EmbedSeries)

    def test_bar(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.bar([1, 2, 3, 4], [10, 20, 15, 25])
        assert isinstance(series, EmbedSeries)

    def test_bar_with_label(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        series = ax.bar([1, 2, 3], [5, 10, 7], label="values")
        assert isinstance(series, EmbedSeries)

    def test_bar_length_mismatch(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        with pytest.raises(AssertionError):
            ax.bar([1, 2, 3], [10, 20])

    def test_auto_fit(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0, 1, 2], [0, 1, 4])
        ax.auto_fit()  # must not crash

    def test_render_after_histogram(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.histogram(list(range(20)), bins=5)
        pixels = s.render()
        assert len(pixels) == 64 * 64 * 4

    def test_render_after_bar(self):
        s = EmbedSurface(64, 64)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.bar([1, 2, 3], [10, 20, 15])
        pixels = s.render()
        assert len(pixels) == 64 * 64 * 4


# ─── Callback APIs ─────────────────────────────────────────────────────────────


@_skip_embed
class TestCallbacks:
    def test_set_on_frame_and_clear(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)

        calls = []

        def on_frame(surface, time_sec, dt_sec):
            calls.append((surface, time_sec, dt_sec))

        s.set_on_frame(on_frame)
        s.update(0.016)
        assert len(calls) >= 1
        assert calls[0][0] is s

        s.clear_on_frame()
        prev = len(calls)
        s.update(0.016)
        assert len(calls) == prev

    def test_set_redraw_callback_accepts_none(self):
        s = EmbedSurface(128, 128)
        s.figure().subplot(1, 1, 1)

        s.set_redraw_callback(lambda: None)
        s.set_redraw_callback(None)  # must not raise

    def test_interactive_callback_setters_accept_none(self):
        s = EmbedSurface(128, 128)
        fig = s.figure()
        ax = fig.subplot(1, 1, 1)
        ax.line([0, 1, 2], [0, 1, 4])
        s.render()

        s.set_on_point_selected(lambda ai, si, pi, x, y: None)
        s.set_on_series_selected(lambda ai, si: None)
        s.set_on_hover(lambda ai, si, pi, x, y: None)
        s.set_on_view_changed(lambda xmin, xmax, ymin, ymax: None)

        # Clear paths route through typed null callbacks and previously regressed.
        s.set_on_point_selected(None)
        s.set_on_series_selected(None)
        s.set_on_hover(None)
        s.set_on_view_changed(None)
