#!/usr/bin/env python3
"""Minimal Spectra Qt embed — side-by-side comparison with matplotlib.

Matplotlib equivalent::

    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
    from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT as NavigationToolbar
    from matplotlib.figure import Figure

    canvas = FigureCanvas(Figure())
    toolbar = NavigationToolbar(canvas, window)
    ax = canvas.figure.add_subplot(111)
    ax.plot(x, y)
    canvas.draw()

Spectra equivalent::

    from spectra.backends.backend_qtagg import FigureCanvasSpectra as FigureCanvas
    from spectra.backends.backend_qtagg import NavigationToolbarSpectra as NavigationToolbar

    canvas = FigureCanvas()
    toolbar = NavigationToolbar(canvas, window)
    ax = canvas.axes()
    ax.line(x, y)
    canvas.draw()

Requirements:
    pip install PyQt6   # or PyQt5, PySide6, PySide2
    cmake -S . -B build -DSPECTRA_BUILD_EMBED_SHARED=ON
    cmake --build build --target spectra_embed

Run:
    python python/examples/qt_minimal.py
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# ─── Spectra imports (mirrors matplotlib pattern) ────────────────────────────
from spectra.backends.backend_qtagg import FigureCanvasSpectra as FigureCanvas
from spectra.backends.backend_qtagg import NavigationToolbarSpectra as NavigationToolbar
from spectra.backends._qt_compat import QApplication, QMainWindow, exec_app


def main() -> None:
    app = QApplication(sys.argv)

    window = QMainWindow()
    window.setWindowTitle("Spectra — Minimal Qt Embed")
    window.resize(900, 650)

    # Create canvas (like matplotlib's FigureCanvas)
    canvas = FigureCanvas()

    # Create toolbar (like matplotlib's NavigationToolbar2QT)
    toolbar = NavigationToolbar(canvas, window)

    # Add data (like matplotlib's ax.plot)
    ax = canvas.axes()
    x = [i * 0.05 for i in range(300)]
    ax.line(x, [math.sin(v) for v in x], label="sin(x)")
    ax.line(x, [math.cos(v) for v in x], label="cos(x)")
    canvas.draw()

    # Assemble window (same as matplotlib)
    window.addToolBar(toolbar)
    window.setCentralWidget(canvas)
    window.show()

    sys.exit(exec_app(app))


if __name__ == "__main__":
    main()
