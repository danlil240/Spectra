#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    spectra::figure(1920, 1080);

    // Generate two independent datasets
    constexpr size_t   N = 300;
    std::vector<float> x(N);
    std::vector<float> y1(N);
    std::vector<float> y2(N);

    for (size_t i = 0; i < N; ++i)
    {
        x[i]  = static_cast<float>(i) * 0.02f;
        y1[i] = std::sin(x[i] * 3.0f) * std::exp(-x[i] * 0.3f);
        y2[i] = std::cos(x[i] * 2.0f) + 0.5f * std::sin(x[i] * 7.0f);
    }

    // Top subplot
    spectra::subplot(2, 1, 1);
    spectra::plot(x, y1, "r-").label("temperature");
    spectra::title("Temperature");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Temp (C)");
    spectra::xlim(0.0f, 6.0f);
    spectra::ylim(-1.5f, 1.5f);

    // Bottom subplot
    spectra::subplot(2, 1, 2);
    spectra::plot(x, y2, "b-").label("pressure");
    spectra::title("Pressure");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Pressure (kPa)");
    spectra::xlim(0.0f, 6.0f);
    spectra::ylim(-2.0f, 2.0f);

    spectra::show();

    return 0;
}
