#include <plotix/app.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>
#include <plotix/axes.hpp>

#include <cmath>
#include <vector>

int main() {
    // Create app with ImGui enabled
    plotix::AppConfig config;
    config.headless = false;
    
    plotix::App app(config);
    
    // Create a figure with sample data
    auto& fig = app.figure();
    
    // Create axes
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(-10, 10);
    ax.ylim(-2, 2);
    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");
    
    // Create sample sine wave data
    std::vector<float> x_data, y_data;
    for (int i = 0; i < 100; ++i) {
        float x = -10.0f + i * 20.0f / 99.0f;
        x_data.push_back(x);
        y_data.push_back(std::sin(x));
    }
    
    // Create series
    auto& series = ax.line(x_data, y_data);
    series.color({0.12f, 0.38f, 0.78f, 1.0f});
    series.width(2.0f);
    
    // Run the application
    app.run();
    
    return 0;
}
