#pragma once

struct GLFWwindow;

namespace spectra
{

class WindowManager;

// Win32-only: track WM_ENTERSIZEMOVE and render while the OS modal loop blocks
// the main Spectra tick (title-bar move / live edge resize).
#ifdef _WIN32
void install_win32_interactive_hook(GLFWwindow* window, WindowManager* mgr);
void remove_win32_interactive_hook(GLFWwindow* window);
bool win32_window_in_size_move(GLFWwindow* window);
// Ask DWM for a dark (or light) caption so the title bar matches the theme
// (Windows 10 1809+; a no-op where the attribute is unsupported).
void win32_set_dark_title_bar(GLFWwindow* window, bool dark);
#else
inline void install_win32_interactive_hook(GLFWwindow*, WindowManager*) {}
inline void remove_win32_interactive_hook(GLFWwindow*) {}
inline bool win32_window_in_size_move(GLFWwindow*)
{
    return false;
}
inline void win32_set_dark_title_bar(GLFWwindow*, bool) {}
#endif

}   // namespace spectra
