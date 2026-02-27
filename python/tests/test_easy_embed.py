"""Tests for the spectra.embed easy rendering API."""

import pytest
import os
import tempfile


class TestImage:
    """Test the Image container class."""

    def test_image_attributes(self):
        from spectra.embed import Image
        img = Image(b"\x00" * (10 * 10 * 4), 10, 10)
        assert img.width == 10
        assert img.height == 10
        assert img.stride == 40
        assert img.size_bytes == 400
        assert len(img) == 400
        assert bool(img) is True

    def test_image_empty(self):
        from spectra.embed import Image
        img = Image(b"", 0, 0)
        assert not img
        assert len(img) == 0


class TestRender:
    """Test the render() function."""

    def test_basic_line(self):
        from spectra.embed import render
        img = render([0, 1, 2, 3, 4], [0, 1, 4, 9, 16])
        assert img
        assert img.width == 800
        assert img.height == 600
        assert img.size_bytes == 800 * 600 * 4

    def test_custom_size(self):
        from spectra.embed import render
        img = render([0, 1, 2], [0, 1, 4], width=400, height=300)
        assert img.width == 400
        assert img.height == 300

    def test_non_blank_pixels(self):
        from spectra.embed import render
        img = render([0, 1, 2, 3, 4], [0, 1, 4, 9, 16])
        nonzero = sum(1 for b in img.data if b != 0)
        assert nonzero > 100, "Rendered image should not be blank"

    def test_save_png(self):
        from spectra.embed import render
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        try:
            img = render([0, 1, 2, 3], [0, 1, 4, 9], save=path)
            assert img
            assert os.path.exists(path)
            assert os.path.getsize(path) > 0
        finally:
            if os.path.exists(path):
                os.unlink(path)

    def test_numpy_data(self):
        try:
            import numpy as np
        except ImportError:
            pytest.skip("numpy not installed")
        from spectra.embed import render
        x = np.linspace(0, 10, 100)
        y = np.sin(x)
        img = render(x, y)
        assert img
        assert img.width == 800

    def test_multiple_renders(self):
        from spectra.embed import render
        for _ in range(3):
            img = render([0, 1, 2], [0, 1, 4])
            assert img


class TestScatter:
    """Test the scatter() function."""

    def test_basic_scatter(self):
        from spectra.embed import scatter
        img = scatter([0, 1, 2, 3], [3, 1, 4, 2])
        assert img
        assert img.width == 800
        assert img.height == 600

    def test_scatter_save(self):
        from spectra.embed import scatter
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        try:
            img = scatter([0, 1, 2, 3], [3, 1, 4, 2], save=path)
            assert img
            assert os.path.exists(path)
        finally:
            if os.path.exists(path):
                os.unlink(path)


class TestRenderMulti:
    """Test the render_multi() function."""

    def test_two_series(self):
        from spectra.embed import render_multi
        img = render_multi([
            ([0, 1, 2, 3], [0, 1, 4, 9], "quadratic"),
            ([0, 1, 2, 3], [0, 1, 2, 3], "linear"),
        ])
        assert img

    def test_no_labels(self):
        from spectra.embed import render_multi
        img = render_multi([
            ([0, 1, 2], [0, 1, 4]),
            ([0, 1, 2], [4, 1, 0]),
        ])
        assert img


class TestWithOptions:
    """Test rendering with title/xlabel/ylabel options."""

    def test_with_title(self):
        from spectra.embed import render
        img = render([0, 1, 2, 3], [0, 1, 4, 9], title="Test Plot")
        assert img

    def test_with_labels(self):
        from spectra.embed import render
        img = render([0, 1, 2], [0, 1, 4],
                     title="My Plot", xlabel="X", ylabel="Y")
        assert img


class TestImageSave:
    """Test Image.save() method."""

    def test_save_method(self):
        from spectra.embed import render
        img = render([0, 1, 2, 3], [0, 1, 4, 9])
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        try:
            ok = img.save(path)
            assert ok
            assert os.path.exists(path)
            assert os.path.getsize(path) > 0
        finally:
            if os.path.exists(path):
                os.unlink(path)
