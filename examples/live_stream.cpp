#include <plotix/plotix.hpp>

#include <cmath>

int main() {
    plotix::App app;
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax  = fig.subplot(1, 1, 1);

    auto& line = ax.line().label("live").color(plotix::colors::cyan);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Live Streaming Plot");
    ax.xlabel("Time (s)");
    ax.ylabel("Signal");

    fig.animate()
       .fps(60)
       .on_frame([&](plotix::Frame& f) {
           float t = f.elapsed_seconds();
           // Append a new point each frame simulating a sensor reading
           float value = std::sin(t * 2.0f) + 0.3f * std::sin(t * 7.0f);
           line.append(t, value);
           // Sliding window: show last 10 seconds
           ax.xlim(t - 10.0f, t);
       })
       .play();

    return 0;
}
