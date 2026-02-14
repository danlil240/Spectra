#include <plotix/plotix.hpp>

#include <vector>
#include <random>
#include <cmath>

using namespace plotix;

int main() {
    App app;
    
    // Create multiple figures using the app.figure() API
    auto& fig1 = app.figure({.width = 1280, .height = 720});
    auto& ax1 = fig1.subplot(1, 1, 1);
    
    // Generate sine and cosine data
    std::vector<float> x, y1, y2;
    for (int i = 0; i < 100; ++i) {
        float t = i * 0.1f;
        x.push_back(t);
        y1.push_back(std::sin(t));
        y2.push_back(std::cos(t));
    }
    
    ax1.line(x, y1).label("Sine Wave").color(rgb(0.2f, 0.6f, 1.0f));
    ax1.line(x, y2).label("Cosine Wave").color(rgb(1.0f, 0.4f, 0.2f));
    ax1.xlabel("Time (s)");
    ax1.ylabel("Amplitude");
    ax1.title("Trigonometric Functions");
    ax1.grid(true);
    
    // Figure 2: Scatter plot
    auto& fig2 = app.figure({.width = 1280, .height = 720});
    auto& ax2 = fig2.subplot(1, 1, 1);
    
    // Generate random scatter data
    std::vector<float> x_scatter, y_scatter;
    std::mt19937 gen(42);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    
    for (int i = 0; i < 200; ++i) {
        x_scatter.push_back(dist(gen));
        y_scatter.push_back(dist(gen));
    }
    
    ax2.scatter(x_scatter, y_scatter).label("Random Points").color(rgb(0.2f, 0.8f, 0.4f));
    ax2.xlabel("X Value");
    ax2.ylabel("Y Value");
    ax2.title("2D Normal Distribution");
    ax2.grid(true);
    
    // Figure 3: Subplot layout
    auto& fig3 = app.figure({.width = 1280, .height = 960});
    auto& ax3_top = fig3.subplot(2, 1, 1);
    auto& ax3_bottom = fig3.subplot(2, 1, 2);
    
    // Top subplot - sine
    std::vector<float> x3, y3;
    for (int i = 0; i < 50; ++i) {
        float t = i * 0.2f;
        x3.push_back(t);
        y3.push_back(std::sin(t) * std::exp(-t * 0.1f));
    }
    ax3_top.line(x3, y3).color(rgb(0.8f, 0.2f, 0.8f));
    ax3_top.title("Damped Sine");
    ax3_top.grid(true);
    
    // Bottom subplot - cosine
    std::vector<float> y4;
    for (int i = 0; i < 50; ++i) {
        float t = i * 0.2f;
        y4.push_back(std::cos(t) * std::exp(-t * 0.1f));
    }
    ax3_bottom.line(x3, y4).color(rgb(0.2f, 0.8f, 0.8f));
    ax3_bottom.title("Damped Cosine");
    ax3_bottom.xlabel("Time (s)");
    ax3_bottom.ylabel("Amplitude");
    ax3_bottom.grid(true);
    
    // Figure 4: Performance test with many points
    auto& fig4 = app.figure({.width = 1280, .height = 720});
    auto& ax4 = fig4.subplot(1, 1, 1);
    
    // Generate large dataset
    std::vector<float> x_large, y_large;
    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        float t = i * 0.01f;
        x_large.push_back(t);
        y_large.push_back(std::sin(t * 0.5f) + std::sin(t * 1.3f) * 0.3f + std::sin(t * 2.7f) * 0.1f);
    }
    
    ax4.line(x_large, y_large).label("Complex Waveform").color(rgb(0.1f, 0.1f, 0.1f));
    ax4.xlabel("Time (s)");
    ax4.ylabel("Amplitude");
    ax4.title("10K Point Performance Test");
    ax4.grid(true);
    
    // Show all figures
    fig1.show();
    fig2.show();
    fig3.show();
    fig4.show();
    
    // Run the application
    app.run();
    
    return 0;
}
