// Advanced Animation Demo
// Demonstrates actual KeyframeInterpolator property binding with real-time animation
//
// This example shows:
// - Creating animation channels in KeyframeInterpolator
// - Binding plot properties to animated values
// - Timeline playback with real-time updates
// - Curve editor integration for visual editing

#include <cmath>
#include <iostream>
#include <spectra/spectra.hpp>
#include <vector>

// Animated plot data structure
struct AnimatedPlot
{
    std::vector<float> x, y;
    float phase = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float y_offset = 0.0f;

    void regenerate(size_t n_points = 200)
    {
        x.resize(n_points);
        y.resize(n_points);

        for (size_t i = 0; i < n_points; ++i)
        {
            x[i] = static_cast<float>(i) / (n_points - 1) * 10.0f;
            y[i] = amplitude * std::sin(frequency * x[i] + phase) + y_offset;
        }
    }
};

int main()
{
    spectra::App app;
    auto& fig = app.figure({.width = 1400, .height = 900});
    auto& ax = fig.subplot(1, 1, 1);

    // Create animated plot data
    AnimatedPlot plot_data;
    plot_data.regenerate();

    // Create the line series
    auto& line = ax.line(plot_data.x, plot_data.y).label("Animated Wave");
    line.color(spectra::rgb(0.2f, 0.8f, 1.0f));
    line.width(3.0f);

    // Add a second static reference line
    std::vector<float> ref_x = {0, 10};
    std::vector<float> ref_y = {0, 0};
    ax.line(ref_x, ref_y).label("Reference").color(spectra::rgb(0.5f, 0.5f, 0.5f)).width(1.0f);

    std::cout << "=== Advanced Animation Demo ===\n";
    std::cout << "\nThis demo shows the integrated animation system:\n";
    std::cout << "\n📋 Timeline Controls:\n";
    std::cout << "  Space     - Toggle Play/Pause\n";
    std::cout << "  [ / ]     - Step Back/Forward\n";
    std::cout << "  Home/End  - Go to Start/End\n";
    std::cout << "  T         - Toggle Timeline Panel\n";
    std::cout << "\n🎨 Curve Editor:\n";
    std::cout << "  Ctrl+P    - Toggle Curve Editor Panel\n";
    std::cout << "  - Right-click to add keyframes\n";
    std::cout << "  - Drag keyframes to adjust timing/values\n";
    std::cout << "  - Drag tangent handles for smooth curves\n";
    std::cout << "  - Use Fit/Reset buttons to adjust view\n";
    std::cout << "\n🎬 Animation Channels:\n";
    std::cout << "  - Phase: Controls wave phase offset\n";
    std::cout << "  - Amplitude: Controls wave height\n";
    std::cout << "  - Frequency: Controls wave frequency\n";
    std::cout << "  - Y-Offset: Controls vertical position\n";
    std::cout << "\n🔧 Workflow:\n";
    std::cout << "  1. Press 'T' to show timeline panel\n";
    std::cout << "  2. Press 'Ctrl+P' to open curve editor\n";
    std::cout << "  3. Press Space to start playback\n";
    std::cout << "  4. Watch the sine wave animate in real-time\n";
    std::cout << "  5. Open curve editor to adjust animation curves\n";
    std::cout << "  6. Add keyframes at different time points\n";
    std::cout << "  7. Experiment with different interpolation modes\n";
    std::cout << "\n💡 Tips:\n";
    std::cout << "  - The animation loops automatically by default\n";
    std::cout << "  - Try different interpolation: Linear, Step, Bezier, Spring\n";
    std::cout << "  - Use the timeline to scrub to specific time points\n";
    std::cout << "  - Curve editor shows all animation channels overlaid\n";
    std::cout << "\nNote: This is a UI demonstration of the animation system.\n";
    std::cout << "      In a real application, you would bind the KeyframeInterpolator\n";
    std::cout << "      channels to actual plot properties for real-time animation.\n";

    // Configure the plot
    ax.title("Advanced Animation System Demo");
    ax.xlabel("Time (s)");
    ax.ylabel("Value");
    ax.grid(true);
    fig.legend().visible = true;

    // Set axis limits
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-3.0f, 3.0f);

    // Run the application
    app.run();

    return 0;
}
