"""Plot style presets (matplotlib-style ``style.use`` / ``style.context``).

A style bundles the look of a figure: background, palette, grid and minor
grid, minor ticks, spines, tick length, font sizes and default line width.
The backend owns the definitions (``spectra::StyleSheet``); this module picks
the style that new figures get::

    import spectra as sp

    sp.style.use("paper")              # every figure created from now on
    sp.plot(x, y)

    with sp.style.context("talk"):     # only figures created in this block
        sp.figure()
        sp.plot(x, y)

    fig.apply_style("minimal")         # restyle one existing figure

Built-in styles: ``"default"``, ``"paper"``, ``"talk"``, ``"minimal"`` and
``"dark-paper"`` (see :data:`available`).
"""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator, List, Optional

#: Names of the built-in styles (``spectra::style::available()``).
available: List[str] = ["default", "paper", "talk", "minimal", "dark-paper"]

_current: Optional[str] = None


def _check(name: str) -> str:
    if not isinstance(name, str) or name not in available:
        raise ValueError(
            f"unknown style {name!r}; available styles: {', '.join(available)}"
        )
    return name


def use(name: str) -> None:
    """Use style ``name`` for every figure created from now on.

    Existing figures keep their look (restyle one with ``Figure.apply_style``).
    ``use("default")`` returns to the built-in look.
    """
    global _current
    _current = None if _check(name) == "default" else name


@contextmanager
def context(name: str) -> Iterator[None]:
    """Use style ``name`` for figures created inside the ``with`` block."""
    global _current
    _check(name)
    previous = _current
    use(name)
    try:
        yield
    finally:
        _current = previous


def current() -> Optional[str]:
    """The style new figures get, or ``None`` for the built-in look."""
    return _current


__all__ = ["available", "use", "context", "current"]
