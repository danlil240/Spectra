// easy_api_demo.cpp — Demonstrates the Spectra Easy API at every complexity level.
//
// This is the simplest way to use Spectra. One header, zero boilerplate.
// Works identically in inproc and multiproc modes.

#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // ── Generate data ────────────────────────────────────────────────────────
    constexpr int      N = 200;
    std::vector<float> x(N), y_sin(N), y_cos(N), y_tan(N);
    for (int i = 0; i < N; ++i)
    {
        x[i]     = static_cast<float>(i) * 0.05f;
        y_sin[i] = std::sin(x[i]);
        y_cos[i] = std::cos(x[i]);
        y_tan[i] = std::sin(x[i]) * std::exp(-x[i] * 0.2f);
    }

    // ── Window 1: Simple one-liner plot ──────────────────────────────────────
    spectra::figure();
    spectra::plot(x, y_sin, "b-").label("sin(x)");
    spectra::plot(x, y_cos, "r--").label("cos(x)");
    spectra::title("Trigonometric Functions");
    spectra::xlabel("x");
    spectra::ylabel("y");
    spectra::legend();

    // ── Window 2: Subplots ───────────────────────────────────────────────────
    spectra::figure();

    spectra::subplot(2, 1, 1);
    spectra::plot(x, y_sin, "b-").label("sin(x)");
    spectra::title("Sine");
    spectra::ylabel("Amplitude");

    spectra::subplot(2, 1, 2);
    spectra::plot(x, y_tan, "m-.").label("damped sin");
    spectra::title("Damped Sine");
    spectra::xlabel("x");
    spectra::ylabel("Amplitude");

    // ── Window 3: Scatter ────────────────────────────────────────────────────
    spectra::figure();
    std::vector<float> sx(50), sy(50);
    for (int i = 0; i < 50; ++i)
    {
        sx[i] = y_sin[i * 4];
        sy[i] = y_cos[i * 4];
    }
    spectra::scatter(sx, sy).label("Lissajous").color(spectra::rgb(0.2f, 0.8f, 0.4f)).size(8.0f);
    spectra::title("Scatter Plot");
    spectra::xlabel("sin(x)");
    spectra::ylabel("cos(x)");
    spectra::grid(true);

    // ── Show all and enter event loop ────────────────────────────────────────
    spectra::show();

    return 0;
}
