// spectra-ros — standalone ROS2 visualization / debugging application (G1).
//
// Default layout:
//   - Topic monitor panel  (left dock)
//   - Plot area             (center)
//   - Statistics overlay    (right dock)
//   - Topic echo panel      (bottom dock)
//
// CLI:
//   --topics TOPIC[:FIELD] ...   subscribe and plot on launch
//   --bag    FILE                open a bag file on launch (Phase D)
//   --layout default|plot-only|monitor
//   --window-s SECONDS           auto-scroll time window (default 30)
//   --node-name NAME             ROS2 node name (default spectra_ros)
//   --rows N                     subplot grid rows (default 4)
//   --cols N                     subplot grid cols (default 1)
//
// SIGINT terminates cleanly: bridge shuts down, Vulkan resources freed.

#include "ros2_adapter.hpp"
#include "ros_app_shell.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>

#ifdef SPECTRA_USE_IMGUI
#include "ui/app/window_ui_context.hpp"
#endif

#include <csignal>
#include <cstdio>

// ---------------------------------------------------------------------------
// Global shutdown flag set by the SIGINT handler
// ---------------------------------------------------------------------------

static spectra::adapters::ros2::RosAppShell* g_shell = nullptr;

static void sigint_handler(int /*sig*/)
{
    if (g_shell) g_shell->request_shutdown();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace spectra::adapters::ros2;

    // Parse CLI args.
    std::string err;
    const RosAppConfig cfg = parse_args(argc, argv, err);

    if (!err.empty())
    {
        // "--help" writes usage into err; print and exit 0.
        // Any real parse error is also printed here and exits 1.
        const bool is_help = (err.find("Usage:") == 0);
        std::fputs(err.c_str(), is_help ? stdout : stderr);
        std::fputc('\n', is_help ? stdout : stderr);
        return is_help ? 0 : 1;
    }

    // Print version banner.
    std::printf("spectra-ros %s  |  layout: %s  |  node: %s\n",
                adapter_version(),
                layout_mode_name(cfg.layout),
                cfg.node_name.c_str());

    // Create shell and install SIGINT handler before init.
    RosAppShell shell(cfg);
    g_shell = &shell;
    std::signal(SIGINT,  sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    // Initialise ROS2, discovery, panels.
    if (!shell.init(argc, argv))
    {
        std::fprintf(stderr, "spectra-ros: failed to initialise ROS2 node '%s'\n",
                     cfg.node_name.c_str());
        return 1;
    }

    std::printf("spectra-ros: node '%s' started.  Ctrl+C to exit.\n",
                cfg.node_name.c_str());

    if (!cfg.initial_topics.empty())
    {
        std::printf("Initial topics: ");
        for (size_t i = 0; i < cfg.initial_topics.size(); ++i)
        {
            std::printf("%s%s",
                        cfg.initial_topics[i].c_str(),
                        i + 1 < cfg.initial_topics.size() ? ", " : "\n");
        }
    }

    // ---------------------------------------------------------------------------
    // Create Spectra App with GLFW + Vulkan + ImGui windowed rendering.
    // ---------------------------------------------------------------------------

    spectra::AppConfig app_cfg;
    spectra::App app(app_cfg);

    // Create a placeholder figure so the App has something to render.
    // Add a dummy subplot so the "Welcome to Spectra" page is suppressed —
    // the ROS2 shell owns the entire UI layout.
    spectra::FigureConfig fig_cfg;
    fig_cfg.width  = cfg.window_width;
    fig_cfg.height = cfg.window_height;
    auto& fig = app.figure(fig_cfg);
    fig.subplot(1, 1, 1);  // suppress welcome page

    // Set up a perpetual animation callback so the render loop stays active
    // and we can poll ROS2 messages each frame.
    fig.animate()
        .fps(60.0f)
        .on_frame([&shell](spectra::Frame& /*frame*/)
        {
            // Drain ROS2 ring buffers each frame.
            shell.poll();
        })
        .loop(true)
        .play();

    // ---------------------------------------------------------------------------
    // Initialise the Spectra rendering runtime (GLFW window, Vulkan, ImGui).
    // ---------------------------------------------------------------------------
    app.init_runtime();

#ifdef SPECTRA_USE_IMGUI
    // Hide Spectra's default chrome — the ROS2 shell provides its own menu bar,
    // status bar, and panel layout.  Keep only the bare canvas.
    auto* ui_ctx = app.ui_context();
    if (ui_ctx && ui_ctx->imgui_ui)
    {
        auto& lm = ui_ctx->imgui_ui->get_layout_manager();
        lm.set_inspector_visible(false);
        lm.set_tab_bar_visible(false);
        ui_ctx->imgui_ui->set_extra_draw_callback([&shell]()
        {
            shell.draw();
        });
    }
#endif

    // ---------------------------------------------------------------------------
    // Render loop — runs until window is closed or SIGINT received.
    // ---------------------------------------------------------------------------
    for (;;)
    {
        if (shell.shutdown_requested())
            break;

        auto result = app.step();
        if (result.should_exit)
            break;
    }

    // ---------------------------------------------------------------------------
    // Clean shutdown: disconnect ImGui callback, tear down Vulkan, then ROS2.
    // Order matters: the animation callback references shell.poll(), so the
    // Spectra runtime must be destroyed before the ROS2 bridge.
    // ---------------------------------------------------------------------------
    std::printf("spectra-ros: shutting down.\n");

#ifdef SPECTRA_USE_IMGUI
    // Disconnect ROS2 draw callback before tearing down the render loop.
    if (ui_ctx && ui_ctx->imgui_ui)
        ui_ctx->imgui_ui->set_extra_draw_callback(nullptr);
#endif

    app.shutdown_runtime();
    shell.shutdown();
    g_shell = nullptr;
    return 0;
}
