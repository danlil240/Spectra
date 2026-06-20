#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"

namespace spectra::ui::shell
{
// Glass-styled dockable panel window (Vision-aligned).
bool begin_panel(const char* title, bool* p_open, ImGuiWindowFlags extra_flags = 0);
void end_panel();

// Push/pop dock-host chrome (tab bar, separators, close buttons).
void push_dock_style();
void pop_dock_style();

// Push/pop table row geometry for data panels (topic monitor, logs, etc.).
void push_data_table_style();
void pop_data_table_style();

}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
