"""Tests for spectra.backends — Qt compatibility layer and backend_qtagg.

These tests verify the module structure, import mechanics, and non-GUI logic.
Tests that require a running QApplication or GPU are marked with pytest.mark.skipif.
"""

from __future__ import annotations

import pytest  # noqa: F401 — used for marks in future GPU tests


# ─── Qt compat layer tests ───────────────────────────────────────────────────

class TestQtCompat:
    """Tests for spectra.backends._qt_compat auto-detection."""

    def test_qt_api_is_string(self):
        from spectra.backends._qt_compat import QT_API
        assert isinstance(QT_API, str)
        assert QT_API in ("pyqt5", "pyside2", "pyqt6", "pyside6")

    def test_qt_version_is_5_or_6(self):
        from spectra.backends._qt_compat import QT_VERSION
        assert QT_VERSION in (5, 6)

    def test_qt_api_matches_version(self):
        from spectra.backends._qt_compat import QT_API, QT_VERSION
        if QT_API in ("pyqt5", "pyside2"):
            assert QT_VERSION == 5
        else:
            assert QT_VERSION == 6

    def test_core_imports_available(self):
        from spectra.backends._qt_compat import Qt, QTimer, QSize, QPointF
        assert Qt is not None
        assert QTimer is not None
        assert QSize is not None
        assert QPointF is not None

    def test_gui_imports_available(self):
        from spectra.backends._qt_compat import QImage, QPainter, QIcon, QColor
        assert QImage is not None
        assert QPainter is not None
        assert QIcon is not None
        assert QColor is not None

    def test_widget_imports_available(self):
        from spectra.backends._qt_compat import (
            QWidget, QVBoxLayout, QLabel, QToolBar, QFileDialog,
        )
        assert QWidget is not None
        assert QVBoxLayout is not None
        assert QLabel is not None
        assert QToolBar is not None
        assert QFileDialog is not None

    def test_signal_slot_available(self):
        from spectra.backends._qt_compat import Signal, Slot
        assert Signal is not None
        assert Slot is not None

    def test_action_import(self):
        """QAction is in QtWidgets (Qt5) vs QtGui (Qt6) — compat handles it."""
        from spectra.backends._qt_compat import QAction
        assert QAction is not None

    def test_mouse_event_pos_callable(self):
        from spectra.backends._qt_compat import mouse_event_pos
        assert callable(mouse_event_pos)

    def test_wheel_event_pos_callable(self):
        from spectra.backends._qt_compat import wheel_event_pos
        assert callable(wheel_event_pos)

    def test_exec_app_callable(self):
        from spectra.backends._qt_compat import exec_app
        assert callable(exec_app)

    def test_enum_value_callable(self):
        from spectra.backends._qt_compat import enum_value
        assert callable(enum_value)


# ─── Backend module structure tests ──────────────────────────────────────────

class TestBackendStructure:
    """Tests for spectra.backends.backend_qtagg module structure."""

    def test_module_imports(self):
        import spectra.backends.backend_qtagg as mod
        assert hasattr(mod, "FigureCanvasSpectra")
        assert hasattr(mod, "NavigationToolbarSpectra")
        assert hasattr(mod, "SpectraWidget")

    def test_aliases_exist(self):
        from spectra.backends.backend_qtagg import FigureCanvas, NavigationToolbar
        from spectra.backends.backend_qtagg import FigureCanvasSpectra, NavigationToolbarSpectra
        assert FigureCanvas is FigureCanvasSpectra
        assert NavigationToolbar is NavigationToolbarSpectra

    def test_canvas_is_qwidget(self):
        from spectra.backends.backend_qtagg import FigureCanvasSpectra
        from spectra.backends._qt_compat import QWidget
        assert issubclass(FigureCanvasSpectra, QWidget)

    def test_toolbar_is_qtoolbar(self):
        from spectra.backends.backend_qtagg import NavigationToolbarSpectra
        from spectra.backends._qt_compat import QToolBar
        assert issubclass(NavigationToolbarSpectra, QToolBar)

    def test_widget_is_qwidget(self):
        from spectra.backends.backend_qtagg import SpectraWidget
        from spectra.backends._qt_compat import QWidget
        assert issubclass(SpectraWidget, QWidget)


# ─── Key/button/modifier mapping tests ──────────────────────────────────────

class TestKeyMapping:
    """Tests for Qt → Spectra key/button/modifier translation."""

    def test_qt_button_left(self):
        from spectra.backends.backend_qtagg import _qt_button
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOUSE_LEFT
        assert _qt_button(Qt.LeftButton) == MOUSE_LEFT

    def test_qt_button_right(self):
        from spectra.backends.backend_qtagg import _qt_button
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOUSE_RIGHT
        assert _qt_button(Qt.RightButton) == MOUSE_RIGHT

    def test_qt_button_middle(self):
        from spectra.backends.backend_qtagg import _qt_button
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOUSE_MIDDLE
        assert _qt_button(Qt.MiddleButton) == MOUSE_MIDDLE

    def test_qt_button_unknown(self):
        from spectra.backends.backend_qtagg import _qt_button
        assert _qt_button(0) == 0

    def test_qt_mods_shift(self):
        from spectra.backends.backend_qtagg import _qt_mods
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOD_SHIFT
        assert _qt_mods(Qt.ShiftModifier) == MOD_SHIFT

    def test_qt_mods_control(self):
        from spectra.backends.backend_qtagg import _qt_mods
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOD_CONTROL
        assert _qt_mods(Qt.ControlModifier) == MOD_CONTROL

    def test_qt_mods_alt(self):
        from spectra.backends.backend_qtagg import _qt_mods
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOD_ALT
        assert _qt_mods(Qt.AltModifier) == MOD_ALT

    def test_qt_mods_combined(self):
        from spectra.backends.backend_qtagg import _qt_mods
        from spectra.backends._qt_compat import Qt
        from spectra._embed import MOD_SHIFT, MOD_CONTROL
        assert _qt_mods(Qt.ShiftModifier | Qt.ControlModifier) == (MOD_SHIFT | MOD_CONTROL)

    def test_qt_key_letters(self):
        from spectra.backends.backend_qtagg import _qt_key
        from spectra.backends._qt_compat import Qt
        assert _qt_key(Qt.Key_A) == Qt.Key_A  # A-Z match between Qt and GLFW
        assert _qt_key(Qt.Key_Z) == Qt.Key_Z

    def test_qt_key_numbers(self):
        from spectra.backends.backend_qtagg import _qt_key
        from spectra.backends._qt_compat import Qt
        assert _qt_key(Qt.Key_0) == Qt.Key_0
        assert _qt_key(Qt.Key_9) == Qt.Key_9

    def test_qt_key_escape(self):
        from spectra.backends.backend_qtagg import _qt_key
        from spectra.backends._qt_compat import Qt
        from spectra._embed import KEY_ESCAPE
        assert _qt_key(Qt.Key_Escape) == KEY_ESCAPE

    def test_qt_key_unknown(self):
        from spectra.backends.backend_qtagg import _qt_key
        assert _qt_key(0xFFFF) == 0


# ─── Icon generation tests ──────────────────────────────────────────────────

class TestIcons:
    """Tests for toolbar icon generation."""

    def test_make_icon_returns_qicon(self):
        from spectra.backends.backend_qtagg import _make_icon
        from spectra.backends._qt_compat import QIcon
        icon = _make_icon("home")
        assert isinstance(icon, QIcon)

    def test_make_icon_unknown_name(self):
        from spectra.backends.backend_qtagg import _make_icon
        from spectra.backends._qt_compat import QIcon
        icon = _make_icon("nonexistent_icon")
        assert isinstance(icon, QIcon)

    def test_all_icons_available(self):
        from spectra.backends.backend_qtagg import _ICON_SVG
        expected = {"home", "back", "forward", "pan", "zoom", "save", "grid"}
        assert expected == set(_ICON_SVG.keys())


# ─── Backends package tests ─────────────────────────────────────────────────

class TestBackendsPackage:
    """Tests for the backends package itself."""

    def test_package_importable(self):
        import spectra.backends
        assert spectra.backends is not None

    def test_backend_qtagg_importable(self):
        import spectra.backends.backend_qtagg
        assert spectra.backends.backend_qtagg is not None

    def test_qt_compat_importable(self):
        import spectra.backends._qt_compat
        assert spectra.backends._qt_compat is not None
