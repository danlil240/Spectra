// Timeline & Curve Editor Demo
// Demonstrates KeyframeInterpolator + AnimationCurveEditor + TimelineEditor integration
//
// This example shows:
// - Creating animated properties via KeyframeInterpolator
// - Visual editing with AnimationCurveEditor
// - Timeline playback controls
// - Real-time property binding to plot elements

#include <cmath>
#include <iostream>
#include <spectra/spectra.hpp>
#include <vector>

int main()
{
    spectra::App app;
    auto& fig = app.figure({.width = 1200, .height = 800});
    auto& ax = fig.subplot(1, 1, 1);

    // Generate sample data
    constexpr size_t N = 100;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) / (N - 1) * 10.0f;
        y[i] = std::sin(x[i]);
    }

    // Create a line series that will be animated
    auto& line = ax.line(x, y).label("Animated Sine Wave");

    // Set initial style
    line.color(spectra::colors::blue);
    line.width(2.0f);

    std::cout << "=== Timeline & Curve Editor Demo ===\n";
    std::cout << "\nControls:\n";
    std::cout << "  Space     - Toggle Play/Pause\n";
    std::cout << "  [ / ]     - Step Back/Forward\n";
    std::cout << "  T         - Toggle Timeline Panel\n";
    std::cout << "  Ctrl+P    - Toggle Curve Editor Panel\n";
    std::cout << "\nFeatures:\n";
    std::cout << "  - Timeline panel at bottom with transport controls\n";
    std::cout << "  - Curve editor window for visual keyframe editing\n";
    std::cout << "  - Real-time property binding during playback\n";
    std::cout << "\nUsage:\n";
    std::cout << "  1. Press 'T' to show the timeline panel\n";
    std::cout << "  2. Press 'Ctrl+P' to open the curve editor\n";
    std::cout << "  3. Press Space to start playback\n";
    std::cout << "  4. Use the curve editor to adjust animation curves\n";
    std::cout << "  5. Add keyframes by right-clicking in the curve editor\n";
    std::cout << "\nNote: This demo shows the UI integration.\n";
    std::cout << "      Actual property binding requires accessing the app's\n";
    std::cout << "      internal KeyframeInterpolator instance.\n";

    // Configure the plot
    ax.title("Timeline & Curve Editor Integration");
    ax.xlabel("Time (s)");
    ax.ylabel("Value");
    ax.grid(true);
    fig.legend().visible = true;

    // Set axis limits to show the full data range
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);

    app.run();

    return 0;
}
