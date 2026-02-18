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

class WindowRuntime;

#ifdef SPECTRA_USE_GLFW
class GlfwAdapter;
class WindowManager;
#endif

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
#ifdef SPECTRA_MULTIPROC
    void run_multiproc();
#else
    void run_inproc();
    void render_secondary_window(struct WindowContext* wctx);
#endif

    AppConfig config_;
    FigureRegistry registry_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<Renderer> renderer_;
};

}  // namespace spectra
