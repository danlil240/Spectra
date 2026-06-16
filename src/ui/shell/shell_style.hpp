#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"

namespace spectra::ui::shell
{
// Glass-styled dockable panel window (Vision-aligned).
bool begin_panel(const char* title, bool* p_open, ImGuiWindowFlags extra_flags = 0);
void end_panel();
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
