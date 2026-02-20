#include <cmath>
#include <spectra/easy.hpp>

int main()
{
    auto& line = spectra::plot().label("live").color(spectra::colors::cyan);
    spectra::ylim(-1.5f, 1.5f);
    spectra::title("Live Streaming Plot");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Signal");

    spectra::on_update(
        [&](float /*dt*/, float t)
        {
            // Append a new point each frame simulating a sensor reading
            float value = std::sin(t * 2.0f) + 0.3f * std::sin(t * 7.0f);
            line.append(t, value);
            // Sliding window: show last 10 seconds
            spectra::xlim(t - 10.0f, t);
        });

    spectra::show();

    return 0;
}
