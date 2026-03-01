#!/usr/bin/env python3
"""Spectra Qt dynamic plotting demo — matching matplotlib's dynamic example.

This demonstrates the same pattern as matplotlib's embedding_in_qt_sgskip example:
- A static canvas with a one-time plot
- A dynamic canvas with decoupled data + drawing timers
- canvas.new_timer() for creating independent timers
- series.set_data(x, y) for atomic data updates
- canvas.draw_idle() for non-blocking repaint requests

Requirements:
    pip install PyQt5 numpy   # or PyQt6, PySide6, PySide2
    # Build the embed library:
    cmake -S . -B build -DSPECTRA_BUILD_EMBED_SHARED=ON
    cmake --build build --target spectra_embed

Run:
    python python/examples/qt_dynamic_demo.py
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

# Ensure `import spectra` works from source tree
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from spectra.backends._qt_compat import (
    QApplication,
    QMainWindow,
    QVBoxLayout,
    QWidget,
    exec_app,
)
from spectra.backends.backend_qtagg import (
    FigureCanvasSpectra,
    NavigationToolbarSpectra,
)


class ApplicationWindow(QMainWindow):
    """Direct Spectra equivalent of the matplotlib dynamic embedding example."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Spectra — Dynamic Plotting (matplotlib-compatible)")
        self.resize(900, 750)

        self._main = QWidget()
        self.setCentralWidget(self._main)
        layout = QVBoxLayout(self._main)

        # ── Static canvas ──────────────────────────────────────────────
        static_canvas = FigureCanvasSpectra(width=800, height=300)
        layout.addWidget(NavigationToolbarSpectra(static_canvas, self))
        layout.addWidget(static_canvas)

        # ── Dynamic canvas ─────────────────────────────────────────────
        dynamic_canvas = FigureCanvasSpectra(width=800, height=300)
        layout.addWidget(dynamic_canvas)
        layout.addWidget(NavigationToolbarSpectra(dynamic_canvas, self))

        # ── Static plot: tan(t) ────────────────────────────────────────
        self._static_ax = static_canvas.axes()
        t = np.linspace(0, 10, 501)
        self._static_ax.scatter(t.tolist(), np.tan(t).tolist(), label="tan(t)")
        self._static_ax.set_title("Static: tan(t)")
        self._static_ax.set_ylim(-5.0, 5.0)
        static_canvas.draw()

        # ── Dynamic plot: shifting sinusoid ────────────────────────────
        self._dynamic_ax = dynamic_canvas.axes()
        self._dynamic_ax.set_title("Dynamic: sin(x + t)")
        self._dynamic_canvas = dynamic_canvas

        # Set up initial data
        self.xdata = np.linspace(0, 10, 101)
        self._update_ydata()
        self._line = self._dynamic_ax.line(
            self.xdata.tolist(), self.ydata.tolist(), label="sin(x + t)"
        )

        # ── Timers (exactly like matplotlib) ───────────────────────────

        # Data retrieval timer — as fast as possible (1 ms)
        self.data_timer = dynamic_canvas.new_timer(1)
        self.data_timer.add_callback(self._update_ydata)
        self.data_timer.start()

        # Drawing timer — 50 Hz (20 ms), smooth without overloading the GUI
        self.drawing_timer = dynamic_canvas.new_timer(20)
        self.drawing_timer.add_callback(self._update_canvas)
        self.drawing_timer.start()

    def _update_ydata(self):
        """Shift the sinusoid as a function of time."""
        self.ydata = np.sin(self.xdata + time.time())

    def _update_canvas(self):
        """Push new data to the line and request a repaint."""
        self._line.set_data(self.xdata, self.ydata)
        self._dynamic_canvas.draw_idle()



if __name__ == "__main__":
    # Check whether there is already a running QApplication (e.g., if running
    # from an IDE).
    qapp = QApplication.instance()
    if not qapp:
        qapp = QApplication(sys.argv)


    app = ApplicationWindow()
    app.show()
    app.activateWindow()
    app.raise_()
    sys.exit(exec_app(qapp))
