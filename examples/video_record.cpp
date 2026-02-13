#include <plotix/plotix.hpp>

#include <cmath>
#include <iostream>
#include <vector>

int main() {
#ifndef PLOTIX_USE_FFMPEG
    std::cerr << "This example requires PLOTIX_USE_FFMPEG=ON\n";
    return 1;
#else
    plotix::App app({.headless = true});
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax  = fig.subplot(1, 1, 1);

    constexpr size_t N = 200;
    std::vector<float> x(N);
    std::vector<float> y(N);
    for (size_t i = 0; i < N; ++i) {
        x[i] = static_cast<float>(i) * 0.05f;
    }

    ax.line(x, y).label("wave").color(plotix::colors::cyan);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Recorded Animation");
    ax.xlabel("X");
    ax.ylabel("Y");

    fig.animate()
       .fps(60)
       .duration(10.0f)
       .on_frame([&](plotix::Frame& f) {
           float t = f.elapsed_seconds();
           for (size_t i = 0; i < N; ++i) {
               y[i] = std::sin(x[i] + t * 2.0f);
           }
           ax.line(x, y);
       })
       .record("output.mp4");

    app.run();
    std::cout << "Recorded output.mp4\n";
    return 0;
#endif
}
