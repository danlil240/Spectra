// Two y axes on one plot: temperature on the left, rainfall on the right.
#include <cmath>
#include <iostream>
#include <spectra/spectra.hpp>
#include <vector>

int main(int argc, char** argv)
{
    const bool   headless = argc > 1;
    spectra::App app({.headless = headless, .socket_path = ""});
    auto&        fig = app.figure({.width = 1280, .height = 720});
    auto&        ax  = fig.subplot(1, 1, 1);

    constexpr size_t   N = 365;
    std::vector<float> day(N);
    std::vector<float> temp(N);
    std::vector<float> rain(N);
    for (size_t i = 0; i < N; ++i)
    {
        day[i]  = static_cast<float>(i);
        temp[i] = 18.0f + 9.0f * std::sin((day[i] - 100.0f) * 6.2832f / 365.0f);
        rain[i] =
            120.0f + 110.0f * std::cos(day[i] * 6.2832f / 365.0f) + 25.0f * std::sin(day[i] * 0.4f);
    }

    ax.plot(day, temp).label("temperature");
    ax.xlabel("day of year");
    ax.ylabel("temperature (C)");
    ax.title("Climate");

    auto& right = ax.twinx();
    right.plot(day, rain, "--").label("rainfall");
    right.ylabel("rainfall (mm)");

    if (headless)
    {
        fig.save_png(argv[1]);
        if (argc > 2)
            fig.save_svg(argv[2]);
    }
    app.run();
    if (headless)
        std::cout << "Saved " << argv[1] << "\n";
    return 0;
}
