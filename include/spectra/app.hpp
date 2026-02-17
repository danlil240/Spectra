#pragma once

#include <memory>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>

#include "../src/ui/figure_registry.hpp"

namespace spectra
{

struct AppConfig
{
    bool headless = false;
};

class App
{
   public:
    explicit App(const AppConfig& config = {});
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    Figure& figure(const FigureConfig& config = {});

    // Run the application (blocking — processes all figures)
    void run();

    bool is_headless() const { return config_.headless; }

    // Access internals (for renderer integration)
    Backend* backend() { return backend_.get(); }
    Renderer* renderer() { return renderer_.get(); }

   private:
    AppConfig config_;
    FigureRegistry registry_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<Renderer> renderer_;
};

}  // namespace spectra
