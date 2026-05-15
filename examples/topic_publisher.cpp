// Example: publish a topic to spectra-backend (works with or without a UI open).
//
// Usage:
//   1. Start a Spectra session in another terminal (e.g. run `spectra` or any
//      example), OR ensure spectra-backend is running with SPECTRA_SOCKET set.
//   2. Run this example. It connects to the daemon and pushes a sine wave at
//      ~50 Hz on topic "demo/sine".
//   3. Open the Topics panel and drag "demo/sine" onto an axes to plot it live.
//
// The example exits after ~30 seconds. Press Ctrl+C to stop earlier.

#include <spectra/topic.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

int main()
{
    spectra::Publisher::Options opts;
    opts.kind = spectra::Publisher::Kind::Scalar2D;
    opts.unit = "";

    auto pub = spectra::Publisher::create("demo/sine", opts);
    if (!pub)
    {
        std::cerr << "Failed to connect / declare topic. Is spectra-backend running?\n";
        return 1;
    }

    std::cout << "Publishing on topic '" << pub->name() << "'. Press Ctrl+C to stop.\n";

    constexpr double kHz       = 50.0;
    constexpr int    kTotal    = static_cast<int>(30.0 * kHz);
    const auto       period_ns = std::chrono::nanoseconds(static_cast<long long>(1e9 / kHz));

    auto next = std::chrono::steady_clock::now();
    for (int i = 0; i < kTotal; ++i)
    {
        double t = i / kHz;
        double y = std::sin(2.0 * M_PI * 1.0 * t);
        if (!pub->publish(t, y))
        {
            std::cerr << "publish failed; exiting\n";
            return 1;
        }
        next += period_ns;
        std::this_thread::sleep_until(next);
    }
    return 0;
}
