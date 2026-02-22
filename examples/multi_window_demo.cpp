// Multi-window demo for Spectra.
//
// Day 0 scaffolding: This demo currently runs in single-window mode with
// multiple figures. As Agents A–D merge their work, this demo will be
// updated to showcase true multi-OS-window rendering.
//
// Phase 0 (current):  Multiple figures in one headless App
// Phase 2 (Agent B):  Multiple OS windows via WindowManager
// Phase 3 (Agent C):  Move figures between windows
// Phase 4 (Agent D):  Tab tear-off UX

#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

using namespace spectra;

int main()
{
    // Generate sample data
    const size_t       N = 500;
    std::vector<float> x(N), y_sin(N), y_cos(N), y_exp(N);
    for (size_t i = 0; i < N; ++i)
    {
        float t  = static_cast<float>(i) / static_cast<float>(N) * 4.0f * 3.14159f;
        x[i]     = t;
        y_sin[i] = std::sin(t);
        y_cos[i] = std::cos(t);
        y_exp[i] = std::exp(-t * 0.2f) * std::sin(t * 3.0f);
    }

    App app;

    // Figure 1: Sine and Cosine
    {
        auto& fig = app.figure({.width = 800, .height = 600});
        auto& ax  = fig.subplot(1, 1, 1);
        ax.line(x, y_sin).label("sin(t)").color({0.2f, 0.5f, 1.0f, 1.0f});
        ax.line(x, y_cos).label("cos(t)").color({1.0f, 0.3f, 0.3f, 1.0f});
        ax.title("Trigonometric Functions");
        ax.xlabel("t (radians)");
        ax.ylabel("Amplitude");
    }

    // Figure 2: Damped oscillation
    {
        auto& fig = app.figure({.width = 800, .height = 600});
        auto& ax  = fig.subplot(1, 1, 1);
        ax.line(x, y_exp).label("damped").color({0.1f, 0.8f, 0.3f, 1.0f});
        ax.title("Damped Oscillation");
        ax.xlabel("t (radians)");
        ax.ylabel("Amplitude");
    }

    // Figure 3: 2x1 subplot with scatter
    {
        auto& fig = app.figure({.width = 1000, .height = 500});

        auto& ax1 = fig.subplot(1, 2, 1);
        ax1.scatter(x, y_sin).label("sin scatter");
        ax1.title("Scatter: sin(t)");

        auto& ax2 = fig.subplot(1, 2, 2);
        ax2.scatter(x, y_cos).label("cos scatter");
        ax2.title("Scatter: cos(t)");
    }

    // TODO(Agent B): Replace with WindowManager-based multi-window:
    //   auto* win1 = window_mgr.create_window(800, 600, "Trig Functions");
    //   auto* win2 = window_mgr.create_window(800, 600, "Damped Oscillation");
    //   auto* win3 = window_mgr.create_window(1000, 500, "Scatter Plots");

    // TODO(Agent C): Demonstrate figure move:
    //   window_mgr.move_figure(fig2_id, win1->id, win2->id);

    // TODO(Agent D): Demonstrate programmatic detach:
    //   window_mgr.detach_figure(fig3_id, win1->id, screen_x, screen_y);

    app.run();

    return 0;
}
