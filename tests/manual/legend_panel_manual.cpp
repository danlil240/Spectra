#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // Create a figure with multiple series to test legend
    spectra::figure(800, 600);

    // Generate data
    std::vector<float> x(100);
    for (int i = 0; i < 100; ++i)
    {
        x[i] = i * 0.1f;
    }

    // Create multiple series with different colors and labels
    std::vector<float> y1(100), y2(100), y3(100);
    for (int i = 0; i < 100; ++i)
    {
        y1[i] = std::sin(x[i]);
        y2[i] = std::cos(x[i]);
        y3[i] = std::sin(x[i]) * 0.5f + std::cos(x[i]) * 0.5f;
    }

    spectra::plot(x, y1, "b-").label("sin(x)");
    spectra::plot(x, y2, "r-").label("cos(x)");
    spectra::plot(x, y3, "g-").label("combined");

    spectra::title("Legend Panel Test");
    spectra::xlabel("x");
    spectra::ylabel("y");
    spectra::legend();
    spectra::grid(true);

    spectra::show();

    return 0;
}
