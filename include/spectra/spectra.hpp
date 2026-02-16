#pragma once

#include <spectra/animator.hpp>
#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/color.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/fwd.hpp>
#include <spectra/logger.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/timeline.hpp>

// ─── Convenience API ─────────────────────────────────────────────────────────
// Free functions that manage a global App instance under the hood.
//
//   auto& fig = spectra::figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//   ax.line(x, y);
//   spectra::show();
//
// For advanced use (multiple windows, headless, custom config), use App directly.

namespace spectra
{

namespace detail
{

inline App& global_app()
{
    static App instance;
    return instance;
}

}  // namespace detail

// Create a new figure on the global App.
inline Figure& figure(const FigureConfig& config = {})
{
    return detail::global_app().figure(config);
}

// Show all figures and enter the event loop (blocking).
inline void show()
{
    detail::global_app().run();
}

}  // namespace spectra
