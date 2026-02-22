// knob_demo.cpp — Interactive parameter knobs
//
// Knobs appear as a floating "PARAMETERS" panel on the plot.
// Drag the sliders to change frequency, amplitude, and phase in real-time.

#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    constexpr int      N = 512;
    std::vector<float> x(N), y(N);

    for (int i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) / (N - 1) * 4.0f * 3.14159f;

    // Define knobs — appear as interactive sliders on the plot
    auto& freq  = spectra::knob("Frequency", 1.0f, 0.1f, 8.0f);
    auto& amp   = spectra::knob("Amplitude", 1.0f, 0.0f, 3.0f);
    auto& phase = spectra::knob("Phase", 0.0f, 0.0f, 6.283f);
    auto& decay = spectra::knob("Decay", 0.0f, 0.0f, 1.0f);

    // Compute initial waveform
    for (int i = 0; i < N; ++i)
        y[i] = amp.value * std::sin(freq.value * x[i]) * std::exp(-decay.value * x[i]);

    auto& line = spectra::plot(x, y);
    spectra::title("Knob Demo");
    spectra::xlabel("t");
    spectra::ylabel("y");
    spectra::ylim(-3.5f, 3.5f);

    spectra::on_update(
        [&](float /*dt*/, float /*t*/)
        {
            for (int i = 0; i < N; ++i)
                y[i] = amp.value * std::sin(freq.value * x[i] + phase.value)
                       * std::exp(-decay.value * x[i]);
            line.set_y(y);
        });

    spectra::show();
    return 0;
}
