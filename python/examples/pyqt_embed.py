#!/usr/bin/env python3
"""PyQt5 example: embed a GPU-accelerated Spectra plot inside a QWidget.

Requirements:
    pip install PyQt5
    # Build the shared library first:
    cd build && cmake .. -DSPECTRA_BUILD_EMBED_SHARED=ON && make spectra_embed

Run:
    python pyqt_embed.py

This renders a Spectra plot to a CPU buffer and paints it into a QWidget
using QImage + QPainter. Pan with left-drag, zoom with scroll wheel,
reset with 'R' key.
"""

import math
import sys
import os

# Add parent directory so we can import spectra
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spectra._embed import (
    EmbedSurface,
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
    KEY_ESCAPE,
)

from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QLabel
from PyQt5.QtGui import QImage, QPainter, QMouseEvent, QWheelEvent, QKeyEvent
from PyQt5.QtCore import Qt, QTimer
import ctypes


class SpectraWidget(QWidget):
    """QWidget that hosts an embedded Spectra plot."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(400, 300)
        self.resize(800, 600)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.StrongFocus)

        # Create the Spectra embed surface
        self._surface = EmbedSurface(self.width(), self.height())

        # Create a figure with data
        fig = self._surface.figure()
        ax = fig.subplot(1, 1, 1)

        # Generate sample data: sin, cos, and a damped wave
        N = 300
        x = [i * 0.05 for i in range(N)]
        y_sin = [math.sin(v) for v in x]
        y_cos = [math.cos(v) for v in x]
        y_damp = [math.exp(-v * 0.1) * math.sin(v * 2) for v in x]

        ax.line(x, y_sin, label="sin(x)")
        ax.line(x, y_cos, label="cos(x)")
        ax.line(x, y_damp, label="damped")

        # Pre-allocate pixel buffer for zero-copy rendering
        buf_size = self.width() * self.height() * 4
        self._pixel_buf = (ctypes.c_uint8 * buf_size)()
        self._qimage = None

        # Initial render
        self._render_frame()

        # Timer for animation updates (~60 FPS)
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._on_timer)
        self._timer.start(16)

    def _render_frame(self):
        """Render Spectra plot into the pixel buffer and schedule repaint."""
        w = self._surface.width
        h = self._surface.height
        buf_size = w * h * 4
        if len(self._pixel_buf) != buf_size:
            self._pixel_buf = (ctypes.c_uint8 * buf_size)()

        if self._surface.render_into(self._pixel_buf):
            # Wrap the ctypes buffer in a QImage (no copy)
            self._qimage = QImage(
                self._pixel_buf, w, h, w * 4, QImage.Format_RGBA8888
            )
            self.update()

    def _on_timer(self):
        """Advance animations and re-render."""
        self._surface.update(1.0 / 60.0)
        self._render_frame()

    # ── Qt event handlers → Spectra input ────────────────────────────────

    def paintEvent(self, event):
        if self._qimage and not self._qimage.isNull():
            painter = QPainter(self)
            painter.drawImage(0, 0, self._qimage)
            painter.end()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        w, h = self.width(), self.height()
        if w > 0 and h > 0:
            self._surface.resize(w, h)
            self._render_frame()

    def mouseMoveEvent(self, event: QMouseEvent):
        pos = event.pos()
        self._surface.mouse_move(float(pos.x()), float(pos.y()))
        self._render_frame()

    def mousePressEvent(self, event: QMouseEvent):
        pos = event.pos()
        btn = self._qt_button(event.button())
        mods = self._qt_mods(event.modifiers())
        self._surface.mouse_button(btn, ACTION_PRESS, mods,
                                   float(pos.x()), float(pos.y()))
        self._render_frame()

    def mouseReleaseEvent(self, event: QMouseEvent):
        pos = event.pos()
        btn = self._qt_button(event.button())
        mods = self._qt_mods(event.modifiers())
        self._surface.mouse_button(btn, ACTION_RELEASE, mods,
                                   float(pos.x()), float(pos.y()))
        self._render_frame()

    def wheelEvent(self, event: QWheelEvent):
        pos = event.pos()
        dy = event.angleDelta().y() / 120.0
        dx = event.angleDelta().x() / 120.0
        self._surface.scroll(dx, dy, float(pos.x()), float(pos.y()))
        self._render_frame()

    def keyPressEvent(self, event: QKeyEvent):
        key = self._qt_key(event.key())
        mods = self._qt_mods(event.modifiers())
        if key:
            self._surface.key(key, ACTION_PRESS, mods)
            self._render_frame()

    def keyReleaseEvent(self, event: QKeyEvent):
        key = self._qt_key(event.key())
        mods = self._qt_mods(event.modifiers())
        if key:
            self._surface.key(key, ACTION_RELEASE, mods)

    # ── Qt → Spectra constant translation ────────────────────────────────

    @staticmethod
    def _qt_button(btn):
        if btn == Qt.LeftButton:
            return MOUSE_LEFT
        elif btn == Qt.RightButton:
            return MOUSE_RIGHT
        elif btn == Qt.MiddleButton:
            return MOUSE_MIDDLE
        return 0

    @staticmethod
    def _qt_mods(mods):
        result = 0
        if mods & Qt.ShiftModifier:
            result |= MOD_SHIFT
        if mods & Qt.ControlModifier:
            result |= MOD_CONTROL
        if mods & Qt.AltModifier:
            result |= MOD_ALT
        return result

    @staticmethod
    def _qt_key(qt_key):
        """Convert Qt key code to Spectra key constant."""
        # Letters A-Z match between Qt and GLFW (both 65-90)
        if Qt.Key_A <= qt_key <= Qt.Key_Z:
            return qt_key
        # Numbers 0-9 match (both 48-57)
        if Qt.Key_0 <= qt_key <= Qt.Key_9:
            return qt_key
        mapping = {
            Qt.Key_Escape: KEY_ESCAPE,
            Qt.Key_R: KEY_R,
            Qt.Key_G: KEY_G,
            Qt.Key_A: KEY_A,
        }
        return mapping.get(qt_key, 0)


def main():
    app = QApplication(sys.argv)

    # Main window with the Spectra widget
    window = QWidget()
    window.setWindowTitle("Spectra — PyQt5 Embed Demo")
    window.resize(900, 650)

    layout = QVBoxLayout(window)
    layout.setContentsMargins(0, 0, 0, 0)

    label = QLabel("  Pan: left-drag  |  Zoom: scroll wheel  |  Reset: R key")
    label.setStyleSheet("background: #1a1a2e; color: #aaa; padding: 4px; font-size: 12px;")
    layout.addWidget(label)

    spectra_widget = SpectraWidget()
    layout.addWidget(spectra_widget)

    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
