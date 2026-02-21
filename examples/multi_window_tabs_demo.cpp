// multi_window_tabs_demo.cpp
//
// Demonstrates the easy API tab control:
//   spectra::figure()  → new OS window
//   spectra::tab()     → new tab in current window
//
// This creates:
//   Window 1: sin(x), cos(x), tan(x) as tabs
//   Window 2: exp(x) standalone

#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    std::vector<float> x(200);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(i) * 0.05f;

    // ── Window 1, Tab 1: Sine ────────────────────────────────────
    spectra::figure();
    {
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = std::sin(x[i]);
        spectra::plot(x, y, "b-").label("sin(x)");
        spectra::title("Sine");
        spectra::grid(true);
    }

    // ── Window 1, Tab 2: Cosine (tab in same window) ─────────────
    spectra::tab();
    {
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = std::cos(x[i]);
        spectra::plot(x, y, "r-").label("cos(x)");
        spectra::title("Cosine");
        spectra::grid(true);
    }

    // ── Window 1, Tab 3: Tangent (tab in same window) ────────────
    spectra::tab();
    {
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = std::tan(x[i]);
        spectra::plot(x, y, "g-").label("tan(x)");
        spectra::title("Tangent");
        spectra::grid(true);
    }

    // ── Window 2: Exponential (new OS window) ────────────────────
    spectra::figure();
    {
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = std::exp(x[i] * 0.3f);
        spectra::plot(x, y, "m-").label("exp(0.3x)");
        spectra::title("Exponential");
        spectra::grid(true);
    }

    spectra::show();
}
