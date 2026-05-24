#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // Generate a sine wave
    std::vector<float> x1(200);
    std::vector<float> y1(200);

    std::vector<float> x2(200);
    std::vector<float> y2(200);

    std::vector<float> x3(200);
    std::vector<float> y3(200);

    for (size_t i = 0; i < x1.size(); ++i)
    {
        x1[i] = static_cast<float>(i) * 0.05f;
        y1[i] = std::sin(x1[i]);

        x2[i] = static_cast<float>(i) * 0.05f;
        y2[i] = std::cos(x2[i]);

        x3[i] = static_cast<float>(i) * 0.05f;
        y3[i] = std::tan(x3[i]);
    }

    spectra::plot(x1, y1).label("sin(x)");
    spectra::plot(x2, y2).label("cos(x)");
    spectra::plot(x3, y3).label("tan(x)");

    spectra::xlim(0.0f, 10.0f);
    spectra::ylim(-1.5f, 1.5f);
    spectra::title("Basic Line Plot");
    spectra::xlabel("X Axis");
    spectra::ylabel("Y Axis");
    spectra::legend();

    spectra::show();

    return 0;
}
