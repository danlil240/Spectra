#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    constexpr size_t N = 100;
    std::vector<float> x(N);
    std::vector<float> y(N);

    // Initial positions on a circle
    for (size_t i = 0; i < N; ++i)
    {
        float angle = static_cast<float>(i) / static_cast<float>(N) * 6.2832f;
        x[i] = std::cos(angle);
        y[i] = std::sin(angle);
    }

    auto& sc = spectra::scatter(x, y).color(spectra::rgb(1.0f, 0.4f, 0.0f)).size(6.0f);

    spectra::xlim(-2.0f, 2.0f);
    spectra::ylim(-2.0f, 2.0f);
    spectra::title("Animated Scatter");
    spectra::xlabel("X");
    spectra::ylabel("Y");

    spectra::on_update(
        [&](float /*dt*/, float t)
        {
            for (size_t i = 0; i < N; ++i)
            {
                float angle = static_cast<float>(i) / static_cast<float>(N) * 6.2832f;
                float r = 1.0f + 0.5f * std::sin(t * 2.0f + angle);
                x[i] = r * std::cos(angle + t * 0.5f);
                y[i] = r * std::sin(angle + t * 0.5f);
            }
            sc.set_x(x);
            sc.set_y(y);
        });

    spectra::show();

    return 0;
}
