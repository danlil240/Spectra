#pragma once

#include <string_view>

namespace spectra::ui
{

// Machine-parseable UI action log for automation / fuzz verification.
// Emits SPECTRA_LOG_DEBUG on category "ui.action" with body:
//   kind=<kind> id=<id> result=<ok|miss|...> [detail=<free text>]
void log_ui_action(std::string_view kind,
                   std::string_view id,
                   std::string_view result = "ok",
                   std::string_view detail = {});

#ifdef SPECTRA_USE_IMGUI
// Log ImGui items activated this frame (fallback for raw ImGui::Button sites).
void log_imgui_frame_activations();
#endif

}   // namespace spectra::ui
