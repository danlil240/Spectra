#!/usr/bin/env python3
"""Easy Embed + PyQt5 example.

This example uses the high-level `spectra.embed` API to render offscreen frames
and display them inside a Qt widget.

Requirements:
    pip install PyQt5

Build requirements (from project root):
    cmake -S . -B build -DSPECTRA_BUILD_EMBED_SHARED=ON
    cmake --build build --target spectra_embed

If needed, point to the shared library:
    export SPECTRA_EMBED_LIB=/absolute/path/to/build/libspectra_embed.so

Run:
    python python/examples/easy_embed_pyqt.py

Controls:
    Space  -> pause/resume animation
    S      -> save current frame to easy_embed_pyqt_frame.png
"""

from __future__ import annotations

import math
import os
import sys
from pathlib import Path

from PyQt5.QtCore import QTimer, Qt
from PyQt5.QtGui import QImage, QKeyEvent, QPainter
from PyQt5.QtWidgets import QApplication, QLabel, QVBoxLayout, QWidget

# Ensure `import spectra` works when run from the source tree.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import spectra.embed as spe


class EasyEmbedCanvas(QWidget):
    """Qt widget that displays frames rendered via spectra.embed."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumSize(480, 320)
        self.setFocusPolicy(Qt.StrongFocus)

        self._phase = 0.0
        self._playing = True
        self._image: QImage | None = None

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._on_tick)
        self._timer.start(33)  # ~30 FPS

        self._render_frame()

    def _build_series(self, phase: float):
        x = [i * 0.02 for i in range(700)]
        y1 = [math.sin(v + phase) for v in x]
        y2 = [0.5 * math.cos(v * 0.7 - phase * 1.3) for v in x]
        y3 = [0.15 * math.sin(v * 2.5 + phase * 0.4) for v in x]
        return x, y1, y2, y3

    def _render_frame(self) -> None:
        w = max(2, self.width())
        h = max(2, self.height())

        x, y1, y2, y3 = self._build_series(self._phase)
        img = spe.render_multi(
            [
                (x, y1, "sin(x + t)"),
                (x, y2, "0.5 cos(0.7x - 1.3t)"),
                (x, y3, "0.15 sin(2.5x + 0.4t)"),
            ],
            width=w,
            height=h,
            title="Spectra Easy Embed in PyQt5",
        )

        # Convert RGBA bytes to QImage. copy() detaches from Python-owned bytes.
        self._image = QImage(
            img.data,
            img.width,
            img.height,
            img.stride,
            QImage.Format_RGBA8888,
        ).copy()
        self.update()

    def _on_tick(self) -> None:
        if not self._playing:
            return
        self._phase += 0.06
        self._render_frame()

    def paintEvent(self, event) -> None:  # noqa: N802 (Qt naming)
        if not self._image or self._image.isNull():
            return
        painter = QPainter(self)
        painter.drawImage(self.rect(), self._image)
        painter.end()

    def resizeEvent(self, event) -> None:  # noqa: N802 (Qt naming)
        super().resizeEvent(event)
        self._render_frame()

    def keyPressEvent(self, event: QKeyEvent) -> None:  # noqa: N802 (Qt naming)
        if event.key() == Qt.Key_Space:
            self._playing = not self._playing
            self._render_frame()
            return

        if event.key() == Qt.Key_S and self._image and not self._image.isNull():
            out = "easy_embed_pyqt_frame.png"
            if self._image.save(out):
                print(f"Saved frame to {os.path.abspath(out)}")
            else:
                print("Failed to save frame")
            return

        super().keyPressEvent(event)


class MainWindow(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Spectra — Easy Embed PyQt5 Demo")
        self.resize(1100, 700)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        help_text = QLabel(
            "  Spectra easy embed (offscreen Vulkan)  |  Space: play/pause  |  S: save frame"
        )
        help_text.setStyleSheet(
            "background:#0f172a; color:#cbd5e1; padding:6px; font-size:12px;"
        )
        layout.addWidget(help_text)

        self.canvas = EasyEmbedCanvas(self)
        layout.addWidget(self.canvas)


def main() -> None:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
