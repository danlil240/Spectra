#pragma once

#include <spectra/fwd.hpp>

namespace spectra
{

class FigureRegistry;
class SessionRuntime;
struct WindowUIContext;

#ifdef SPECTRA_USE_GLFW
class WindowManager;
#endif

// App-level bindings needed by command lambdas.
// Pointers-to-pointers for active_figure / active_figure_id because
// they change every frame and the lambdas must always read the latest value.
struct CommandBindings
{
    WindowUIContext* ui_ctx           = nullptr;
    FigureRegistry*  registry         = nullptr;
    Figure**         active_figure    = nullptr;   // updated per-frame
    FigureId*        active_figure_id = nullptr;   // updated per-frame
    SessionRuntime*  session          = nullptr;
#ifdef SPECTRA_USE_GLFW
    WindowManager* window_mgr = nullptr;
#endif
};

// Register the full set of standard commands (view, edit, file, figure,
// animation, theme, panel, tools, split, new-window) into the
// CommandRegistry / ShortcutManager owned by ui_ctx.
// Both app_inproc and spectra-window agent call this so the UI is identical.
void register_standard_commands(const CommandBindings& bindings);

}   // namespace spectra
