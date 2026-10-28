#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include <spectra/figure_registry.hpp>

namespace spectra
{

/// @brief GPU backend used for rendering.
enum class RenderBackend
{
    Vulkan,
    WebGPU,
};

/// @brief Options for constructing an App.
struct AppConfig
{
    /// Render offscreen without opening windows (export, CI).
    bool        headless = false;
    std::string socket_path;   ///< non-empty → multiproc mode; empty → check SPECTRA_SOCKET env
    /// Rendering backend.
    RenderBackend backend = RenderBackend::Vulkan;
    // Start the automation Unix socket + MCP HTTP server (remote UI control).
    // Off by default; can also be enabled with SPECTRA_AUTOMATION=1. See SECURITY.md.
    bool enable_automation = false;

    /// When true, App::run() installs process-wide SIGINT/SIGTERM handlers that
    /// request a graceful exit (App::request_exit()) and ignores SIGPIPE.
    /// Off by default so embedding Spectra never replaces the host's signal
    /// dispositions; the standalone `spectra` executable turns it on.
    bool install_signal_handlers = false;

    /// Theme name: "night", "dark", "light" or "high_contrast".  Empty → "night"
    /// for headless apps, or the user's saved default for windowed apps.  When
    /// set, it overrides the user's settings in both modes.
    std::string theme{};
    /// Data (series colour) palette name, e.g. "default", "colorblind", "tol_bright".
    /// Empty → "default" (the theme's own palette) for headless apps, or the user's
    /// saved default for windowed apps.  When set, it overrides the user's settings.
    std::string data_palette{};
    /// Headless apps ignore the user's settings.json so exports render the same
    /// on every machine.  Set to true to apply the user's saved theme and
    /// palette anyway.  Windowed apps always read the user's settings.
    ///
    /// settings.json (like every per-user Spectra file) lives in Spectra's config
    /// directory: $SPECTRA_CONFIG_DIR if set (used as-is), else
    /// $XDG_CONFIG_HOME/spectra (absolute paths, non-Windows), else
    /// $HOME/.config/spectra.  Point SPECTRA_CONFIG_DIR at an empty directory to
    /// isolate a windowed app from saved user settings.
    bool use_user_settings = false;
};

class SessionRuntime;
class WindowRuntime;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
class WindowManager;
#endif

/// @brief Owns figures, windows and the renderer, and runs the event loop.
///
/// Typical use:
/// @code
/// spectra::App app;
/// auto& fig = app.figure();
/// fig.subplot(1, 1, 1).plot(x, y, "b-");
/// app.run();
/// @endcode
/// The app runs in-process unless AppConfig::socket_path or the SPECTRA_SOCKET environment
/// variable names a `spectra-backend` socket, in which case it runs in multi-process mode.
class App
{
   public:
    /// @brief Create an application; no window opens until run().
    explicit App(const AppConfig& config = {});
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    /// @brief True when the render backend and renderer initialized successfully.
    ///
    /// When false (e.g. no Vulkan driver / GPU available), run() logs an error
    /// and returns immediately without rendering or exporting anything.
    bool ok() const { return initialized_; }

    /// @brief Create a new figure shown in its own window.
    Figure& figure(const FigureConfig& config = {});
    /// @brief Create a new figure shown as a tab next to @p sibling.
    Figure& figure(Figure& sibling);

    /// @brief Run the application until all windows close (blocking).
    ///
    /// Runs in-process, or in multi-process mode when a backend socket is configured.
    ///
    /// Returns immediately (after logging an ERROR) when ok() is false.
    void run();

    // Frame-by-frame control (alternative to run()).
    // init_runtime() performs all setup, step() runs one frame,
    // shutdown_runtime() cleans up.  run_inproc() calls these internally.
    /// @brief Result of a single step() call.
    struct StepResult
    {
        bool     should_exit   = false;
        float    frame_time_ms = 0.0f;
        uint64_t frame_number  = 0;
    };

    /// @brief Set up windows and the renderer for frame-by-frame control.
    void init_runtime();
    /// @brief Process events and render one frame.
    StepResult step();
    /// @brief Tear down what init_runtime() created.
    void shutdown_runtime();

    // Access internals exposed for QA / testing after init_runtime().
    /// @brief UI context (QA/testing; valid after init_runtime()).
    WindowUIContext* ui_context();
    /// @brief Session runtime (QA/testing; valid after init_runtime()).
    SessionRuntime* session();
    /// @brief Registry of all figures owned by this app.
    FigureRegistry& figure_registry() { return registry_; }
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    WindowManager* window_manager();
#endif

    /// @brief True when the app renders offscreen only.
    bool is_headless() const { return config_.headless; }

    /// @brief Request a graceful exit; safe to call from a signal handler.
    ///
    /// Signal-safe: call from SIGINT/SIGTERM handlers to request graceful exit.
    /// The next iteration of the run loop will detect this and break out,
    /// allowing shutdown_runtime() to run.
    static void request_exit();

    // Access internals (for renderer integration)
    /// @brief Rendering backend (advanced integration).
    Backend* backend() { return backend_.get(); }
    /// @brief Renderer (advanced integration).
    Renderer* renderer() { return renderer_.get(); }

    // Knob manager (set by easy API before run(), or by user directly)
    /// @brief Attach an external knob manager (not owned).
    void set_knob_manager(KnobManager* km) { knob_manager_ = km; }
    /// @brief Attached knob manager, or nullptr.
    KnobManager* knob_manager() const { return knob_manager_; }

   private:
    void run_inproc();
    void run_multiproc();
    void render_secondary_window(struct WindowContext* wctx);

    /// Opaque runtime state created by init_runtime(), destroyed by shutdown_runtime().
    struct AppRuntime;
    std::unique_ptr<AppRuntime> runtime_;

    /// Group figures into windows based on sibling relationships.
    /// Returns a vector of groups; each group is a vector of FigureIds
    /// that should share one OS window.
    std::vector<std::vector<FigureId>> compute_window_groups() const;

    AppConfig                         config_;
    bool                              initialized_ = false;
    FigureRegistry                    registry_;
    std::unique_ptr<ui::ThemeManager> theme_mgr_;
    std::unique_ptr<Backend>          backend_;
    std::unique_ptr<Renderer>         renderer_;

    /// Maps a FigureId to the FigureId it should be tabbed next to.
    /// Figures not in this map get their own window.
    std::unordered_map<FigureId, FigureId> sibling_map_;

    /// External knob manager (not owned — set by easy API or user)
    KnobManager* knob_manager_ = nullptr;

    static std::atomic<bool> s_exit_requested_;
};

}   // namespace spectra
