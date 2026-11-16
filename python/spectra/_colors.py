"""Colour specifications accepted by the easy API and spectra.pyplot.

Accepted forms (case-insensitive, surrounding spaces ignored):

* MATLAB letters ``r g b c m y k w`` and their names (theme-aware, see _easy).
* ``"C0"`` .. ``"C9"`` (any ``C<n>``): slot *n* of the active data palette,
  resolved by the window for its theme like the letter colours.
* ``"tab:blue"`` ... ``"tab:cyan"``: the Tableau 10 colours.
* Hex ``#rgb``, ``#rgba``, ``#rrggbb`` and ``#rrggbbaa``.
* CSS4 / X11 colour names (``"steelblue"``, ``"rebeccapurple"``, ...).
* A grey level as a string between ``"0"`` and ``"1"`` (``"0.5"``).
* ``"none"`` (fully transparent).
* RGB / RGBA sequences of floats in 0..1.

Anything else raises ValueError with a "did you mean" suggestion.
"""

from __future__ import annotations

import difflib
import re
from typing import Optional, Tuple

RGBA = Tuple[float, float, float, float]

# Spectra's default data palette (palette::default_cycle in spectra/color.hpp).
# "C<n>" sent to a series stays a palette slot; this RGB is only used where a
# fixed colour is needed (text annotations).
DEFAULT_CYCLE = (
    "#22D3EE", "#FB923C", "#E879F9", "#A3E635", "#60A5FA",
    "#FB7185", "#FACC15", "#34D399", "#C084FC", "#E5E7EB",
)

TABLEAU_COLORS = {
    "tab:blue": "#1f77b4",
    "tab:orange": "#ff7f0e",
    "tab:green": "#2ca02c",
    "tab:red": "#d62728",
    "tab:purple": "#9467bd",
    "tab:brown": "#8c564b",
    "tab:pink": "#e377c2",
    "tab:gray": "#7f7f7f",
    "tab:grey": "#7f7f7f",
    "tab:olive": "#bcbd22",
    "tab:cyan": "#17becf",
}

# CSS Color Module Level 4 named colours.
CSS4_COLORS = {
    "aliceblue": "#f0f8ff", "antiquewhite": "#faebd7", "aqua": "#00ffff",
    "aquamarine": "#7fffd4", "azure": "#f0ffff", "beige": "#f5f5dc",
    "bisque": "#ffe4c4", "black": "#000000", "blanchedalmond": "#ffebcd",
    "blue": "#0000ff", "blueviolet": "#8a2be2", "brown": "#a52a2a",
    "burlywood": "#deb887", "cadetblue": "#5f9ea0", "chartreuse": "#7fff00",
    "chocolate": "#d2691e", "coral": "#ff7f50", "cornflowerblue": "#6495ed",
    "cornsilk": "#fff8dc", "crimson": "#dc143c", "cyan": "#00ffff",
    "darkblue": "#00008b", "darkcyan": "#008b8b", "darkgoldenrod": "#b8860b",
    "darkgray": "#a9a9a9", "darkgreen": "#006400", "darkgrey": "#a9a9a9",
    "darkkhaki": "#bdb76b", "darkmagenta": "#8b008b", "darkolivegreen": "#556b2f",
    "darkorange": "#ff8c00", "darkorchid": "#9932cc", "darkred": "#8b0000",
    "darksalmon": "#e9967a", "darkseagreen": "#8fbc8f", "darkslateblue": "#483d8b",
    "darkslategray": "#2f4f4f", "darkslategrey": "#2f4f4f", "darkturquoise": "#00ced1",
    "darkviolet": "#9400d3", "deeppink": "#ff1493", "deepskyblue": "#00bfff",
    "dimgray": "#696969", "dimgrey": "#696969", "dodgerblue": "#1e90ff",
    "firebrick": "#b22222", "floralwhite": "#fffaf0", "forestgreen": "#228b22",
    "fuchsia": "#ff00ff", "gainsboro": "#dcdcdc", "ghostwhite": "#f8f8ff",
    "gold": "#ffd700", "goldenrod": "#daa520", "gray": "#808080",
    "green": "#008000", "greenyellow": "#adff2f", "grey": "#808080",
    "honeydew": "#f0fff0", "hotpink": "#ff69b4", "indianred": "#cd5c5c",
    "indigo": "#4b0082", "ivory": "#fffff0", "khaki": "#f0e68c",
    "lavender": "#e6e6fa", "lavenderblush": "#fff0f5", "lawngreen": "#7cfc00",
    "lemonchiffon": "#fffacd", "lightblue": "#add8e6", "lightcoral": "#f08080",
    "lightcyan": "#e0ffff", "lightgoldenrodyellow": "#fafad2", "lightgray": "#d3d3d3",
    "lightgreen": "#90ee90", "lightgrey": "#d3d3d3", "lightpink": "#ffb6c1",
    "lightsalmon": "#ffa07a", "lightseagreen": "#20b2aa", "lightskyblue": "#87cefa",
    "lightslategray": "#778899", "lightslategrey": "#778899", "lightsteelblue": "#b0c4de",
    "lightyellow": "#ffffe0", "lime": "#00ff00", "limegreen": "#32cd32",
    "linen": "#faf0e6", "magenta": "#ff00ff", "maroon": "#800000",
    "mediumaquamarine": "#66cdaa", "mediumblue": "#0000cd", "mediumorchid": "#ba55d3",
    "mediumpurple": "#9370db", "mediumseagreen": "#3cb371", "mediumslateblue": "#7b68ee",
    "mediumspringgreen": "#00fa9a", "mediumturquoise": "#48d1cc", "mediumvioletred": "#c71585",
    "midnightblue": "#191970", "mintcream": "#f5fffa", "mistyrose": "#ffe4e1",
    "moccasin": "#ffe4b5", "navajowhite": "#ffdead", "navy": "#000080",
    "oldlace": "#fdf5e6", "olive": "#808000", "olivedrab": "#6b8e23",
    "orange": "#ffa500", "orangered": "#ff4500", "orchid": "#da70d6",
    "palegoldenrod": "#eee8aa", "palegreen": "#98fb98", "paleturquoise": "#afeeee",
    "palevioletred": "#db7093", "papayawhip": "#ffefd5", "peachpuff": "#ffdab9",
    "peru": "#cd853f", "pink": "#ffc0cb", "plum": "#dda0dd",
    "powderblue": "#b0e0e6", "purple": "#800080", "rebeccapurple": "#663399",
    "red": "#ff0000", "rosybrown": "#bc8f8f", "royalblue": "#4169e1",
    "saddlebrown": "#8b4513", "salmon": "#fa8072", "sandybrown": "#f4a460",
    "seagreen": "#2e8b57", "seashell": "#fff5ee", "sienna": "#a0522d",
    "silver": "#c0c0c0", "skyblue": "#87ceeb", "slateblue": "#6a5acd",
    "slategray": "#708090", "slategrey": "#708090", "snow": "#fffafa",
    "springgreen": "#00ff7f", "steelblue": "#4682b4", "tan": "#d2b48c",
    "teal": "#008080", "thistle": "#d8bfd8", "tomato": "#ff6347",
    "turquoise": "#40e0d0", "violet": "#ee82ee", "wheat": "#f5deb3",
    "white": "#ffffff", "whitesmoke": "#f5f5f5", "yellow": "#ffff00",
    "yellowgreen": "#9acd32",
}

# Spectra's own tuned shades for the short names it has always accepted.
# They take precedence over CSS4 so existing scripts keep their colours (the
# letter names are sent as theme-aware letters by _easy._apply_color anyway).
SPECTRA_NAMED = {
    "red": (1.0, 0.2, 0.2, 1.0), "r": (1.0, 0.2, 0.2, 1.0),
    "green": (0.2, 0.8, 0.2, 1.0), "g": (0.2, 0.8, 0.2, 1.0),
    "blue": (0.2, 0.4, 1.0, 1.0), "b": (0.2, 0.4, 1.0, 1.0),
    "yellow": (1.0, 0.9, 0.1, 1.0), "y": (1.0, 0.9, 0.1, 1.0),
    "cyan": (0.0, 0.9, 0.9, 1.0), "c": (0.0, 0.9, 0.9, 1.0),
    "magenta": (0.9, 0.1, 0.9, 1.0), "m": (0.9, 0.1, 0.9, 1.0),
    "white": (1.0, 1.0, 1.0, 1.0), "w": (1.0, 1.0, 1.0, 1.0),
    "black": (0.0, 0.0, 0.0, 1.0), "k": (0.0, 0.0, 0.0, 1.0),
    "orange": (1.0, 0.5, 0.0, 1.0),
    "purple": (0.6, 0.2, 0.9, 1.0),
    "pink": (1.0, 0.4, 0.7, 1.0),
    "gray": (0.5, 0.5, 0.5, 1.0), "grey": (0.5, 0.5, 0.5, 1.0),
}

_PALETTE_RE = re.compile(r"^c(\d{1,5})$")
_HEX_RE = re.compile(r"^#([0-9a-f]{3}|[0-9a-f]{4}|[0-9a-f]{6}|[0-9a-f]{8})$")


def palette_index(color) -> Optional[int]:
    """Slot *n* for a ``"C<n>"`` string, else None."""
    if not isinstance(color, str):
        return None
    m = _PALETTE_RE.match(color.strip().lower())
    return int(m.group(1)) if m else None


def hex_to_rgba(text: str) -> Optional[RGBA]:
    """``#rgb``, ``#rgba``, ``#rrggbb`` or ``#rrggbbaa`` -> RGBA floats, else None."""
    m = _HEX_RE.match(text.strip().lower())
    if not m:
        return None
    h = m.group(1)
    if len(h) in (3, 4):
        h = "".join(ch * 2 for ch in h)
    vals = [int(h[i:i + 2], 16) / 255.0 for i in range(0, len(h), 2)]
    if len(vals) == 3:
        vals.append(1.0)
    return (vals[0], vals[1], vals[2], vals[3])


def _all_names():
    names = set(SPECTRA_NAMED) | set(CSS4_COLORS) | set(TABLEAU_COLORS)
    names.update("C%d" % i for i in range(10))
    names.add("none")
    return sorted(names)


def _suggest(name: str) -> str:
    matches = difflib.get_close_matches(name.lower(), [n.lower() for n in _all_names()], n=3,
                                        cutoff=0.6)
    if not matches:
        return ""
    return "; did you mean " + " or ".join(repr(m) for m in matches) + "?"


def to_rgba(color) -> Optional[RGBA]:
    """Parse a colour specification to RGBA floats (None stays None).

    Raises ValueError for an unknown or malformed colour.
    """
    if color is None:
        return None
    if isinstance(color, str):
        text = color.strip()
        lower = text.lower()
        if not lower:
            raise ValueError("empty colour string")
        if lower in SPECTRA_NAMED:
            return SPECTRA_NAMED[lower]
        if lower == "none":
            return (0.0, 0.0, 0.0, 0.0)
        slot = palette_index(lower)
        if slot is not None:
            return hex_to_rgba(DEFAULT_CYCLE[slot % len(DEFAULT_CYCLE)])
        if lower in TABLEAU_COLORS:
            return hex_to_rgba(TABLEAU_COLORS[lower])
        if lower in CSS4_COLORS:
            return hex_to_rgba(CSS4_COLORS[lower])
        if lower.startswith("#"):
            rgba = hex_to_rgba(lower)
            if rgba is None:
                raise ValueError(
                    "invalid hex colour %r: use #rgb, #rgba, #rrggbb or #rrggbbaa" % (color,))
            return rgba
        try:
            level = float(lower)
        except ValueError:
            level = None
        if level is not None:
            if 0.0 <= level <= 1.0:
                return (level, level, level, 1.0)
            raise ValueError("grey level %r must be between 0 and 1" % (color,))
        raise ValueError("unknown colour %r%s" % (color, _suggest(lower)))
    try:
        values = [float(v) for v in color]
    except (TypeError, ValueError):
        raise ValueError("invalid colour %r: expected a name, hex string or RGB(A) tuple"
                         % (color,)) from None
    if len(values) == 3:
        return (values[0], values[1], values[2], 1.0)
    if len(values) == 4:
        return (values[0], values[1], values[2], values[3])
    raise ValueError("colour tuple must have 3 or 4 values, got %d" % len(values))
