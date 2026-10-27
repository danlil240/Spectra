#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace spectra
{

// Defaults shared by every path that opens a Spectra window: the in-process App,
// the multi-process window agent (spectra-window), the backend daemon
// (spectra-backend), the easy API and the Python bindings.  Keeping them in one
// place makes both runtime modes open identical-looking windows.

/// Default width of a new figure window, in pixels.
inline constexpr uint32_t DEFAULT_WINDOW_WIDTH = 1500;
/// Default height of a new figure window, in pixels.
inline constexpr uint32_t DEFAULT_WINDOW_HEIGHT = 900;

/// @brief Default tab/window title for the figure at 1-based position @p number.
/// @return "Figure 1", "Figure 2", ...
inline std::string default_figure_title(std::size_t number)
{
    return "Figure " + std::to_string(number);
}

}   // namespace spectra
