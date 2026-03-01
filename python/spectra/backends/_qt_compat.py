"""Qt binding compatibility layer — auto-detects PyQt5, PySide2, PyQt6, or PySide6.

Import order (first available wins):
    1. ``QT_API`` environment variable (``pyqt5``, ``pyside2``, ``pyqt6``, ``pyside6``)
    2. Already-imported binding (check sys.modules)
    3. PyQt6 → PySide6 → PyQt5 → PySide2

After import, all symbols are normalized so the rest of the codebase uses a
single API surface regardless of binding.

Differences handled:
    - Qt6 removed ``QWidget.pos()`` from ``QMouseEvent`` (use ``position()`` instead)
    - Qt6 enums moved from ``Qt.Key_A`` to ``Qt.Key.Key_A``
    - PySide uses ``Signal``/``Slot`` instead of ``pyqtSignal``/``pyqtSlot``
    - ``exec_()`` deprecated in Qt6 in favour of ``exec()``
"""

from __future__ import annotations

import os
import sys

# ─── Detect binding ──────────────────────────────────────────────────────────

QT_API: str = ""  # set to "pyqt5", "pyside2", "pyqt6", "pyside6"
QT_VERSION: int = 0  # 5 or 6

_env = os.environ.get("QT_API", "").lower().strip()

_BINDINGS_5 = ["pyqt5", "pyside2"]
_BINDINGS_6 = ["pyqt6", "pyside6"]
_BINDINGS = _BINDINGS_6 + _BINDINGS_5  # prefer Qt6

_MODULE_MAP = {
    "pyqt5": "PyQt5",
    "pyside2": "PySide2",
    "pyqt6": "PyQt6",
    "pyside6": "PySide6",
}


def _try_import(api: str) -> bool:
    mod = _MODULE_MAP.get(api)
    if mod is None:
        return False
    try:
        __import__(f"{mod}.QtCore")
        __import__(f"{mod}.QtGui")
        __import__(f"{mod}.QtWidgets")
        return True
    except ImportError:
        return False


def _detect() -> str:
    # 1. Env var
    if _env in _MODULE_MAP:
        if _try_import(_env):
            return _env
        raise ImportError(
            f"QT_API={_env!r} requested but {_MODULE_MAP[_env]} is not installed"
        )

    # 2. Already imported
    for api, mod in _MODULE_MAP.items():
        if f"{mod}.QtCore" in sys.modules:
            return api

    # 3. Probe in preference order
    for api in _BINDINGS:
        if _try_import(api):
            return api

    raise ImportError(
        "No Qt binding found. Install one of:\n"
        "  pip install PyQt6      # recommended\n"
        "  pip install PySide6\n"
        "  pip install PyQt5\n"
        "  pip install PySide2\n"
        "Or set QT_API=pyqt6 (etc.) to force a specific binding."
    )


QT_API = _detect()
QT_VERSION = 6 if QT_API in _BINDINGS_6 else 5

# ─── Unified imports ─────────────────────────────────────────────────────────

if QT_API == "pyqt6":
    from PyQt6 import QtCore, QtGui, QtWidgets
    from PyQt6.QtCore import Qt, QTimer, QSize, QPoint, QRect, QPointF
    from PyQt6.QtGui import (
        QImage, QPainter, QMouseEvent, QWheelEvent, QKeyEvent,
        QIcon, QPixmap, QAction, QCursor, QPen, QColor, QFont,
    )
    from PyQt6.QtWidgets import (
        QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
        QToolBar, QToolButton, QSizePolicy, QFileDialog, QMessageBox,
        QMainWindow, QStatusBar, QSplitter, QFrame,
    )
    Signal = QtCore.pyqtSignal
    Slot = QtCore.pyqtSlot

elif QT_API == "pyside6":
    from PySide6 import QtCore, QtGui, QtWidgets
    from PySide6.QtCore import Qt, QTimer, QSize, QPoint, QRect, QPointF, Signal, Slot
    from PySide6.QtGui import (
        QImage, QPainter, QMouseEvent, QWheelEvent, QKeyEvent,
        QIcon, QPixmap, QAction, QCursor, QPen, QColor, QFont,
    )
    from PySide6.QtWidgets import (
        QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
        QToolBar, QToolButton, QSizePolicy, QFileDialog, QMessageBox,
        QMainWindow, QStatusBar, QSplitter, QFrame,
    )

elif QT_API == "pyqt5":
    from PyQt5 import QtCore, QtGui, QtWidgets
    from PyQt5.QtCore import Qt, QTimer, QSize, QPoint, QRect, QPointF
    from PyQt5.QtGui import (
        QImage, QPainter, QMouseEvent, QWheelEvent, QKeyEvent,
        QIcon, QPixmap, QCursor, QPen, QColor, QFont,
    )
    from PyQt5.QtWidgets import (
        QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
        QToolBar, QToolButton, QSizePolicy, QFileDialog, QMessageBox,
        QMainWindow, QStatusBar, QSplitter, QFrame, QAction,
    )
    Signal = QtCore.pyqtSignal
    Slot = QtCore.pyqtSlot

elif QT_API == "pyside2":
    from PySide2 import QtCore, QtGui, QtWidgets
    from PySide2.QtCore import Qt, QTimer, QSize, QPoint, QRect, QPointF, Signal, Slot
    from PySide2.QtGui import (
        QImage, QPainter, QMouseEvent, QWheelEvent, QKeyEvent,
        QIcon, QPixmap, QCursor, QPen, QColor, QFont,
    )
    from PySide2.QtWidgets import (
        QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
        QToolBar, QToolButton, QSizePolicy, QFileDialog, QMessageBox,
        QMainWindow, QStatusBar, QSplitter, QFrame, QAction,
    )
    Signal = QtCore.Signal
    Slot = QtCore.Slot


# ─── Qt6 scoped enum normalization ───────────────────────────────────────────
# Qt6 (PyQt6/PySide6) uses scoped enums: Qt.MouseButton.LeftButton
# Qt5 (PyQt5/PySide2) uses flat enums: Qt.LeftButton
# Normalize to flat style so downstream code works on both.

if QT_VERSION >= 6:
    # Mouse buttons
    Qt.LeftButton = Qt.MouseButton.LeftButton
    Qt.RightButton = Qt.MouseButton.RightButton
    Qt.MiddleButton = Qt.MouseButton.MiddleButton
    Qt.NoButton = Qt.MouseButton.NoButton

    # Keyboard modifiers
    Qt.NoModifier = Qt.KeyboardModifier.NoModifier
    Qt.ShiftModifier = Qt.KeyboardModifier.ShiftModifier
    Qt.ControlModifier = Qt.KeyboardModifier.ControlModifier
    Qt.AltModifier = Qt.KeyboardModifier.AltModifier
    Qt.MetaModifier = Qt.KeyboardModifier.MetaModifier

    # Focus policy
    Qt.StrongFocus = Qt.FocusPolicy.StrongFocus
    Qt.NoFocus = Qt.FocusPolicy.NoFocus

    # Cursor shapes
    Qt.ArrowCursor = Qt.CursorShape.ArrowCursor
    Qt.OpenHandCursor = Qt.CursorShape.OpenHandCursor
    Qt.ClosedHandCursor = Qt.CursorShape.ClosedHandCursor
    Qt.CrossCursor = Qt.CursorShape.CrossCursor
    Qt.PointingHandCursor = Qt.CursorShape.PointingHandCursor

    # Orientation
    Qt.Horizontal = Qt.Orientation.Horizontal
    Qt.Vertical = Qt.Orientation.Vertical

    # Keys (A-Z, 0-9 have same integer values in Qt5 and Qt6)
    Qt.Key_A = Qt.Key.Key_A
    Qt.Key_B = Qt.Key.Key_B
    Qt.Key_C = Qt.Key.Key_C
    Qt.Key_G = Qt.Key.Key_G
    Qt.Key_R = Qt.Key.Key_R
    Qt.Key_S = Qt.Key.Key_S
    Qt.Key_Z = Qt.Key.Key_Z
    Qt.Key_0 = Qt.Key.Key_0
    Qt.Key_9 = Qt.Key.Key_9
    Qt.Key_Escape = Qt.Key.Key_Escape
    Qt.Key_Space = Qt.Key.Key_Space
    Qt.Key_Tab = Qt.Key.Key_Tab

    # QImage formats
    QImage.Format_RGBA8888 = QImage.Format.Format_RGBA8888
    QImage.Format_ARGB32 = QImage.Format.Format_ARGB32

    # QSizePolicy
    QSizePolicy.Expanding = QSizePolicy.Policy.Expanding
    QSizePolicy.Preferred = QSizePolicy.Policy.Preferred
    QSizePolicy.Fixed = QSizePolicy.Policy.Fixed

    # Widget attributes
    Qt.WA_OpaquePaintEvent = Qt.WidgetAttribute.WA_OpaquePaintEvent
    Qt.WA_NoSystemBackground = Qt.WidgetAttribute.WA_NoSystemBackground


# ─── Compatibility helpers ───────────────────────────────────────────────────

def mouse_event_pos(event: QMouseEvent) -> QPointF:
    """Get mouse event position, compatible with Qt5 and Qt6."""
    if QT_VERSION >= 6:
        return event.position()
    return QPointF(event.pos())


def wheel_event_pos(event: QWheelEvent) -> QPointF:
    """Get wheel event position, compatible with Qt5 and Qt6."""
    if QT_VERSION >= 6:
        return event.position()
    return QPointF(event.pos())


def exec_app(app: QApplication) -> int:
    """Call app.exec() or app.exec_() depending on Qt version."""
    if hasattr(app, "exec"):
        return app.exec()
    return app.exec_()


def enum_value(enum_member):
    """Get integer value from a Qt enum member (Qt6 uses proper enums)."""
    if hasattr(enum_member, "value"):
        return enum_member.value
    return int(enum_member)
