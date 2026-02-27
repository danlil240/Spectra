"""Tests for the EmbedSurface Python ctypes wrapper."""

import pytest
import ctypes

from spectra._embed import (
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


# ─── Construction ────────────────────────────────────────────────────────────


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
