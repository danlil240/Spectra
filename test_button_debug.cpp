#include <plotix/app.hpp>
#include <plotix/figure.hpp>
#include <iostream>

int main() {
    std::cout << "Creating Plotix app with debug logging enabled..." << std::endl;
    
    // Create app with UI enabled
    plotix::AppConfig config;
    config.headless = false;  // Enable UI to test buttons
    
    plotix::App app(config);
    
    // Create a simple figure with some data
    auto& fig = app.figure();
    
    // Add some sample data
    std::vector<float> x = {1, 2, 3, 4, 5};
    std::vector<float> y = {1, 4, 2, 8, 5};
    
    auto& ax = fig.subplot(1, 1, 1);  // Create a 1x1 subplot and get the first axes
    ax.line(x, y);  // Use line() method instead of plot()
    
    std::cout << "Starting app - test clicking the buttons in the UI!" << std::endl;
    std::cout << "Check the console for debug logs showing button clicks." << std::endl;
    std::cout << "Buttons to test:" << std::endl;
    std::cout << "- Menu button (hamburger icon)" << std::endl;
    std::cout << "- Home button (reset view)" << std::endl;
    std::cout << "- Pan mode button (hand icon)" << std::endl;
    std::cout << "- Box zoom button (magnifying glass icon)" << std::endl;
    std::cout << "- File/View menu items" << std::endl;
    std::cout << "- Reset to Defaults button in inspector" << std::endl;
    std::cout << "- Auto-fit button in inspector" << std::endl;
    
    // Run the app
    app.run();
    
    return 0;
}
