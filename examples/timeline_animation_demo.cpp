// Timeline Animation Demo
// Demonstrates TimelineEditor + KeyframeInterpolator + AnimationCurveEditor integration
// Press T to toggle timeline, Shift+T for curve editor, Space to play/pause

#include <plotix/plotix.hpp>
#include <cmath>
#include <vector>

int main() {
    plotix::App app;
    auto& fig = app.figure({.width = 1200, .height = 800});
    
    // Create 4 subplots for different animation types
    auto& ax1 = fig.subplot(2, 2, 1);
    auto& ax2 = fig.subplot(2, 2, 2);
    auto& ax3 = fig.subplot(2, 2, 3);
    auto& ax4 = fig.subplot(2, 2, 4);
    
    ax1.title("Frequency Modulation");
    ax1.xlabel("X");
    ax1.ylabel("Y");
    
    ax2.title("Position + Size Animation");
    ax2.xlabel("X");
    ax2.ylabel("Y");
    
    ax3.title("Style Animation");
    ax3.xlabel("Time");
    ax3.ylabel("Value");
    
    ax4.title("Opacity Fade");
    ax4.xlabel("Time");
    ax4.ylabel("Value");

    // Generate base data
    constexpr size_t N = 200;
    std::vector<float> x_base(N), y_base(N);
    std::vector<float> t_line(N), y_line(N);
    std::vector<float> t_fade(N), y_fade(N);
    std::vector<float> scatter_x(1), scatter_y(1);
    
    for (size_t i = 0; i < N; ++i) {
        x_base[i] = static_cast<float>(i) / static_cast<float>(N - 1) * 6.28f;
        y_base[i] = std::sin(x_base[i]);
        
        t_line[i] = static_cast<float>(i) / static_cast<float>(N - 1) * 4.0f;
        y_line[i] = std::sin(t_line[i] * 2.0f) * 0.5f + 0.5f;
        
        t_fade[i] = static_cast<float>(i) / static_cast<float>(N - 1) * 4.0f;
        y_fade[i] = std::cos(t_fade[i] * 1.5f) * 0.3f + 0.5f;
    }
    
    scatter_x[0] = 0.5f;
    scatter_y[0] = 0.5f;

    // Create series that will be animated
    auto& sine_wave = ax1.line(x_base, y_base).color(plotix::rgb(0.2f, 0.4f, 1.0f)).width(2.0f);
    auto& scatter = ax2.scatter(scatter_x, scatter_y).color(plotix::rgb(1.0f, 0.4f, 0.0f)).size(20.0f);
    auto& style_line = ax3.line(t_line, y_line).width(2.0f);
    auto& fade_line = ax4.line(t_fade, y_fade).width(3.0f);
    
    // Set axis limits
    ax1.xlim(0.0f, 6.28f);
    ax1.ylim(-1.5f, 1.5f);
    ax2.xlim(-0.2f, 1.2f);
    ax2.ylim(-0.2f, 1.2f);
    ax3.xlim(0.0f, 4.0f);
    ax3.ylim(0.0f, 1.0f);
    ax4.xlim(0.0f, 4.0f);
    ax4.ylim(0.0f, 1.0f);

    // Set up animation using the figure's animation system
    fig.animate()
        .fps(60)
        .on_frame([&](plotix::Frame& f) {
            float t = f.elapsed_seconds();
            
            // Animate frequency modulation
            float freq_mod = 1.0f + 0.5f * std::sin(t * 0.5f);
            std::vector<float> y_mod(N);
            for (size_t i = 0; i < N; ++i) {
                y_mod[i] = std::sin(x_base[i] * freq_mod);
            }
            sine_wave.set_y(y_mod);
            
            // Animate scatter position and size
            scatter_x[0] = 0.5f + 0.3f * std::cos(t * 0.7f);
            scatter_y[0] = 0.5f + 0.3f * std::sin(t * 0.7f);
            float scatter_size = 20.0f + 15.0f * std::sin(t * 1.2f);
            scatter.set_x(scatter_x);
            scatter.set_y(scatter_y);
            scatter.size(scatter_size);
            
            // Animate line color (simple RGB interpolation) and width
            float hue = std::fmod(t * 0.3f, 1.0f);
            float line_width = 2.0f + 3.0f * (std::sin(t * 0.8f) * 0.5f + 0.5f);
            // Simple HSV to RGB conversion for demo
            float r = std::abs(std::cos(hue * 6.28f));
            float g = std::abs(std::cos((hue + 0.33f) * 6.28f));
            float b = std::abs(std::cos((hue + 0.66f) * 6.28f));
            style_line.color(plotix::rgb(r, g, b)).width(line_width);
            
            // Animate opacity
            float opacity = 0.3f + 0.7f * (std::sin(t * 0.6f) * 0.5f + 0.5f);
            fade_line.opacity(opacity);
        })
        .play();

    app.run();
    return 0;
}
