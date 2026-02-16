#include <cmath>
#include <plotix/plotix.hpp>
#include <vector>

int main()
{
    plotix::App app;
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax = fig.subplot(1, 1, 1);

    // Generate a sine wave
    std::vector<float> x(200);
    std::vector<float> y(200);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i) * 0.05f;
        y[i] = std::sin(x[i]);
    }

    ax.line(x, y).label("sin(x)").color(plotix::rgb(0.2f, 0.8f, 1.0f));
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Basic Line Plot");
    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");

    app.run();

    return 0;
}
