"""Colour specifications of the easy API and spectra.pyplot (pure Python, no backend)."""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra import _colors  # noqa: E402
from spectra._easy import _apply_color, _check_color, _parse_color  # noqa: E402
from spectra.pyplot import _parse_fmt  # noqa: E402


class _Series:
    def __init__(self, palette=True):
        self.calls = []
        self.set_color_letter = lambda letter: self.calls.append(("letter", letter))
        if palette:
            self.set_palette_index = lambda i: self.calls.append(("palette", i))

    def set_color(self, r, g, b, a=1.0):
        self.calls.append(("rgb", (r, g, b, a)))


def _close(a, b):
    return all(abs(x - y) < 1e-6 for x, y in zip(a, b))


class TestUnknownColours:
    def test_unknown_name_raises_with_suggestion(self):
        with pytest.raises(ValueError, match=r"did you mean 'steelblue'"):
            _parse_color("steelblu")

    def test_typo_of_tab_colour_suggests_it(self):
        with pytest.raises(ValueError, match=r"tab:orange"):
            _parse_color("tab:ornage")

    def test_garbage_has_no_suggestion_but_still_raises(self):
        with pytest.raises(ValueError, match=r"unknown colour 'qqqqqqqq'$"):
            _parse_color("qqqqqqqq")

    def test_bad_hex_lengths_raise(self):
        for bad in ("#12", "#12345", "#1234567", "#gggggg"):
            with pytest.raises(ValueError):
                _parse_color(bad)

    def test_bad_tuple_raises(self):
        with pytest.raises(ValueError):
            _parse_color((1.0, 0.0))
        with pytest.raises(ValueError):
            _parse_color(("a", "b", "c"))

    def test_grey_level_out_of_range_raises(self):
        with pytest.raises(ValueError):
            _parse_color("1.5")

    def test_check_color_raises_before_anything_is_created(self):
        with pytest.raises(ValueError):
            _check_color("blu")
        _check_color("k")
        _check_color(None)
        _check_color("C3")

    def test_apply_color_raises_and_sends_nothing(self):
        s = _Series()
        with pytest.raises(ValueError):
            _apply_color(s, "notacolour")
        assert s.calls == []


class TestAcceptedColours:
    def test_hex_forms(self):
        assert _parse_color("#f00") == (1.0, 0.0, 0.0, 1.0)
        assert _close(_parse_color("#f008"), (1.0, 0.0, 0.0, 0x88 / 255.0))
        assert _parse_color("#00FF00") == (0.0, 1.0, 0.0, 1.0)
        assert _close(_parse_color("#0000ff80"), (0.0, 0.0, 1.0, 0x80 / 255.0))

    def test_css4_names(self):
        assert len(_colors.CSS4_COLORS) == 148
        assert _close(_parse_color("steelblue"), (0x46 / 255, 0x82 / 255, 0xB4 / 255, 1.0))
        assert _close(_parse_color("RebeccaPurple"), (0x66 / 255, 0x33 / 255, 0x99 / 255, 1.0))

    def test_existing_spectra_names_keep_their_shades(self):
        assert _parse_color("orange") == (1.0, 0.5, 0.0, 1.0)
        assert _parse_color("gray") == (0.5, 0.5, 0.5, 1.0)

    def test_tab_colours(self):
        assert _close(_parse_color("tab:blue"), (0x1F / 255, 0x77 / 255, 0xB4 / 255, 1.0))
        assert _parse_color("tab:grey") == _parse_color("tab:gray")
        assert len({_parse_color(n) for n in _colors.TABLEAU_COLORS}) == 10

    def test_palette_slots_parse_to_default_cycle(self):
        assert _close(_parse_color("C0"), (0x22 / 255, 0xD3 / 255, 0xEE / 255, 1.0))
        assert _parse_color("C10") == _parse_color("C0")

    def test_grey_level_and_none(self):
        assert _parse_color("0.25") == (0.25, 0.25, 0.25, 1.0)
        assert _parse_color("none") == (0.0, 0.0, 0.0, 0.0)

    def test_numpy_like_sequences(self):
        class Vec:
            def __iter__(self):
                return iter([0.1, 0.2, 0.3])

        assert _parse_color(Vec()) == (0.1, 0.2, 0.3, 1.0)


class TestPaletteIndex:
    def test_cn_is_sent_as_palette_slot(self):
        s = _Series()
        _apply_color(s, "C3")
        assert s.calls == [("palette", 3)]

    def test_cn_falls_back_to_rgb_without_palette_support(self):
        s = _Series(palette=False)
        _apply_color(s, "C1")
        assert s.calls == [("rgb", _parse_color("C1"))]

    def test_palette_index_helper(self):
        assert _colors.palette_index("C9") == 9
        assert _colors.palette_index(" C12 ") == 12
        assert _colors.palette_index("c") is None
        assert _colors.palette_index("Cx") is None
        assert _colors.palette_index((1, 2, 3)) is None


class TestPyplotFormat:
    def test_cn_in_format_string(self):
        assert _parse_fmt("C1--") == ("C1", None, "--")
        assert _parse_fmt("C10o") == ("C10", "o", None)
        assert _parse_fmt("r--") == ("r", None, "--")

    def test_pyplot_colour_keyword_validates(self):
        import spectra.pyplot as plt

        with pytest.raises(ValueError, match="did you mean"):
            plt.plot([1, 2, 3], color="bleu")
