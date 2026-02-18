// multi_window_tabs_demo.cpp
//
// Demonstrates the figure() API:
//   app.figure()      → new OS window
//   app.figure(fig)   → tab next to fig
//
// This creates:
//   Window 1: sin(x), cos(x), tan(x) as tabs
//   Window 2: exp(x) standalone

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <cmath>
#include <vector>

int main()
{
    spectra::App app;

    std::vector<float> x(200);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(i) * 0.05f;

    // ── Window 1 ─────────────────────────────────────────────────

    auto& sin_fig = app.figure();
    {
        auto& ax = sin_fig.subplot(1, 1, 1);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = std::sin(x[i]);
        ax.plot(x, y).label("sin(x)").color({0.2f, 0.6f, 1.0f, 1.0f});
        ax.title("Sine");
        ax.grid(true);
    }

    auto& cos_fig = app.figure(sin_fig);  // tab in sin's window
    {
        auto& ax = cos_fig.subplot(1, 1, 1);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = std::cos(x[i]);
        ax.plot(x, y).label("cos(x)").color({1.0f, 0.4f, 0.2f, 1.0f});
        ax.title("Cosine");
        ax.grid(true);
    }

    auto& tan_fig = app.figure(sin_fig);  // another tab in sin's window
    {
        auto& ax = tan_fig.subplot(1, 1, 1);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = std::tan(x[i]);
        ax.plot(x, y).label("tan(x)").color({0.2f, 0.8f, 0.4f, 1.0f});
        ax.title("Tangent");
        ax.grid(true);
    }

    // ── Window 2 ─────────────────────────────────────────────────

    // auto& exp_fig = app.figure();  // new window
    // {
    //     auto& ax = exp_fig.subplot(1, 1, 1);
    //     std::vector<float> y(x.size());
    //     for (size_t i = 0; i < x.size(); ++i) y[i] = std::exp(x[i] * 0.3f);
    //     ax.plot(x, y).label("exp(0.3x)").color({0.8f, 0.2f, 0.8f, 1.0f});
    //     ax.title("Exponential");
    //     ax.grid(true);
    // }

    app.run();
}
