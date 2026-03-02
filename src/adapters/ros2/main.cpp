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
    std::signal(SIGINT, sigint_handler);

    // Initialise ROS2, discovery, panels.
    if (!shell.init(argc, argv))
    {
        std::fprintf(stderr, "spectra-ros: failed to initialise ROS2 node '%s'\n",
                     cfg.node_name.c_str());
        return 1;
    }

    std::printf("spectra-ros: node '%s' started.  Ctrl+C to exit.\n",
                cfg.node_name.c_str());
    std::printf("Window title: %s\n", shell.window_title().c_str());

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
    // Headless spin loop — real windowed rendering requires the Spectra App
    // object (GLFW + Vulkan) which is wired in G2.  For now we spin until
    // shutdown is requested, processing ROS2 messages in the background thread.
    // ---------------------------------------------------------------------------
    while (!shell.shutdown_requested())
    {
        // poll() drains ring buffers; safe to call even without a renderer.
        shell.poll();

        // Yield ~16 ms (60 Hz) so we don't burn CPU in a tight loop.
        rclcpp::sleep_for(std::chrono::milliseconds(16));
    }

    std::printf("spectra-ros: shutting down.\n");
    shell.shutdown();
    g_shell = nullptr;
    return 0;
}
