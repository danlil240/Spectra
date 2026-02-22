#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // Create a figure with multiple subplots
    spectra::figure(800, 600);

    // Generate data
    std::vector<float> x(100);
    for (int i = 0; i < 100; ++i)
    {
        x[i] = i * 0.1f;
    }

    // First subplot - sine wave
    spectra::subplot(2, 1, 1);
    std::vector<float> y1(100);
    for (int i = 0; i < 100; ++i)
    {
        y1[i] = std::sin(x[i]);
    }
    spectra::plot(x, y1, "b-").label("sin(x)");
    spectra::title("First Subplot");
    spectra::xlabel("x");
    spectra::ylabel("sin(x)");
    spectra::legend();

    // Second subplot - cosine wave
    spectra::subplot(2, 1, 2);
    std::vector<float> y2(100);
    for (int i = 0; i < 100; ++i)
    {
        y2[i] = std::cos(x[i]);
    }
    spectra::plot(x, y2, "r-").label("cos(x)");
    spectra::title("Second Subplot");
    spectra::xlabel("x");
    spectra::ylabel("cos(x)");
    spectra::legend();

    // Create a second figure with single subplot
    spectra::figure(800, 600);
    std::vector<float> y3(100);
    for (int i = 0; i < 100; ++i)
    {
        y3[i] = std::sin(x[i]) * std::cos(x[i]);
    }
    spectra::plot(x, y3, "g-").label("sin(x)*cos(x)");
    spectra::title("Single Subplot");
    spectra::xlabel("x");
    spectra::ylabel("sin(x)*cos(x)");
    spectra::legend();

    spectra::show();

    return 0;
}
