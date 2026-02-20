#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // Generate a sine wave
    std::vector<float> x(200);
    std::vector<float> y(200);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i) * 0.05f;
        y[i] = std::sin(x[i]);
    }

    spectra::plot(x, y, "b-").label("sin(x)");
    spectra::xlim(0.0f, 10.0f);
    spectra::ylim(-1.5f, 1.5f);
    spectra::title("Basic Line Plot");
    spectra::xlabel("X Axis");
    spectra::ylabel("Y Axis");
    spectra::legend();

    spectra::show();

    return 0;
}
