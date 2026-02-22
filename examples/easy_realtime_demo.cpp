// easy_realtime_demo.cpp — Real-time multi-signal sensor simulation.
//
// Demonstrates:
//   - Multiple live signals on the same axes
//   - Sliding time window (last 10 seconds)
//   - Tabbed figures: raw signals + frequency analysis
//   - on_update() for frame-by-frame data streaming
//
// This is the pattern for live sensor dashboards, oscilloscopes,
// telemetry viewers, and simulation monitors.

#include <algorithm>
#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // ── Window: Sensor Dashboard ─────────────────────────────────────────
    // Tab 1: Raw sensor signals (time domain)
    spectra::figure();

    auto& temp = spectra::plot().label("Temperature (°C)").color(spectra::colors::red);
    auto& press = spectra::plot().label("Pressure (kPa)").color(spectra::colors::blue);
    auto& vibr = spectra::plot().label("Vibration (g)").color(spectra::colors::green);

    spectra::ylim(-3.0f, 3.0f);
    spectra::title("Live Sensor Dashboard");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Normalized Value");
    spectra::grid(true);
    spectra::legend();

    // Save a pointer to the live axes so we can update xlim from the callback
    auto* live_ax = spectra::gca();

    // Tab 2: Histogram / envelope (computed from recent data)
    spectra::tab();

    constexpr int HIST_N = 100;
    std::vector<float> hist_x(HIST_N), hist_y(HIST_N, 0.0f);
    for (int i = 0; i < HIST_N; ++i)
        hist_x[i] = -3.0f + 6.0f * static_cast<float>(i) / (HIST_N - 1);

    auto& envelope = spectra::plot(hist_x, hist_y, "m-").label("Signal Envelope");

    spectra::xlim(-3.0f, 3.0f);
    spectra::ylim(0.0f, 1.0f);
    spectra::title("Signal Distribution (last 5s)");
    spectra::xlabel("Value");
    spectra::ylabel("Density");
    spectra::grid(true);

    // Ring buffer for recent temperature values (for histogram)
    constexpr int RING_SIZE = 300;  // ~5 seconds at 60 FPS
    std::vector<float> recent_temp(RING_SIZE, 0.0f);
    int ring_idx = 0;

    // ── Real-time update at 60 FPS ───────────────────────────────────────
    spectra::on_update(
        [&](float /*dt*/, float t)
        {
            // Simulate three sensor readings
            float t_val =
                std::sin(t * 0.8f) + 0.2f * std::sin(t * 5.3f) + 0.1f * std::sin(t * 13.7f);
            float p_val = std::cos(t * 0.5f) + 0.4f * std::sin(t * 3.1f);
            float v_val = 0.5f * std::sin(t * 4.0f) * std::exp(-std::fmod(t, 3.0f) * 0.5f);

            // Append to live traces
            temp.append(t, t_val);
            press.append(t, p_val);
            vibr.append(t, v_val);

            // Sliding window on the live axes
            live_ax->xlim(t - 10.0f, t);

            // Store in ring buffer
            recent_temp[ring_idx % RING_SIZE] = t_val;
            ring_idx++;

            // Update histogram every 10 frames
            if (ring_idx % 10 == 0)
            {
                // Simple histogram
                std::fill(hist_y.begin(), hist_y.end(), 0.0f);
                int count = std::min(ring_idx, RING_SIZE);
                float bin_width = 6.0f / HIST_N;
                for (int i = 0; i < count; ++i)
                {
                    int bin = static_cast<int>((recent_temp[i] + 3.0f) / bin_width);
                    if (bin >= 0 && bin < HIST_N)
                        hist_y[bin] += 1.0f;
                }
                // Normalize
                float max_val = 1.0f;
                for (float v : hist_y)
                    max_val = std::max(max_val, v);
                for (float& v : hist_y)
                    v /= max_val;
                envelope.set_y(hist_y);
            }
        });

    spectra::show();

    return 0;
}
