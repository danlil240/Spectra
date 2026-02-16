#include <spectra/spectra.hpp>
#include <vector>

int main()
{
    // Create a simple figure to test the menubar
    spectra::App app;
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot(1, 1, 1);

    // Add some sample data
    std::vector<float> x = {1, 2, 3, 4, 5};
    std::vector<float> y = {2, 4, 1, 5, 3};
    ax.line(x, y).label("Test Line").color(spectra::rgb(0.2, 0.6, 1.0));

    // Enable legend to test UI interactions
    fig.legend().visible = true;

    // Run the application - this will display the new modern menubar
    app.run();

    return 0;
}
