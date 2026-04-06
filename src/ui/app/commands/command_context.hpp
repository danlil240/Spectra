// command_context.hpp — Shared context passed to command descriptor factories.
// Captures all the dependencies that command lambdas need, replacing scattered
// reference captures with a single, well-typed context object.

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

namespace ui
{
class ThemeManager;
}

// All the context a command factory needs to build its descriptors.
// Mirrors CommandBindings closely but uses references where possible
// and is designed for the per-group factory functions.
struct CommandContext
{
    WindowUIContext& ui_ctx;
    FigureRegistry&  registry;
    Figure**         active_figure;      // pointer-to-pointer, updated per-frame
    FigureId*        active_figure_id;   // pointer, updated per-frame
    SessionRuntime*  session = nullptr;
#ifdef SPECTRA_USE_GLFW
    WindowManager* window_mgr = nullptr;
#endif
};

}   // namespace spectra
