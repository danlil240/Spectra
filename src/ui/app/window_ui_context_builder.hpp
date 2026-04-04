#pragma once

#include <functional>
#include <memory>
#include <spectra/fwd.hpp>

namespace spectra
{

class FigureRegistry;
class SessionRuntime;
class ExportFormatRegistry;
class OverlayRegistry;
class PluginManager;
class SeriesClipboard;

#ifdef SPECTRA_USE_GLFW
class WindowManager;
#endif

struct WindowUIContextBuildOptions
{
    FigureRegistry*   registry          = nullptr;
    ui::ThemeManager* theme_mgr         = nullptr;
    FigureId          initial_figure_id = INVALID_FIGURE_ID;

    Figure**  active_figure    = nullptr;
    FigureId* active_figure_id = nullptr;

    SessionRuntime* session = nullptr;

    PluginManager*        plugin_manager         = nullptr;
    ExportFormatRegistry* export_format_registry = nullptr;
    OverlayRegistry*      overlay_registry       = nullptr;
    SeriesClipboard*      series_clipboard       = nullptr;

#ifdef SPECTRA_USE_GLFW
    WindowManager* window_manager = nullptr;
    uint32_t       window_id      = 0;
#endif

    std::function<void()>             on_window_close_request;
    std::function<void(FigureId, Figure*)> on_figure_closed;

    bool create_imgui_integration = false;
};

std::unique_ptr<WindowUIContext> build_window_ui_context(
    const WindowUIContextBuildOptions& options);

}   // namespace spectra
