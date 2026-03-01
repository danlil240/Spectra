#!/usr/bin/env python3
"""Spectra Qt backend demo — works with PyQt5, PySide2, PyQt6, or PySide6.

This demonstrates the matplotlib-compatible backend API::

    from spectra.backends.backend_qtagg import FigureCanvasSpectra, NavigationToolbarSpectra

Equivalent matplotlib code would be::

    from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg, NavigationToolbar2QT

Requirements:
    pip install PyQt6   # or PyQt5, PySide6, PySide2
    # Build the embed library:
    cmake -S . -B build -DSPECTRA_BUILD_EMBED_SHARED=ON
    cmake --build build --target spectra_embed

Run:
    python python/examples/qt_backend_demo.py

Controls:
    Toolbar buttons: Home, Pan, Zoom, Grid, Save
    Mouse: left-drag pan, scroll zoom, right-drag directional zoom
    Keys: A = auto-fit, G = grid toggle, R = reset
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

# Ensure `import spectra` works from source tree
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from spectra.backends._qt_compat import (
    QApplication,
    QMainWindow,
    QSplitter,
    Qt,
    exec_app,
)
from spectra.backends.backend_qtagg import (
    FigureCanvasSpectra,
    NavigationToolbarSpectra,
    SpectraWidget,
)


class SinglePlotWindow(QMainWindow):
    """Simple window with one Spectra plot + toolbar (like matplotlib)."""

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Spectra — Single Plot (matplotlib-style)")
        self.resize(900, 650)

        # Create canvas + toolbar (exactly like matplotlib)
        self.canvas = FigureCanvasSpectra(800, 600)
        self.toolbar = NavigationToolbarSpectra(self.canvas, self)

        # Add data
        ax = self.canvas.axes()
        N = 500
        x = [i * 0.04 for i in range(N)]
        y_sin = [math.sin(v) for v in x]
        y_cos = [math.cos(v) for v in x]
        y_damp = [math.exp(-v * 0.08) * math.sin(v * 2) for v in x]

        ax.line(x, y_sin, label="sin(x)")
        ax.line(x, y_cos, label="cos(x)")
        ax.line(x, y_damp, label="damped")

        self.canvas.draw()

        # Layout
        self.addToolBar(self.toolbar)
        self.setCentralWidget(self.canvas)

        # Status bar
        self.statusBar().showMessage(
            "Pan: left-drag | Zoom: scroll | Auto-fit: A | Grid: G"
        )


class MultiPlotWindow(QMainWindow):
    """Window with two Spectra plots side-by-side using SpectraWidget."""

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Spectra — Multi-Plot Layout")
        self.resize(1200, 600)

        splitter = QSplitter(Qt.Horizontal)

        # Left plot: line data
        self.left = SpectraWidget(show_toolbar=True)
        ax1 = self.left.axes()
        x = [i * 0.05 for i in range(400)]
        ax1.line(x, [math.sin(v) * math.cos(v * 0.3) for v in x], label="beat")
        ax1.line(x, [0.5 * math.sin(v * 3) for v in x], label="fast")
        self.left.draw()

        # Right plot: scatter data
        self.right = SpectraWidget(show_toolbar=True)
        ax2 = self.right.axes()
        import random
        random.seed(42)
        sx = [random.gauss(0, 1) for _ in range(300)]
        sy = [0.7 * xi + random.gauss(0, 0.3) for xi in sx]
        ax2.scatter(sx, sy, label="samples")
        self.right.draw()

        splitter.addWidget(self.left)
        splitter.addWidget(self.right)
        self.setCentralWidget(splitter)


class AnimatedPlotWindow(QMainWindow):
    """Window with an animated plot using the animation timer."""

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Spectra — Animated Plot")
        self.resize(900, 650)

        self.widget = SpectraWidget(show_toolbar=True)
        self.setCentralWidget(self.widget)

        # Create figure and initial data
        self._ax = self.widget.axes()
        N = 500
        self._x = [i * 0.04 for i in range(N)]
        self._phase = 0.0

        y = [math.sin(v) for v in self._x]
        self._series = self._ax.line(self._x, y, label="wave")

        # Set up animation at 60 FPS for smooth rendering
        self.widget.start_animation(fps=175)
        self.widget.canvas.frame_rendered.connect(self._update_data)

    def _update_data(self) -> None:
        self._phase += 0.03
        y = [math.sin(v + self._phase) * math.cos(v * 0.3 - self._phase * 0.7)
             for v in self._x]
        self._series.set_y(y)


# Spectra dark theme for Qt window chrome only.
# IMPORTANT: Do NOT set background on QMainWindow — it propagates to ALL
# child widgets via CSS inheritance and causes bleed-through artifacts
# when windows overlap (the canvas widget paints its own opaque background).
_SPECTRA_DARK_QSS = """
QMainWindow > QWidget { background: #0D1117; }
QStatusBar { background: #161B22; color: #8B949E; border-top: 1px solid #30363D; }
QSplitter { background: #0D1117; }
QSplitter::handle { background: #30363D; width: 2px; }
QMenuBar { background: #161B22; color: #E6EDF3; }
QMenuBar::item:selected { background: #1C2128; }
"""


def main() -> None:
    app = QApplication(sys.argv)
    app.setStyleSheet(_SPECTRA_DARK_QSS)

    # Show which Qt binding was detected
    from spectra.backends._qt_compat import QT_API, QT_VERSION
    print(f"Using Qt binding: {QT_API} (Qt{QT_VERSION})")

    # Launch all demo windows
    win1 = SinglePlotWindow()
    win1.show()

    win2 = MultiPlotWindow()
    win2.move(win1.x() + 50, win1.y() + 50)
    win2.show()

    win3 = AnimatedPlotWindow()
    win3.move(win1.x() + 100, win1.y() + 100)
    win3.show()

    sys.exit(exec_app(app))


if __name__ == "__main__":
    main()
