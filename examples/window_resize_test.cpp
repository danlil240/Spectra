#include <plotix/plotix.hpp>
#include <cmath>
#include <vector>

int main() {
    plotix::App app;
    
    // Create a figure with some sample data
    auto& fig = app.figure();
    auto& ax = fig.subplot(1, 1, 0);
    
    // Generate sample data
    std::vector<float> x, y;
    for (int i = 0; i < 1000; ++i) {
        x.push_back(i * 0.01f);
        y.push_back(std::sin(x[i]) * std::exp(-x[i] * 0.1f));
    }
    
    // Create a line series
    auto line = ax.line(x, y);
    line.color({0.2f, 0.6f, 1.0f, 1.0f});
    line.width(2.0f);
    
    // Add grid
    ax.grid(true);
    
    // Set labels
    ax.title("Window Resizing Test");
    ax.xlabel("Time (s)");
    ax.ylabel("Amplitude");
    
    // Show the window - try resizing it!
    fig.show();
    
    return 0;
}
