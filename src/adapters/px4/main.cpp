// spectra-px4 — standalone PX4 ULog visualizer / real-time inspection tool.
//
// Modes:
//   1. Offline: load a .ulg file, browse topics, plot fields
//   2. Real-time: connect via MAVLink UDP, live telemetry plotting
//
// CLI:
//   --ulog FILE       open a ULog file on launch
//   --host HOST       MAVLink UDP host (default 127.0.0.1)
//   --port PORT       MAVLink UDP port (default 14540)
//   --connect         auto-connect to MAVLink on launch
//   --window-s SEC    real-time time window (default 30)
//
// Examples:
//   spectra-px4 flight.ulg                    # open log file
//   spectra-px4 --connect --port 14540        # live SITL inspection
//   spectra-px4 --ulog log.ulg --connect      # both modes simultaneously

#include "px4_adapter.hpp"
#include "px4_app_shell.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>

#ifdef SPECTRA_USE_IMGUI
    #include "ui/app/window_ui_context.hpp"
#endif

#include <csignal>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Global shutdown flag
// ---------------------------------------------------------------------------

static spectra::adapters::px4::Px4AppShell* g_shell = nullptr;

static void sigint_handler(int /*sig*/)
{
    if (g_shell)
        g_shell->request_shutdown();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace spectra::adapters::px4;

    // Parse CLI args.
    std::string        err;
    const Px4AppConfig cfg = parse_px4_args(argc, argv, err);

    if (!err.empty())
    {
        const bool is_help = (err.find("Usage:") == 0);
        std::fputs(err.c_str(), is_help ? stdout : stderr);
        std::fputc('\n', is_help ? stdout : stderr);
        return is_help ? 0 : 1;
    }

    // Print version banner.
    std::printf("spectra-px4 %s\n", adapter_version());

    // Create Spectra App.
    spectra::AppConfig app_cfg;
    spectra::App       app(app_cfg);

    spectra::FigureConfig fig_cfg;
    fig_cfg.width  = cfg.window_width;
    fig_cfg.height = cfg.window_height;
    auto& fig      = app.figure(fig_cfg);

    // Create shell.
    Px4AppShell shell(cfg);
    shell.set_canvas_figure(&fig);
    g_shell = &shell;
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    if (!shell.init())
    {
        std::fprintf(stderr, "spectra-px4: failed to initialise\n");
        return 1;
    }

    std::printf("spectra-px4: ready.  Ctrl+C to exit.\n");

    // Animation loop.
    fig.animate()
        .fps(60.0f)
        .on_frame([&shell](spectra::Frame& /*frame*/) { shell.poll(); })
        .loop(true)
        .play();

    app.init_runtime();

#ifdef SPECTRA_USE_IMGUI
    auto* ui_ctx = app.ui_context();
    if (ui_ctx && ui_ctx->imgui_ui)
    {
        ui_ctx->imgui_ui->enable_docking();
        ui_ctx->imgui_ui->set_extra_draw_callback([&shell]() { shell.draw(); });
    }

    // Wire WindowManager so panels can create real OS windows on detach.
    if (auto* wm = app.window_manager())
        shell.set_window_manager(wm);
#endif

    // Render loop.
    for (;;)
    {
        if (shell.shutdown_requested())
            break;
        auto result = app.step();
        shell.process_pending_panels();
        if (result.should_exit)
            break;
    }

    // Clean shutdown.
    std::printf("spectra-px4: shutting down.\n");

#ifdef SPECTRA_USE_IMGUI
    {
        auto* ctx = app.ui_context();
        if (ctx && ctx->imgui_ui)
            ctx->imgui_ui->set_extra_draw_callback(nullptr);
    }
#endif

    app.shutdown_runtime();
    shell.shutdown();
    g_shell = nullptr;
    return 0;
}
