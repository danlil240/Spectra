"""Inline display for Jupyter: figures render as PNG images under the cell.

Inline mode is used automatically inside a Jupyter kernel (IPython's
``ZMQInteractiveShell``: JupyterLab, Notebook, VS Code, Colab).  In that mode

* figures never open a window; the backend renders them offscreen through the
  same path as ``savefig()``,
* every figure created or drawn into by a cell is shown under it when the cell
  finishes (an IPython ``post_run_cell`` hook, like matplotlib's inline
  backend), and the next plot call starts a new figure,
* ``show()`` displays the pending figures right away and never blocks,
* a ``Figure`` (and the pyplot wrapper) shows as an image when it is the last
  expression of a cell (``_repr_png_``).

Switch with :func:`use_window` (native windows, interactive), back with
:func:`use_inline`, or set ``SPECTRA_DISPLAY=window`` / ``inline`` before the
first plot.  The interactive notebook canvas stays available through
``spectra.notebook`` / ``EmbedSurface.widget()``.
"""

from __future__ import annotations

import os
import sys
import tempfile
from typing import Any, List, Optional

from ._log import log

# Pixel size of a default-sized figure rendered inline (figures created with an
# explicit size, e.g. pyplot figsize, keep theirs).  Smaller than the default
# window so plot text stays readable when the notebook scales the image down.
DEFAULT_INLINE_WIDTH = 960
DEFAULT_INLINE_HEIGHT = 576
DEFAULT_INLINE_THEME = "light"


class _InlineSettings:
    """Inline display mode and options (per Python process)."""

    def __init__(self) -> None:
        self.mode: Optional[str] = None  # "inline" | "window" | None = auto
        self.width = DEFAULT_INLINE_WIDTH
        self.height = DEFAULT_INLINE_HEIGHT
        self.theme = DEFAULT_INLINE_THEME
        self.hook_shell: Any = None  # IPython shell the hook is registered on
        self.pending: List[Any] = []  # figures to show after the current cell
        self.warned_render = False


_settings = _InlineSettings()


def _ipython_shell() -> Any:
    """The running IPython shell, or None (IPython is never imported here)."""
    mod = sys.modules.get("IPython")
    if mod is None:
        return None
    try:
        return mod.get_ipython()
    except Exception:
        return None


def in_jupyter_kernel() -> bool:
    """True inside a Jupyter kernel (ZMQInteractiveShell or a subclass, e.g. Colab)."""
    shell = _ipython_shell()
    if shell is None:
        return False
    return any(c.__name__ == "ZMQInteractiveShell" for c in type(shell).__mro__)


def _env_mode() -> Optional[str]:
    raw = os.environ.get("SPECTRA_DISPLAY", "").strip().lower()
    if raw in ("inline", "png"):
        return "inline"
    if raw in ("window", "windows", "native"):
        return "window"
    return None


def is_inline() -> bool:
    """True when figures are shown inline (as PNG) instead of in windows."""
    mode = _settings.mode or _env_mode()
    inline = in_jupyter_kernel() if mode is None else mode == "inline"
    if inline:
        _register_hook()
    return inline


def use_inline(width: Optional[int] = None, height: Optional[int] = None,
               theme: Optional[str] = None) -> None:
    """Show figures as PNG images in the notebook (the default in Jupyter).

    ``width`` / ``height`` set the pixel size of default-sized figures
    (960 x 576); ``theme`` is the export theme ("light" by default, "dark"
    or any registered theme name; "" keeps the saved one).  Outside IPython,
    figures are still not opened in windows; call :func:`spectra.show` or
    display a figure to render it.
    """
    _settings.mode = "inline"
    if width is not None:
        _settings.width = max(1, int(width))
    if height is not None:
        _settings.height = max(1, int(height))
    if theme is not None:
        _settings.theme = str(theme)
    _register_hook()


def use_window() -> None:
    """Show figures in native Spectra windows (the default outside Jupyter).

    Figures created while inline mode was on open in a window on their next
    plot call or on :func:`spectra.show`.
    """
    _settings.mode = "window"
    _settings.pending.clear()
    _unregister_hook()


def display_mode() -> str:
    """"inline" or "window": how figures are currently shown."""
    return "inline" if is_inline() else "window"


# ─── IPython hook ────────────────────────────────────────────────────────────

def _register_hook() -> None:
    shell = _ipython_shell()
    if shell is None or _settings.hook_shell is shell:
        return
    try:
        shell.events.register("post_run_cell", _post_run_cell)
        _settings.hook_shell = shell
    except Exception as exc:  # pragma: no cover - exotic shells
        log.debug("could not register post_run_cell hook: %s", exc)


def _unregister_hook() -> None:
    shell = _settings.hook_shell
    if shell is None:
        return
    try:
        shell.events.unregister("post_run_cell", _post_run_cell)
    except (ValueError, Exception):
        pass
    _settings.hook_shell = None


def _post_run_cell(*_args: Any) -> None:
    """After each cell: show the figures it drew into, then start fresh."""
    if _settings.mode == "window" or not _settings.pending:
        return
    flush()


def track(fig: Any) -> None:
    """Remember ``fig`` for display after the current cell (inline mode)."""
    if fig is not None and not any(f is fig for f in _settings.pending):
        _settings.pending.append(fig)


def forget(fig: Any) -> None:
    """``fig`` was shown (or closed): do not show it again after this cell."""
    _settings.pending[:] = [f for f in _settings.pending if f is not fig]


def flush() -> None:
    """Display every pending figure now and detach them from the easy API.

    Like matplotlib's inline backend, the next plot call then starts a new
    figure; figure handles stay usable (display them again with
    ``display(fig)``).
    """
    figs = list(_settings.pending)
    _settings.pending.clear()
    if not figs:
        return
    shell = _ipython_shell()
    if shell is not None:
        try:
            from IPython.display import display

            for fig in figs:
                display(fig)
        except Exception as exc:
            log.warning("inline display failed: %s", exc)
    from . import _easy

    _easy._detach_current_figure()


# ─── PNG rendering ───────────────────────────────────────────────────────────

def render_png(fig: Any) -> Optional[bytes]:
    """PNG bytes of ``fig`` rendered offscreen by the backend, or None on failure."""
    from ._protocol import DEFAULT_WINDOW_HEIGHT, DEFAULT_WINDOW_WIDTH

    forget(fig)
    size = getattr(fig, "_size", None)
    if size is None or tuple(size) == (DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT):
        width, height = _settings.width, _settings.height
    else:
        width, height = int(size[0]), int(size[1])
    fd, path = tempfile.mkstemp(prefix="spectra-inline-", suffix=".png")
    os.close(fd)
    try:
        from . import _easy

        _easy._state._flush_pending_knobs()
        fig.savefig(path, width=width, height=height, format="png", theme=_settings.theme)
        with open(path, "rb") as f:
            data = f.read()
        return data or None
    except Exception as exc:
        if not _settings.warned_render:
            _settings.warned_render = True
            import warnings

            warnings.warn(
                f"spectra: could not render the figure inline ({exc}); "
                "showing its text form instead",
                RuntimeWarning,
                stacklevel=2,
            )
        return None
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
