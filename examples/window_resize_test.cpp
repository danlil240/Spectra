#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // Generate sample data
    std::vector<float> x, y;
    for (int i = 0; i < 1000; ++i)
    {
        x.push_back(i * 0.01f);
        y.push_back(std::sin(x[i]) * std::exp(-x[i] * 0.1f));
    }

    spectra::plot(x, y, "b-");
    spectra::grid(true);
    spectra::title("Window Resizing Test");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Amplitude");

    // Show the window - try resizing it!
    spectra::show();

    return 0;
}
