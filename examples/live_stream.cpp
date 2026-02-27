#include <cmath>
#include <spectra/easy.hpp>

int main()
{
    auto& line = spectra::plot().label("live").color(spectra::colors::cyan);
    spectra::presented_buffer(10.0f);
    spectra::title("Live Streaming Plot");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Signal");

    spectra::on_update(
        [&](float /*dt*/, float t)
        {
            // Append a new point each frame simulating a sensor reading
            float value = std::sin(t * 2.0f) + 0.3f * std::sin(t * 7.0f);
            line.append(t, value);
        });

    spectra::show();

    return 0;
}
