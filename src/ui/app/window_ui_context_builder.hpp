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

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
class WindowManager;
#endif

namespace ui::settings
{
class SettingsStore;
}

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

    ui::settings::SettingsStore* settings_store = nullptr;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    WindowManager* window_manager = nullptr;
    uint32_t       window_id      = 0;
#endif

    std::function<void()>                  on_window_close_request;
    std::function<void(FigureId, Figure*)> on_figure_closed;

    bool create_imgui_integration = false;

    // When true, creates a minimal headless-safe context with only
    // FigureManager and ThemeManager — skips commands, shortcuts, input,
    // animation, and ImGui-dependent objects to avoid destruction-order
    // issues in rapid create/destroy cycles (e.g. golden tests).
    bool headless = false;
};

std::unique_ptr<WindowUIContext> build_window_ui_context(
    const WindowUIContextBuildOptions& options);

}   // namespace spectra
