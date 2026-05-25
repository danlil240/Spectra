// Example: publish a topic to spectra-backend (works with or without a UI open).
//
// Usage:
//   1. Run this example. If Spectra is not open yet, samples are dropped while
//      the publisher retries in the background.
//   2. Start a Spectra session in another terminal (e.g. run `spectra` or any
//      example), OR ensure spectra-backend is running with SPECTRA_SOCKET set.
//   3. Open the Topics panel and drag "demo/sine" onto an axes to plot it live.
//
// Docker publisher-first workflow:
//   docker run --rm --user "$(id -u):$(id -g)"
//     -v "$XDG_RUNTIME_DIR:$XDG_RUNTIME_DIR"
//     -e XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR"
//     spectra-topic-publisher:local
//   Then run `spectra` on the host. Do not set SPECTRA_SOCKET for this mode;
//   leaving it unset lets the publisher discover the new spectra-*.sock.
//
// The example exits after ~30 seconds. Press Ctrl+C to stop earlier.

#include <spectra/topic.hpp>
#include <spectra/logger.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

int main()
{
    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());

    spectra::Publisher::Options opts;
    opts.kind = spectra::Publisher::Kind::Scalar2D;
    opts.unit = "";

    auto pub = spectra::Publisher::create("demo/sine", opts);
    if (!pub)
    {
        SPECTRA_LOG_ERROR("publisher", "Failed to create publisher. Is the topic name valid?");
        return 1;
    }

    SPECTRA_LOG_INFO("publisher",
                     "Publishing topic '{}' at 50 Hz for 30 seconds",
                     pub->name());
    if (const char* pinned = std::getenv("SPECTRA_SOCKET"); pinned && *pinned)
    {
        SPECTRA_LOG_INFO("publisher", "Using pinned socket: {}", pinned);
    }
    else
    {
        const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
        SPECTRA_LOG_INFO("publisher",
                         "Waiting for Spectra in {} (auto-discovery enabled)",
                         (runtime_dir && *runtime_dir) ? runtime_dir : "/tmp");
    }

    bool was_connected = pub->is_connected();
    if (was_connected)
        SPECTRA_LOG_INFO("publisher", "Connected to Spectra");

    constexpr double kHz       = 50.0;
    constexpr int    kTotal    = static_cast<int>(30.0 * kHz);
    const auto       period_ns = std::chrono::nanoseconds(static_cast<long long>(1e9 / kHz));

    auto next = std::chrono::steady_clock::now();
    for (int i = 0; i < kTotal; ++i)
    {
        double t = i / kHz;
        double y = std::sin(2.0 * M_PI * 1.0 * t);
        // publish() drops samples silently while Spectra is closed and
        // auto-reconnects when it returns, so we don't treat a single
        // failure as fatal.
        pub->publish(t, y);
        bool connected = pub->is_connected();
        if (connected != was_connected)
        {
            if (connected)
                SPECTRA_LOG_INFO("publisher", "Connected to Spectra");
            else
                SPECTRA_LOG_INFO("publisher", "Spectra disconnected; waiting to reconnect");
            was_connected = connected;
        }
        next += period_ns;
        std::this_thread::sleep_until(next);
    }
    return 0;
}
