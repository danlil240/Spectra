#include <plotix/plotix.hpp>

#include <cmath>
#include <vector>

int main() {
    plotix::App app;
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax  = fig.subplot(1, 1, 1);

    constexpr size_t N = 100;
    std::vector<float> x(N);
    std::vector<float> y(N);

    // Initial positions on a circle
    for (size_t i = 0; i < N; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(N) * 6.2832f;
        x[i] = std::cos(angle);
        y[i] = std::sin(angle);
    }

    auto& scatter = ax.scatter(x, y)
                      .color(plotix::rgb(1.0f, 0.4f, 0.0f))
                      .size(6.0f);

    ax.xlim(-2.0f, 2.0f);
    ax.ylim(-2.0f, 2.0f);
    ax.title("Animated Scatter");
    ax.xlabel("X");
    ax.ylabel("Y");

    fig.animate()
        .fps(60)
        .on_frame(
            [&](plotix::Frame &f)
            {
                float t = f.elapsed_seconds();
                for (size_t i = 0; i < N; ++i)
                {
                    float angle = static_cast<float>(i) / static_cast<float>(N) * 6.2832f;
                    float r = 1.0f + 0.5f * std::sin(t * 2.0f + angle);
                    x[i] = r * std::cos(angle + t * 0.5f);
                    y[i] = r * std::sin(angle + t * 0.5f);
                }
                scatter.set_x(x);
                scatter.set_y(y);
            })
        .play();

    app.run();


    return 0;
}
