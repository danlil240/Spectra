"""Qt backend for Spectra — embed GPU-accelerated plots in Qt5/Qt6 applications.

Works with PyQt5, PySide2, PyQt6, and PySide6. The correct binding is
auto-detected (or forced via ``QT_API`` env var).

Usage (matplotlib-compatible style)::

    from spectra.backends.backend_qtagg import FigureCanvasSpectra
    from spectra.backends.backend_qtagg import NavigationToolbarSpectra

    # Create a canvas with an embedded Spectra surface
    canvas = FigureCanvasSpectra(width=800, height=600)
    ax = canvas.axes()
    ax.line(x, y, label="data")
    ax.scatter(x2, y2, label="samples")

    # Optional toolbar (zoom, pan, home, save)
    toolbar = NavigationToolbarSpectra(canvas, parent)

    # Or use the all-in-one convenience widget
    from spectra.backends.backend_qtagg import SpectraWidget
    widget = SpectraWidget()
    ax = widget.axes()
    ax.line(x, y)
"""

from __future__ import annotations

import ctypes
import time
from typing import List, Optional, Tuple

from ._qt_compat import (
    QT_API,
    Qt,
    QTimer,
    QSize,
    QPointF,
    Signal,
    Slot,
    QImage,
    QPainter,
    QMouseEvent,
    QWheelEvent,
    QKeyEvent,
    QIcon,
    QAction,
    QCursor,
    QColor,
    QWidget,
    QVBoxLayout,
    QLabel,
    QToolBar,
    QSizePolicy,
    QFileDialog,
    mouse_event_pos,
    wheel_event_pos,
)

from .._embed import (
    EmbedSurface,
    EmbedFigure,
    EmbedAxes,
    MOUSE_LEFT,
    MOUSE_RIGHT,
    MOUSE_MIDDLE,
    ACTION_PRESS,
    ACTION_RELEASE,
    MOD_SHIFT,
    MOD_CONTROL,
    MOD_ALT,
    KEY_R,
    KEY_G,
    KEY_A,
    KEY_S,
    KEY_ESCAPE,
)


# ─── Key mapping ─────────────────────────────────────────────────────────────

def _qt_button(btn) -> int:
    """Convert Qt mouse button to Spectra constant."""
    if btn == Qt.LeftButton:
        return MOUSE_LEFT
    if btn == Qt.RightButton:
        return MOUSE_RIGHT
    if btn == Qt.MiddleButton:
        return MOUSE_MIDDLE
    return 0


def _qt_mods(mods) -> int:
    """Convert Qt modifier flags to Spectra modifier mask."""
    result = 0
    if mods & Qt.ShiftModifier:
        result |= MOD_SHIFT
    if mods & Qt.ControlModifier:
        result |= MOD_CONTROL
    if mods & Qt.AltModifier:
        result |= MOD_ALT
    return result


def _qt_key(qt_key) -> int:
    """Convert Qt key code to Spectra key constant.

    Qt and GLFW share the same codes for A-Z (65-90) and 0-9 (48-57).
    """
    if Qt.Key_A <= qt_key <= Qt.Key_Z:
        return qt_key
    if Qt.Key_0 <= qt_key <= Qt.Key_9:
        return qt_key
    _MAP = {
        Qt.Key_Escape: KEY_ESCAPE,
        Qt.Key_R: KEY_R,
        Qt.Key_G: KEY_G,
        Qt.Key_A: KEY_A,
        Qt.Key_S: KEY_S,
    }
    return _MAP.get(qt_key, 0)


# ─── SpectraTimer (matplotlib-compatible) ────────────────────────────────────

class SpectraTimer:
    """A timer attached to a canvas, compatible with matplotlib's TimerBase.

    Usage (identical to matplotlib)::

        timer = canvas.new_timer(interval)  # interval in ms
        timer.add_callback(func, *args)
        timer.start()

    The timer fires ``interval`` ms after the previous callback finishes.
    Use ``single_shot=True`` for a one-shot timer.
    """

    def __init__(
        self,
        interval: int = 1000,
        parent: Optional[QWidget] = None,
        single_shot: bool = False,
    ) -> None:
        self._interval = max(1, interval)
        self._single_shot = single_shot
        self._callbacks: list = []  # list of (func, args, kwargs)
        self._timer = QTimer(parent)
        self._timer.setSingleShot(single_shot)
        # Use a lambda so Qt sees a plain callable (SpectraTimer is not a
        # QObject, so @Slot() decorated methods can't be connected directly).
        self._timer.timeout.connect(lambda: self._fire())

    @property
    def interval(self) -> int:
        return self._interval

    @interval.setter
    def interval(self, value: int) -> None:
        self._interval = max(1, value)
        if self._timer.isActive():
            self._timer.setInterval(self._interval)

    @property
    def single_shot(self) -> bool:
        return self._single_shot

    @single_shot.setter
    def single_shot(self, value: bool) -> None:
        self._single_shot = value
        self._timer.setSingleShot(value)

    def add_callback(self, func, *args, **kwargs) -> None:
        """Register a callback. Called on every tick."""
        self._callbacks.append((func, args, kwargs))

    def remove_callback(self, func) -> None:
        """Remove a previously registered callback."""
        self._callbacks = [
            (f, a, kw) for f, a, kw in self._callbacks if f is not func
        ]

    def start(self, interval: Optional[int] = None) -> None:
        """Start the timer. Optionally override the interval (ms)."""
        if interval is not None:
            self._interval = max(1, interval)
        self._timer.start(self._interval)

    def stop(self) -> None:
        """Stop the timer."""
        self._timer.stop()

    @property
    def is_active(self) -> bool:
        return self._timer.isActive()

    def _fire(self) -> None:
        for func, args, kwargs in self._callbacks:
            func(*args, **kwargs)


# ─── FigureCanvasSpectra ─────────────────────────────────────────────────────

class FigureCanvasSpectra(QWidget):
    """QWidget that hosts a GPU-accelerated Spectra plot.

    This is the Spectra equivalent of matplotlib's ``FigureCanvasQTAgg``.
    It wraps an ``EmbedSurface`` and provides:

    - Automatic resize handling
    - Mouse/keyboard event forwarding (pan, zoom, etc.)
    - Periodic animation updates
    - Zero-copy rendering into a QImage

    Parameters
    ----------
    width : int
        Initial surface width in pixels (default 800).
    height : int
        Initial surface height in pixels (default 600).
    parent : QWidget | None
        Parent widget.
    fps : int
        Target frames per second for animation timer (default 60).

    Example
    -------
    ::

        canvas = FigureCanvasSpectra(800, 600)
        ax = canvas.axes()
        ax.line([0,1,2,3], [0,1,4,9], label="quadratic")
        canvas.draw()
    """

    #: Emitted after each frame is rendered.
    frame_rendered = Signal()

    #: Emitted when the surface is resized. Args: (width, height).
    surface_resized = Signal(int, int)

    def __init__(
        self,
        width: int = 800,
        height: int = 600,
        parent: Optional[QWidget] = None,
        fps: int = 60,
    ) -> None:
        super().__init__(parent)
        self.setMinimumSize(120, 80)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setAttribute(Qt.WA_OpaquePaintEvent, True)
        self.setAttribute(Qt.WA_NoSystemBackground, True)
        self.setAutoFillBackground(False)
        # Block CSS background inheritance from parent widgets — the canvas
        # paints its own opaque background in paintEvent, so any inherited
        # CSS background causes compositor bleed-through on X11/Wayland.
        self.setStyleSheet("background: transparent;")
        # Spectra dark bg_primary — matches the Vulkan clear color used by
        # Renderer::begin_render_pass() so the Qt widget background is seamless.
        self._bg_color = QColor(0x0D, 0x11, 0x17)

        # DPI scaling: render at physical pixel size for crisp text
        self._dpr = 1.0  # updated in showEvent / resizeEvent
        phys_w = int(width * self._dpr)
        phys_h = int(height * self._dpr)

        # Create the Vulkan offscreen surface at physical resolution
        self._surface = EmbedSurface(phys_w, phys_h)
        self._figure: Optional[EmbedFigure] = None
        self._axes_cache: Optional[EmbedAxes] = None

        # Pixel buffer (zero-copy path)
        buf_size = phys_w * phys_h * 4
        self._pixel_buf = (ctypes.c_uint8 * buf_size)()
        self._qimage: Optional[QImage] = None
        self._dirty = True  # needs re-render

        # View history for navigation (back/forward)
        self._view_history: List[Tuple[float, float, float, float]] = []
        self._view_index: int = -1

        # Navigation state
        self._nav_mode: str = ""  # "", "pan", "zoom"
        self._nav_start: Optional[QPointF] = None

        # Animation timer — this is the ONLY place that triggers rendering.
        # Input events mark _dirty but do NOT render directly (prevents stutter).
        self._fps = max(1, fps)
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._on_tick)
        self._last_tick = time.monotonic()
        # Always start the timer for responsive input — even without animation.
        # At idle (no dirty flag), the tick is a no-op.
        self._timer.start(max(1, int(1000 / self._fps)))

    # ── Public API ────────────────────────────────────────────────────────

    @property
    def surface(self) -> EmbedSurface:
        """Access the underlying ``EmbedSurface``."""
        return self._surface

    def figure(self) -> EmbedFigure:
        """Get or create the figure on this canvas."""
        if self._figure is None:
            self._figure = self._surface.figure()
        return self._figure

    def axes(self, rows: int = 1, cols: int = 1, index: int = 1) -> EmbedAxes:
        """Get or create axes (subplot) on the figure.

        Caches the default (1,1,1) axes; call with different args for subplots.
        """
        fig = self.figure()
        if rows == 1 and cols == 1 and index == 1:
            if self._axes_cache is None:
                self._axes_cache = fig.subplot(1, 1, 1)
            return self._axes_cache
        return fig.subplot(rows, cols, index)

    def axes3d(self, rows: int = 1, cols: int = 1, index: int = 1) -> EmbedAxes:
        """Create a 3D subplot on the figure."""
        return self.figure().subplot3d(rows, cols, index)

    def draw(self) -> None:
        """Request a re-render on the next timer tick."""
        self._dirty = True

    def draw_idle(self) -> None:
        """Request a re-render on the next timer tick (non-blocking).

        This is the safe way to trigger a repaint from a data-update callback.
        Equivalent to matplotlib's ``draw_idle()``.
        """
        self._dirty = True

    def new_timer(self, interval: int = 1000, single_shot: bool = False) -> SpectraTimer:
        """Create a new timer attached to this canvas.

        This is the Spectra equivalent of matplotlib's ``FigureCanvasBase.new_timer()``.

        Parameters
        ----------
        interval : int
            Timer interval in milliseconds (default 1000).
        single_shot : bool
            If True, fires only once (default False — repeating).

        Returns
        -------
        SpectraTimer
            A timer object with ``add_callback()``, ``start()``, ``stop()`` methods.

        Example
        -------
        ::

            timer = canvas.new_timer(20)  # 50 Hz
            timer.add_callback(update_data)
            timer.start()
        """
        return SpectraTimer(interval=interval, parent=self, single_shot=single_shot)

    def start_animation(self, fps: Optional[int] = None) -> None:
        """Start the animation timer at a specific FPS."""
        if fps is not None:
            self._fps = max(1, fps)
        interval = max(1, int(1000 / self._fps))
        self._last_tick = time.monotonic()
        self._timer.start(interval)

    def stop_animation(self) -> None:
        """Stop the animation timer (input still renders via idle timer)."""
        # Restart at a lower idle rate so input events still get painted
        self._timer.start(max(1, int(1000 / 30)))  # 30 FPS idle

    @property
    def is_animating(self) -> bool:
        return self._timer.isActive()

    def _update_dpr(self) -> None:
        """Detect and apply device pixel ratio for HiDPI rendering."""
        screen = self.screen()
        dpr = screen.devicePixelRatio() if screen else 1.0
        if dpr != self._dpr:
            self._dpr = dpr
            self._surface.set_dpi_scale(dpr)

    def save_figure(self, path: str) -> bool:
        """Save the current frame to a PNG file.

        Returns True on success.
        """
        if self._qimage and not self._qimage.isNull():
            return self._qimage.save(path)
        return False

    # ── Navigation modes (used by toolbar) ────────────────────────────────

    def set_nav_mode(self, mode: str) -> None:
        """Set navigation mode: 'pan', 'zoom', or '' (none)."""
        self._nav_mode = mode
        if mode == "pan":
            self.setCursor(QCursor(Qt.OpenHandCursor))
        elif mode == "zoom":
            self.setCursor(QCursor(Qt.CrossCursor))
        else:
            self.setCursor(QCursor(Qt.ArrowCursor))

    def home(self) -> None:
        """Reset view to auto-fit (sends 'A' key to surface)."""
        self._surface.key(KEY_A, ACTION_PRESS, 0)
        self._surface.key(KEY_A, ACTION_RELEASE, 0)
        self.draw()

    def nav_back(self) -> None:
        """Go to the previous view in history (placeholder — requires axes limit API)."""
        pass

    def nav_forward(self) -> None:
        """Go to the next view in history (placeholder — requires axes limit API)."""
        pass

    # ── Internal rendering ────────────────────────────────────────────────

    def _render_frame(self) -> None:
        """Render the surface into the pixel buffer and schedule a repaint."""
        w = self._surface.width
        h = self._surface.height
        buf_size = w * h * 4
        if len(self._pixel_buf) != buf_size:
            self._pixel_buf = (ctypes.c_uint8 * buf_size)()

        if self._surface.render_into(self._pixel_buf):
            img = QImage(self._pixel_buf, w, h, w * 4, QImage.Format_RGBA8888)
            # Tell Qt this image is at device-pixel resolution so it
            # scales correctly on HiDPI screens.
            if self._dpr > 1.0:
                img.setDevicePixelRatio(self._dpr)
            self._qimage = img
            self._dirty = False
            self.update()  # schedule paintEvent
            self.frame_rendered.emit()

    @Slot()
    def _on_tick(self) -> None:
        """Timer callback — advance animations and render if dirty."""
        now = time.monotonic()
        dt = now - self._last_tick
        self._last_tick = now
        # Always advance animations (pan inertia, zoom easing, etc.)
        self._surface.update(float(dt))
        # During active animation (high FPS timer), always render so that
        # frame_rendered callbacks can drive data updates.  At idle rate
        # (≤30 FPS), only render when dirty to save GPU cycles.
        if self._dirty or self._fps > 30:
            self._render_frame()

    # ── Qt event handlers ─────────────────────────────────────────────────

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        # Always fill the entire widget with Spectra dark background first.
        # This prevents bleed-through from other windows during moves/resizes.
        painter.fillRect(self.rect(), self._bg_color)
        if self._qimage and not self._qimage.isNull():
            painter.drawImage(0, 0, self._qimage)
        painter.end()

    def showEvent(self, event) -> None:
        super().showEvent(event)
        self._update_dpr()
        # Resize surface to match widget at physical resolution
        w, h = self.width(), self.height()
        if w > 0 and h > 0:
            phys_w = max(1, int(w * self._dpr))
            phys_h = max(1, int(h * self._dpr))
            self._surface.resize(phys_w, phys_h)
            self._dirty = True

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        self._update_dpr()
        w, h = self.width(), self.height()
        if w > 0 and h > 0:
            phys_w = max(1, int(w * self._dpr))
            phys_h = max(1, int(h * self._dpr))
            self._surface.resize(phys_w, phys_h)
            self.surface_resized.emit(phys_w, phys_h)
            # Resize needs immediate render so the user doesn't see stale content
            self._render_frame()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        pos = mouse_event_pos(event)
        # Scale logical coords to physical for the surface
        self._surface.mouse_move(pos.x() * self._dpr, pos.y() * self._dpr)
        self._dirty = True

    def mousePressEvent(self, event: QMouseEvent) -> None:
        pos = mouse_event_pos(event)
        btn = _qt_button(event.button())
        mods = _qt_mods(event.modifiers())
        self._surface.mouse_button(
            btn, ACTION_PRESS, mods,
            pos.x() * self._dpr, pos.y() * self._dpr,
        )
        self._dirty = True

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        pos = mouse_event_pos(event)
        btn = _qt_button(event.button())
        mods = _qt_mods(event.modifiers())
        self._surface.mouse_button(
            btn, ACTION_RELEASE, mods,
            pos.x() * self._dpr, pos.y() * self._dpr,
        )
        self._dirty = True

    def wheelEvent(self, event: QWheelEvent) -> None:
        pos = wheel_event_pos(event)
        delta = event.angleDelta()
        dy = delta.y() / 120.0
        dx = delta.x() / 120.0
        self._surface.scroll(dx, dy, pos.x() * self._dpr, pos.y() * self._dpr)
        self._dirty = True

    def keyPressEvent(self, event: QKeyEvent) -> None:
        key = _qt_key(event.key())
        mods = _qt_mods(event.modifiers())
        if key:
            self._surface.key(key, ACTION_PRESS, mods)
            self._dirty = True
        else:
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event: QKeyEvent) -> None:
        key = _qt_key(event.key())
        mods = _qt_mods(event.modifiers())
        if key:
            self._surface.key(key, ACTION_RELEASE, mods)
        else:
            super().keyReleaseEvent(event)

    def sizeHint(self) -> QSize:
        return QSize(800, 600)

    def minimumSizeHint(self) -> QSize:
        return QSize(120, 80)


# ─── NavigationToolbarSpectra ────────────────────────────────────────────────

# SVG icon data for toolbar buttons (inline, no file dependencies)
_ICON_SVG = {
    "home": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 '
        '2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>'
    ),
    "back": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><polyline points="15 18 9 12 15 6"/></svg>'
    ),
    "forward": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>'
    ),
    "pan": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><polyline points="5 9 2 12 5 15"/>'
        '<polyline points="9 5 12 2 15 5"/><polyline points="15 19 12 22 9 19"/>'
        '<polyline points="19 9 22 12 19 15"/><line x1="2" y1="12" x2="22" '
        'y2="12"/><line x1="12" y1="2" x2="12" y2="22"/></svg>'
    ),
    "zoom": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><circle cx="11" cy="11" r="8"/>'
        '<line x1="21" y1="21" x2="16.65" y2="16.65"/>'
        '<line x1="11" y1="8" x2="11" y2="14"/>'
        '<line x1="8" y1="11" x2="14" y2="11"/></svg>'
    ),
    "save": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 '
        '1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 '
        '13 7 21"/><polyline points="7 3 7 8 15 8"/></svg>'
    ),
    "grid": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
        'fill="none" stroke="{color}" stroke-width="2" stroke-linecap="round" '
        'stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" '
        'rx="2" ry="2"/><line x1="3" y1="9" x2="21" y2="9"/>'
        '<line x1="3" y1="15" x2="21" y2="15"/>'
        '<line x1="9" y1="3" x2="9" y2="21"/>'
        '<line x1="15" y1="3" x2="15" y2="21"/></svg>'
    ),
}


def _make_icon(name: str, color: str = "#8B949E", size: int = 20) -> QIcon:
    """Create a QIcon from inline SVG data.

    Returns an empty QIcon if no QApplication exists or QtSvg is unavailable.
    """
    svg = _ICON_SVG.get(name, "")
    if not svg:
        return QIcon()

    # QPixmap requires a QApplication — guard against headless/test usage
    from ._qt_compat import QApplication as _QApp
    if _QApp.instance() is None:
        return QIcon()

    svg_bytes = svg.format(color=color).encode("utf-8")

    from ._qt_compat import QPixmap
    pixmap = QPixmap(QSize(size, size))
    pixmap.fill(QColor(0, 0, 0, 0))

    try:
        if QT_API in ("pyqt6", "pyside6"):
            from PyQt6.QtSvg import QSvgRenderer
        elif QT_API == "pyqt5":
            from PyQt5.QtSvg import QSvgRenderer
        elif QT_API == "pyside2":
            from PySide2.QtSvg import QSvgRenderer
        else:
            return QIcon()

        from ._qt_compat import QtCore
        renderer = QSvgRenderer(QtCore.QByteArray(svg_bytes))
        painter = QPainter(pixmap)
        renderer.render(painter)
        painter.end()
        return QIcon(pixmap)
    except ImportError:
        return QIcon()


class NavigationToolbarSpectra(QToolBar):
    """Navigation toolbar for a Spectra canvas.

    This is the Spectra equivalent of matplotlib's ``NavigationToolbar2QT``.
    Provides Home, Back, Forward, Pan, Zoom, Grid toggle, and Save buttons.

    Parameters
    ----------
    canvas : FigureCanvasSpectra
        The canvas this toolbar controls.
    parent : QWidget | None
        Parent widget.
    """

    def __init__(
        self,
        canvas: FigureCanvasSpectra,
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__("Spectra Navigation", parent)
        self._canvas = canvas
        self.setMovable(False)
        self.setIconSize(QSize(20, 20))
        self.setStyleSheet(
            "QToolBar { background: #161B22; border: none; border-bottom: 1px solid #30363D;"
            "  spacing: 2px; padding: 2px 4px; }"
            "QToolButton { color: #E6EDF3; background: transparent; border: none; "
            "  border-radius: 4px; padding: 5px 10px; font-size: 12px; }"
            "QToolButton:hover { background: #1C2128; color: #58A6FF; }"
            "QToolButton:checked { background: #1C2128; color: #58A6FF;"
            "  border: 1px solid #30363D; }"
            "QToolButton:pressed { background: #2D333B; }"
        )

        self._actions: dict[str, QAction] = {}
        self._build_toolbar()

    def _build_toolbar(self) -> None:
        actions = [
            ("home", "Home", "Reset view to auto-fit (A)", self._on_home),
            ("back", "Back", "Back to previous view", self._on_back),
            ("forward", "Forward", "Forward to next view", self._on_forward),
            None,  # separator
            ("pan", "Pan", "Pan with left mouse, zoom with right (drag)", self._on_pan),
            ("zoom", "Zoom", "Zoom to rectangle (drag)", self._on_zoom),
            None,  # separator
            ("grid", "Grid", "Toggle grid (G)", self._on_grid),
            ("save", "Save", "Save figure as PNG (Ctrl+S)", self._on_save),
        ]

        for item in actions:
            if item is None:
                self.addSeparator()
                continue

            name, text, tooltip, callback = item
            icon = _make_icon(name)
            action = QAction(icon, text, self)
            action.setToolTip(tooltip)
            action.triggered.connect(callback)

            if name in ("pan", "zoom"):
                action.setCheckable(True)

            self._actions[name] = action
            self.addAction(action)

        # Coordinate display label
        self._coord_label = QLabel("")
        self._coord_label.setStyleSheet(
            "color: #8B949E; font-size: 11px; padding: 0 8px; font-family: monospace;"
        )
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.addWidget(spacer)
        self.addWidget(self._coord_label)

    # ── Button handlers ───────────────────────────────────────────────────

    @Slot()
    def _on_home(self) -> None:
        self._canvas.home()

    @Slot()
    def _on_back(self) -> None:
        self._canvas.nav_back()

    @Slot()
    def _on_forward(self) -> None:
        self._canvas.nav_forward()

    @Slot()
    def _on_pan(self) -> None:
        if self._actions["pan"].isChecked():
            self._actions["zoom"].setChecked(False)
            self._canvas.set_nav_mode("pan")
        else:
            self._canvas.set_nav_mode("")

    @Slot()
    def _on_zoom(self) -> None:
        if self._actions["zoom"].isChecked():
            self._actions["pan"].setChecked(False)
            self._canvas.set_nav_mode("zoom")
        else:
            self._canvas.set_nav_mode("")

    @Slot()
    def _on_grid(self) -> None:
        self._canvas.surface.key(KEY_G, ACTION_PRESS, 0)
        self._canvas.surface.key(KEY_G, ACTION_RELEASE, 0)
        self._canvas.draw()

    @Slot()
    def _on_save(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Save Figure",
            "spectra_figure.png",
            "PNG Files (*.png);;All Files (*)",
        )
        if path:
            if self._canvas.save_figure(path):
                self.set_message(f"Saved to {path}")
            else:
                self.set_message("Save failed")

    def set_message(self, text: str) -> None:
        """Display a message in the coordinate/status area."""
        self._coord_label.setText(text)

    def update_coordinates(self, x: float, y: float) -> None:
        """Update the coordinate display."""
        self._coord_label.setText(f"x={x:.6g}  y={y:.6g}")


# ─── SpectraWidget (convenience: canvas + toolbar) ───────────────────────────

class SpectraWidget(QWidget):
    """All-in-one Spectra plot widget with canvas and navigation toolbar.

    This is the simplest way to embed a Spectra plot in a Qt application::

        widget = SpectraWidget()
        ax = widget.axes()
        ax.line(x, y, label="sin")
        ax.scatter(x2, y2, label="data")
        widget.draw()

    Parameters
    ----------
    width : int
        Initial canvas width (default 800).
    height : int
        Initial canvas height (default 600).
    parent : QWidget | None
        Parent widget.
    fps : int
        Animation FPS (default 60).
    show_toolbar : bool
        Show the navigation toolbar (default True).
    """

    def __init__(
        self,
        width: int = 800,
        height: int = 600,
        parent: Optional[QWidget] = None,
        fps: int = 60,
        show_toolbar: bool = True,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("SpectraWidget")
        # Use object-name selector so the background does NOT propagate
        # to child widgets (canvas, toolbar). Propagation causes
        # bleed-through artifacts on Linux compositors.
        self.setStyleSheet("#SpectraWidget { background: #0D1117; }")

        self._canvas = FigureCanvasSpectra(width, height, fps=fps)
        self._toolbar = NavigationToolbarSpectra(self._canvas)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        if show_toolbar:
            layout.addWidget(self._toolbar)
        layout.addWidget(self._canvas)

        self._toolbar.setVisible(show_toolbar)

    @property
    def canvas(self) -> FigureCanvasSpectra:
        """The plot canvas."""
        return self._canvas

    @property
    def toolbar(self) -> NavigationToolbarSpectra:
        """The navigation toolbar."""
        return self._toolbar

    @property
    def surface(self) -> EmbedSurface:
        """The underlying ``EmbedSurface``."""
        return self._canvas.surface

    def figure(self) -> EmbedFigure:
        """Get or create the figure."""
        return self._canvas.figure()

    def axes(self, rows: int = 1, cols: int = 1, index: int = 1) -> EmbedAxes:
        """Get or create axes."""
        return self._canvas.axes(rows, cols, index)

    def axes3d(self, rows: int = 1, cols: int = 1, index: int = 1) -> EmbedAxes:
        """Create 3D axes."""
        return self._canvas.axes3d(rows, cols, index)

    def draw(self) -> None:
        """Redraw the canvas."""
        self._canvas.draw()

    def draw_idle(self) -> None:
        """Request a non-blocking redraw (safe from callbacks)."""
        self._canvas.draw_idle()

    def new_timer(self, interval: int = 1000, single_shot: bool = False) -> SpectraTimer:
        """Create a new timer attached to this canvas."""
        return self._canvas.new_timer(interval=interval, single_shot=single_shot)

    def start_animation(self, fps: Optional[int] = None) -> None:
        """Start the animation timer."""
        self._canvas.start_animation(fps)

    def stop_animation(self) -> None:
        """Stop the animation timer."""
        self._canvas.stop_animation()

    def save_figure(self, path: str) -> bool:
        """Save current frame to PNG."""
        return self._canvas.save_figure(path)

    def set_toolbar_visible(self, visible: bool) -> None:
        """Show or hide the toolbar."""
        self._toolbar.setVisible(visible)


# ─── Convenience aliases (matplotlib-compatible naming) ──────────────────────

FigureCanvas = FigureCanvasSpectra
NavigationToolbar = NavigationToolbarSpectra
