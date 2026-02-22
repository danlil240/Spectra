#include <cmath>
#include <random>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // ── Window 1: Trigonometric Functions ─────────────────────────────────
    spectra::figure();

    std::vector<float> x, y1, y2;
    for (int i = 0; i < 100; ++i)
    {
        float t = i * 0.1f;
        x.push_back(t);
        y1.push_back(std::sin(t));
        y2.push_back(std::cos(t));
    }

    spectra::plot(x, y1, "b-").label("Sine Wave");
    spectra::plot(x, y2, "r-").label("Cosine Wave");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Amplitude");
    spectra::title("Trigonometric Functions");
    spectra::grid(true);
    spectra::legend();

    // ── Window 2: Scatter plot ───────────────────────────────────────────
    spectra::figure();

    std::vector<float>              x_scatter, y_scatter;
    std::mt19937                    gen(42);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < 200; ++i)
    {
        x_scatter.push_back(dist(gen));
        y_scatter.push_back(dist(gen));
    }

    spectra::scatter(x_scatter, y_scatter)
        .label("Random Points")
        .color(spectra::rgb(0.2f, 0.8f, 0.4f));
    spectra::xlabel("X Value");
    spectra::ylabel("Y Value");
    spectra::title("2D Normal Distribution");
    spectra::grid(true);

    // ── Window 3: Subplots ───────────────────────────────────────────────
    spectra::figure(1280, 960);

    std::vector<float> x3, y3, y4;
    for (int i = 0; i < 50; ++i)
    {
        float t = i * 0.2f;
        x3.push_back(t);
        y3.push_back(std::sin(t) * std::exp(-t * 0.1f));
        y4.push_back(std::cos(t) * std::exp(-t * 0.1f));
    }

    spectra::subplot(2, 1, 1);
    spectra::plot(x3, y3, "m-");
    spectra::title("Damped Sine");
    spectra::grid(true);

    spectra::subplot(2, 1, 2);
    spectra::plot(x3, y4, "c-");
    spectra::title("Damped Cosine");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Amplitude");
    spectra::grid(true);

    // ── Window 4: 10K points ─────────────────────────────────────────────
    spectra::figure();

    std::vector<float> x_large, y_large;
    const int          N = 10000;
    for (int i = 0; i < N; ++i)
    {
        float t = i * 0.01f;
        x_large.push_back(t);
        y_large.push_back(std::sin(t * 0.5f) + std::sin(t * 1.3f) * 0.3f
                          + std::sin(t * 2.7f) * 0.1f);
    }

    spectra::plot(x_large, y_large, "k-").label("Complex Waveform");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Amplitude");
    spectra::title("10K Point Performance Test");
    spectra::grid(true);

    spectra::show();

    return 0;
}
