#pragma once

#include <optional>
#include <spectra/axes.hpp>
#include <spectra/color.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

/// @brief A named bundle of figure, axes and series look settings (a "plot style").
///
/// Apply one with Figure::apply_style() (or App::use_style() / spectra::style::use()
/// to make it the default for new figures).  The built-in styles are listed by
/// style::available(): "default", "paper", "talk", "minimal" and "dark-paper".
/// A StyleSheet{} holds the default look, so a custom style can start from one:
/// @code
/// spectra::StyleSheet s = *spectra::style::find("paper");
/// s.line_width = 1.0f;
/// fig.apply_style(s);
/// @endcode
struct StyleSheet
{
    /// Style name ("paper", ...); empty for an unnamed custom style.
    std::string name;

    // ── Figure ──
    /// When true the plot background is @ref background whatever the app theme
    /// (FigureStyle::background_override); when false it follows the theme.
    bool  background_override = false;
    Color background          = colors::white;
    /// Data palette for auto-coloured series (e.g. "colorblind"); empty follows
    /// the app's palette selection (FigureStyle::palette).
    std::string palette;
    /// Legend font size in pixels; 0 uses the default.
    float legend_font_size = 0.0f;

    // ── Axes ──
    bool          grid           = true;    ///< major grid lines
    bool          minor_grid     = true;    ///< minor grid lines (drawn only with the grid)
    bool          minor_ticks    = false;   ///< minor tick marks between the major ones
    bool          despine        = false;   ///< hide the top and right axis lines
    TickDirection tick_direction = TickDirection::Out;
    float         tick_length    = AxisStyle{}.tick_length;
    float         grid_width     = AxisStyle{}.grid_width;
    float         label_size     = AxisStyle{}.label_size;   ///< axis label font size (px)
    float         title_size     = AxisStyle{}.title_size;   ///< axes title font size (px)

    // ── Series (defaults for new series; see AxesBase::apply_style) ──
    float line_width  = 2.0f;
    float marker_size = 6.0f;
};

namespace style
{

/// The built-in style named @p name, or nullptr when there is none.
const StyleSheet* find(std::string_view name);

/// Names of the built-in styles, "default" first.
std::vector<std::string> available();

/// A user-facing label for a style name ("dark-paper" -> "Dark paper").
std::string display_name(std::string_view name);

}   // namespace style

}   // namespace spectra
