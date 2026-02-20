#pragma once

// ─── Spectra Easy API ───────────────────────────────────────────────────────
//
// The simplest possible interface for scientific plotting.
// One header, zero boilerplate. Works identically in inproc and multiproc modes.
//
//   #include <spectra/easy.hpp>
//
//   int main() {
//       std::vector<float> x = {0, 1, 2, 3, 4};
//       std::vector<float> y = {0, 1, 4, 9, 16};
//
//       spectra::plot(x, y, "r--o");          // MATLAB-style format string
//       spectra::title("My Plot");
//       spectra::xlabel("X"); spectra::ylabel("Y");
//       spectra::show();
//   }
//
// ─── Progressive Complexity ─────────────────────────────────────────────────
//
// Level 1: One-liner plots
//   spectra::plot(x, y);
//   spectra::scatter(x, y);
//   spectra::show();
//
// Level 2: Styling
//   spectra::plot(x, y, "r--o").label("sin(x)");
//   spectra::plot(x, y2, "b:s").label("cos(x)");
//   spectra::title("Trig");
//   spectra::legend();
//   spectra::show();
//
// Level 3: Subplots
//   spectra::subplot(2, 1, 1);
//   spectra::plot(x, y1);
//   spectra::title("Top");
//
//   spectra::subplot(2, 1, 2);
//   spectra::plot(x, y2);
//   spectra::title("Bottom");
//   spectra::show();
//
// Level 4: Multiple windows & tabs
//   spectra::figure();              // Window 1
//   spectra::plot(x, y1);
//
//   spectra::tab();                 // Tab in same window
//   spectra::plot(x, y2);
//
//   spectra::figure();              // Window 2 (new OS window)
//   spectra::plot(x, y3);
//   spectra::show();
//
// Level 5: Real-time / animation
//   auto& line = spectra::plot(x, y);
//   spectra::on_update([&](float dt, float t) {
//       for (int i = 0; i < N; ++i)
//           y[i] = sin(x[i] + t);
//       line.set_y(y);
//   });
//   spectra::show();
//
// Level 6: 3D
//   spectra::figure();
//   spectra::plot3(x, y, z);
//   spectra::scatter3(x, y, z, "ro");
//   spectra::surf(xg, yg, zv);
//   spectra::show();
//
// Level 7: Full control (drop down to object API)
//   spectra::App app;
//   auto& fig = app.figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//   ax.line(x, y);
//   app.run();
//
// ─────────────────────────────────────────────────────────────────────────────

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace spectra
{

// ─── Internal: Global State ─────────────────────────────────────────────────

namespace detail
{

inline App& global_app()
{
    static App instance;
    return instance;
}

struct EasyState
{
    App* app = nullptr;
    Figure* current_fig = nullptr;
    Axes* current_ax = nullptr;
    Axes3D* current_ax3d = nullptr;
    bool owns_app = false;

    // Animation callback
    std::function<void(float dt, float elapsed)> on_update_cb;

    // Auto-created state tracking
    bool has_explicit_figure = false;
    bool has_explicit_subplot = false;

    App& ensure_app()
    {
        if (!app)
        {
            app = &global_app();
            owns_app = false;
        }
        return *app;
    }

    Figure& ensure_figure()
    {
        ensure_app();
        if (!current_fig)
        {
            current_fig = &app->figure();
            has_explicit_figure = false;
        }
        return *current_fig;
    }

    Axes& ensure_axes()
    {
        ensure_figure();
        if (!current_ax)
        {
            current_ax = &current_fig->subplot(1, 1, 1);
            has_explicit_subplot = false;
        }
        return *current_ax;
    }

    Axes3D& ensure_axes3d()
    {
        ensure_figure();
        if (!current_ax3d)
        {
            current_ax3d = &current_fig->subplot3d(1, 1, 1);
            has_explicit_subplot = false;
        }
        return *current_ax3d;
    }

    void reset()
    {
        current_fig = nullptr;
        current_ax = nullptr;
        current_ax3d = nullptr;
        on_update_cb = nullptr;
        has_explicit_figure = false;
        has_explicit_subplot = false;
        // Don't reset app — it persists
    }
};

inline EasyState& easy_state()
{
    static EasyState s;
    return s;
}

}  // namespace detail

// ─── Figure Management ──────────────────────────────────────────────────────

// Create a new figure (new OS window). Returns the Figure for advanced use.
inline Figure& figure(uint32_t width = 1280, uint32_t height = 720)
{
    auto& s = detail::easy_state();
    s.ensure_app();
    s.current_fig = &s.app->figure({.width = width, .height = height});
    s.current_ax = nullptr;
    s.current_ax3d = nullptr;
    s.has_explicit_figure = true;
    s.has_explicit_subplot = false;
    return *s.current_fig;
}

// Create a new figure that opens as a tab next to an existing figure.
inline Figure& figure(Figure& tab_next_to, uint32_t width = 1280, uint32_t height = 720)
{
    auto& s = detail::easy_state();
    s.ensure_app();
    s.current_fig = &s.app->figure(tab_next_to);
    s.current_ax = nullptr;
    s.current_ax3d = nullptr;
    s.has_explicit_figure = true;
    s.has_explicit_subplot = false;
    return *s.current_fig;
}

// Create a new figure as a tab in the current window.
// If no figure exists yet, behaves like figure() (creates a new window).
//
//   spectra::figure();          // Window 1
//   spectra::plot(x, y_sin);    //   tab 1: sine
//
//   spectra::tab();             //   tab 2 (same window)
//   spectra::plot(x, y_cos);    //   tab 2: cosine
//
//   spectra::figure();          // Window 2 (new OS window)
//   spectra::plot(x, y_exp);
//
inline Figure& tab(uint32_t width = 0, uint32_t height = 0)
{
    auto& s = detail::easy_state();
    s.ensure_app();

    if (!s.current_fig)
    {
        // No current figure — just create a new window
        return figure(width ? width : 1280, height ? height : 720);
    }

    // Create a new figure as a tab next to the current figure
    s.current_fig = &s.app->figure(*s.current_fig);
    s.current_ax = nullptr;
    s.current_ax3d = nullptr;
    s.has_explicit_figure = true;
    s.has_explicit_subplot = false;
    return *s.current_fig;
}

// ─── Subplot Selection ──────────────────────────────────────────────────────

// Select a 2D subplot (creates the figure if needed). 1-based index.
inline Axes& subplot(int rows, int cols, int index)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_ax = &s.current_fig->subplot(rows, cols, index);
    s.current_ax3d = nullptr;
    s.has_explicit_subplot = true;
    return *s.current_ax;
}

// Select a 3D subplot (creates the figure if needed). 1-based index.
inline Axes3D& subplot3d(int rows, int cols, int index)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_ax3d = &s.current_fig->subplot3d(rows, cols, index);
    s.current_ax = nullptr;
    s.has_explicit_subplot = true;
    return *s.current_ax3d;
}

// ─── 2D Plotting ────────────────────────────────────────────────────────────

// Plot a line. Auto-creates figure and axes if needed.
inline LineSeries& plot(std::span<const float> x,
                        std::span<const float> y,
                        std::string_view fmt = "-")
{
    return detail::easy_state().ensure_axes().plot(x, y, fmt);
}

// Plot with explicit PlotStyle.
inline LineSeries& plot(std::span<const float> x,
                        std::span<const float> y,
                        const PlotStyle& style)
{
    return detail::easy_state().ensure_axes().plot(x, y, style);
}

// Create an empty line series (for real-time append).
inline LineSeries& plot()
{
    return detail::easy_state().ensure_axes().line();
}

// Scatter plot.
inline ScatterSeries& scatter(std::span<const float> x,
                               std::span<const float> y)
{
    return detail::easy_state().ensure_axes().scatter(x, y);
}

// Create an empty scatter series (for real-time append).
inline ScatterSeries& scatter()
{
    return detail::easy_state().ensure_axes().scatter();
}

// ─── 3D Plotting ────────────────────────────────────────────────────────────

// 3D line plot.
inline LineSeries3D& plot3(std::span<const float> x,
                            std::span<const float> y,
                            std::span<const float> z)
{
    return detail::easy_state().ensure_axes3d().line3d(x, y, z);
}

// 3D scatter plot.
inline ScatterSeries3D& scatter3(std::span<const float> x,
                                  std::span<const float> y,
                                  std::span<const float> z)
{
    return detail::easy_state().ensure_axes3d().scatter3d(x, y, z);
}

// Surface plot.
inline SurfaceSeries& surf(std::span<const float> x_grid,
                            std::span<const float> y_grid,
                            std::span<const float> z_values)
{
    return detail::easy_state().ensure_axes3d().surface(x_grid, y_grid, z_values);
}

// Mesh plot.
inline MeshSeries& mesh(std::span<const float> vertices,
                         std::span<const uint32_t> indices)
{
    return detail::easy_state().ensure_axes3d().mesh(vertices, indices);
}

// ─── Axes Configuration (applies to current axes) ───────────────────────────

inline void xlim(float min, float max)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->xlim(min, max);
    else
        s.ensure_axes().xlim(min, max);
}

inline void ylim(float min, float max)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->ylim(min, max);
    else
        s.ensure_axes().ylim(min, max);
}

inline void zlim(float min, float max)
{
    detail::easy_state().ensure_axes3d().zlim(min, max);
}

inline void title(const std::string& t)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->title(t);
    else
        s.ensure_axes().title(t);
}

inline void xlabel(const std::string& lbl)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->xlabel(lbl);
    else
        s.ensure_axes().xlabel(lbl);
}

inline void ylabel(const std::string& lbl)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->ylabel(lbl);
    else
        s.ensure_axes().ylabel(lbl);
}

inline void zlabel(const std::string& lbl)
{
    detail::easy_state().ensure_axes3d().zlabel(lbl);
}

inline void grid(bool enabled = true)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->grid(enabled);
    else
        s.ensure_axes().grid(enabled);
}

inline void legend(LegendPosition pos = LegendPosition::TopRight)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_fig->legend().position = pos;
    s.current_fig->legend().visible = true;
}

// ─── Real-Time / Animation ──────────────────────────────────────────────────

// Register a per-frame update callback. Called every frame with (dt, elapsed_seconds).
// Use this to update data in real-time (e.g. live sensor streams, simulations).
//
//   auto& line = spectra::plot(x, y);
//   spectra::on_update([&](float dt, float t) {
//       for (int i = 0; i < N; ++i) y[i] = sin(x[i] + t);
//       line.set_y(y);
//       spectra::xlim(t - 10, t);  // sliding window
//   });
//   spectra::show();
//
inline void on_update(std::function<void(float dt, float elapsed)> callback)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.on_update_cb = std::move(callback);

    // Wire it through the Figure's animation system
    s.current_fig->animate()
        .fps(60)
        .on_frame(
            [&s](Frame& f)
            {
                if (s.on_update_cb)
                    s.on_update_cb(f.delta_time(), f.elapsed_seconds());
            })
        .play();
}

// Register per-frame update with explicit FPS target.
inline void on_update(float fps, std::function<void(float dt, float elapsed)> callback)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.on_update_cb = std::move(callback);

    s.current_fig->animate()
        .fps(fps)
        .on_frame(
            [&s](Frame& f)
            {
                if (s.on_update_cb)
                    s.on_update_cb(f.delta_time(), f.elapsed_seconds());
            })
        .play();
}

// ─── Export ─────────────────────────────────────────────────────────────────

// Save current figure as PNG.
inline void save_png(const std::string& path)
{
    detail::easy_state().ensure_figure().save_png(path);
}

// Save current figure as PNG with explicit resolution.
inline void save_png(const std::string& path, uint32_t width, uint32_t height)
{
    detail::easy_state().ensure_figure().save_png(path, width, height);
}

// Save current figure as SVG.
inline void save_svg(const std::string& path)
{
    detail::easy_state().ensure_figure().save_svg(path);
}

// ─── Show & Run ─────────────────────────────────────────────────────────────

// Show all figures and enter the interactive event loop (blocking).
// This is the last call in your program.
inline void show()
{
    auto& s = detail::easy_state();
    s.ensure_app();
    s.app->run();
    s.reset();
}

// ─── Utility ────────────────────────────────────────────────────────────────

// Get the current axes (2D). Returns nullptr if no axes created yet.
inline Axes* gca()
{
    return detail::easy_state().current_ax;
}

// Get the current 3D axes. Returns nullptr if no 3D axes created yet.
inline Axes3D* gca3d()
{
    return detail::easy_state().current_ax3d;
}

// Get the current figure. Returns nullptr if no figure created yet.
inline Figure* gcf()
{
    return detail::easy_state().current_fig;
}

// Clear the current axes (remove all series).
inline void cla()
{
    auto& s = detail::easy_state();
    if (s.current_ax)
        s.current_ax->clear_series();
    if (s.current_ax3d)
        s.current_ax3d->clear_series();
}

}  // namespace spectra
