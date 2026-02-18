#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
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
    Figure& figure(Figure& sibling);

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

    // Group figures into windows based on sibling relationships.
    // Returns a vector of groups; each group is a vector of FigureIds
    // that should share one OS window.
    std::vector<std::vector<FigureId>> compute_window_groups() const;

    AppConfig config_;
    FigureRegistry registry_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<Renderer> renderer_;

    // Maps a FigureId to the FigureId it should be tabbed next to.
    // Figures not in this map get their own window.
    std::unordered_map<FigureId, FigureId> sibling_map_;
};

}  // namespace spectra
