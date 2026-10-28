#pragma once

#include <QtGui/QColor>
#include <QtGui/QPalette>

#include "ui/theme/theme.hpp"

namespace spectra::adapters::qt
{

// Convert a Spectra theme colour (0..1 floats) to a QColor.
QColor to_qcolor(const ui::Color& c);

// Build a QPalette whose roles follow the Spectra theme, so native Qt chrome around an
// embedded canvas (QMainWindow, QStatusBar, menus, splitters, ...) matches the plot UI:
// Window/Base come from the theme backgrounds, text roles from the theme text colours,
// Highlight from the accent.  Disabled roles use the tertiary text colour.
QPalette palette_from_theme(const ui::ThemeColors& colors);

}   // namespace spectra::adapters::qt
