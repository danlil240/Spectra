"""MATLAB / matplotlib format strings and line / marker style names.

``parse_fmt("r--o")`` -> ``("r", "o", "--")``: colour letter, marker symbol and
line style token, each ``None`` when the format string does not set it.  The
tokens are the ones the backend understands for the ``line_style`` and
``marker_style`` series properties (src/ipc/series_style_state.hpp).
"""

from __future__ import annotations

from typing import Any, Optional, Tuple

COLOR_LETTERS = "bgrcmykw"

# Spectra line style tokens (spectra::LineStyle symbols) and accepted aliases.
_LINESTYLE_ALIASES = {
    "-": "-",
    "solid": "-",
    "--": "--",
    "dashed": "--",
    ":": ":",
    "dotted": ":",
    "-.": "-.",
    "dashdot": "-.",
    "-..": "-..",
    "dashdotdot": "-..",
    "none": "none",
    "None": "none",
    " ": "none",
    "": "none",
}

# Format-string line styles, longest first so "--" wins over "-".
_FMT_LINESTYLES = ("-..", "--", "-.", "-", ":")

# matplotlib / MATLAB marker -> Spectra marker symbol (spectra::MarkerStyle).
_MARKER_ALIASES = {
    ".": ".",
    ",": ".",       # matplotlib pixel: smallest marker Spectra has
    "o": "o",
    "+": "+",
    "x": "x",
    "X": "x",
    "*": "*",
    "s": "s",
    "d": "d",
    "D": "D",       # filled diamond, like matplotlib
    "^": "^",
    "v": "v",
    "<": "<",
    ">": ">",
    "p": "p",
    "h": "h",
    "H": "h",
    "O": "O",       # Spectra extensions: filled circle / square / triangle
    "S": "S",
    "A": "A",
    "circle": "o",
    "square": "s",
    "diamond": "d",
    "triangle": "^",
    "point": ".",
    "plus": "+",
    "cross": "x",
    "star": "*",
    "pentagon": "p",
    "hexagon": "h",
    "none": "none",
    "None": "none",
    "": "none",
    " ": "none",
}

# Single characters that are markers in a format string (not colours).
_FMT_MARKERS = frozenset(k for k in _MARKER_ALIASES if len(k) == 1 and k.strip())


def normalize_linestyle(value: Any) -> str:
    """Line style name or symbol -> backend token ("-", "--", ":", "-.", "-..", "none")."""
    if value is None:
        return "none"
    if isinstance(value, str):
        token = _LINESTYLE_ALIASES.get(value)
        if token is None:
            token = _LINESTYLE_ALIASES.get(value.strip().lower())
        if token is not None:
            return token
    raise ValueError(
        f"unknown line style {value!r}; use '-', '--', ':', '-.', '-..' or 'none' "
        "(or 'solid', 'dashed', 'dotted', 'dashdot')"
    )


def normalize_marker(value: Any) -> str:
    """Marker name or symbol -> backend token ("o", "s", "^", ..., "none")."""
    if value is None or value is False:
        return "none"
    if isinstance(value, str):
        token = _MARKER_ALIASES.get(value)
        if token is None and len(value) > 1:
            token = _MARKER_ALIASES.get(value.strip().lower())
        if token is not None:
            return token
    supported = " ".join(sorted(k for k in _FMT_MARKERS))
    raise ValueError(f"unknown marker {value!r}; use one of: {supported} or 'none'")


def is_fmt(value: Any) -> bool:
    """True when ``value`` parses as a format string (e.g. "r--o", "b:", "g^")."""
    if not isinstance(value, str):
        return False
    try:
        parse_fmt(value)
    except ValueError:
        return False
    return True


def parse_fmt(fmt: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """``"[color][marker][line]"`` in any order -> (colour letter, marker, line style).

    Like MATLAB and matplotlib: ``"r--o"`` is a red dashed line with circle
    markers, ``"g^"`` green triangles with no line, ``"k-."`` a black
    dash-dot line.  Raises ``ValueError`` for anything else.
    """
    if not isinstance(fmt, str):
        raise ValueError(f"format string must be a str, got {type(fmt).__name__}")
    color = marker = line = None
    rest = fmt.strip()
    if not rest:
        raise ValueError("empty format string")
    while rest:
        for ls in _FMT_LINESTYLES:
            if rest.startswith(ls):
                if line is not None:
                    raise ValueError(f"format string {fmt!r} has two line styles")
                line, rest = ls, rest[len(ls):]
                break
        else:
            ch, rest = rest[0], rest[1:]
            if ch in COLOR_LETTERS and color is None:
                color = ch
            elif ch in _FMT_MARKERS and marker is None:
                marker = _MARKER_ALIASES[ch]
            else:
                raise ValueError(
                    f"unrecognised format string {fmt!r}: expected [colour][marker][line], "
                    "e.g. 'r--', 'bo', 'g^-', 'k-.' (colours: b g r c m y k w)"
                )
    return color, marker, line


def resolve_style(
    fmt: Optional[str],
    linestyle: Any = None,
    marker: Any = None,
) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """Combine a format string with ``linestyle=`` / ``marker=`` keywords.

    Returns (colour letter or None, line style token or None, marker token or
    None); ``None`` means "leave the series default".  Like matplotlib, a
    format string with a marker but no line style draws markers only, and the
    keywords win over the format string.
    """
    color = fmt_marker = fmt_line = None
    if fmt:
        color, fmt_marker, fmt_line = parse_fmt(fmt)
    line_token = normalize_linestyle(linestyle) if linestyle is not None else None
    marker_token = normalize_marker(marker) if marker is not None else None
    if line_token is None and fmt_line is not None:
        line_token = normalize_linestyle(fmt_line)
    if marker_token is None and fmt_marker is not None:
        marker_token = fmt_marker
    if line_token is None and fmt and fmt_marker is not None and fmt_line is None:
        line_token = "none"  # "o" alone: markers only
    return color, line_token, marker_token
