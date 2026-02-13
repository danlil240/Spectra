#include <plotix/plotix.hpp>

#include <cmath>
#include <vector>

int main() {
    plotix::App app;
    auto& fig = app.figure({.width = 1920, .height = 1080});

    // 2x1 subplot grid
    auto& ax1 = fig.subplot(2, 1, 1);
    auto& ax2 = fig.subplot(2, 1, 2);

    // Generate two independent datasets
    constexpr size_t N = 300;
    std::vector<float> x(N);
    std::vector<float> y1(N);
    std::vector<float> y2(N);

    for (size_t i = 0; i < N; ++i) {
        x[i]  = static_cast<float>(i) * 0.02f;
        y1[i] = std::sin(x[i] * 3.0f) * std::exp(-x[i] * 0.3f);
        y2[i] = std::cos(x[i] * 2.0f) + 0.5f * std::sin(x[i] * 7.0f);
    }

    ax1.line(x, y1).label("temperature").color(plotix::colors::red);
    ax1.title("Temperature");
    ax1.xlabel("Time (s)");
    ax1.ylabel("Temp (C)");
    ax1.xlim(0.0f, 6.0f);
    ax1.ylim(-1.5f, 1.5f);

    ax2.line(x, y2).label("pressure").color(plotix::rgb(0.2f, 0.6f, 1.0f));
    ax2.title("Pressure");
    ax2.xlabel("Time (s)");
    ax2.ylabel("Pressure (kPa)");
    ax2.xlim(0.0f, 6.0f);
    ax2.ylim(-2.0f, 2.0f);

    fig.show();

    return 0;
}
