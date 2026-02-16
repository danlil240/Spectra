#pragma once

#include <plotix/animator.hpp>
#include <plotix/app.hpp>
#include <plotix/axes.hpp>
#include <plotix/axes3d.hpp>
#include <plotix/camera.hpp>
#include <plotix/color.hpp>
#include <plotix/export.hpp>
#include <plotix/figure.hpp>
#include <plotix/frame.hpp>
#include <plotix/fwd.hpp>
#include <plotix/logger.hpp>
#include <plotix/math3d.hpp>
#include <plotix/series.hpp>
#include <plotix/series3d.hpp>
#include <plotix/timeline.hpp>

// ─── Convenience API ─────────────────────────────────────────────────────────
// Free functions that manage a global App instance under the hood.
//
//   auto& fig = plotix::figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//   ax.line(x, y);
//   plotix::show();
//
// For advanced use (multiple windows, headless, custom config), use App directly.

namespace plotix {

namespace detail {

inline App& global_app() {
    static App instance;
    return instance;
}

} // namespace detail

// Create a new figure on the global App.
inline Figure& figure(const FigureConfig& config = {}) {
    return detail::global_app().figure(config);
}

// Show all figures and enter the event loop (blocking).
inline void show() {
    detail::global_app().run();
}

} // namespace plotix
