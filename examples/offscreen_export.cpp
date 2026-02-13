#include <plotix/plotix.hpp>

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    plotix::App app({.headless = true});
    auto& fig = app.figure({.width = 1920, .height = 1080});
    auto& ax  = fig.subplot(1, 1, 1);

    // Generate data
    constexpr size_t N = 500;
    std::vector<float> x(N);
    std::vector<float> y(N);
    for (size_t i = 0; i < N; ++i) {
        x[i] = static_cast<float>(i) * 0.02f;
        y[i] = std::sin(x[i]) * std::cos(x[i] * 0.5f);
    }

    ax.line(x, y).label("signal").color(plotix::rgb(0.1f, 0.7f, 0.3f));
    ax.title("Offscreen Export");
    ax.xlabel("X");
    ax.ylabel("Y");

    fig.save_png("output.png");
    std::cout << "Saved output.png\n";

    return 0;
}
