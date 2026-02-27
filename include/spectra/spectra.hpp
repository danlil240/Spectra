#pragma once

#include <spectra/animator.hpp>
#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/color.hpp>
#include <spectra/easy.hpp>
#include <spectra/easy_embed.hpp>
#include <spectra/embed.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/fwd.hpp>
#include <spectra/logger.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

// ─── Convenience API ─────────────────────────────────────────────────────────
// For the easiest possible API with free functions (plot, scatter, show, etc.),
// include <spectra/easy.hpp> instead.
//
//   #include <spectra/easy.hpp>
//
//   spectra::plot(x, y, "r--o");
//   spectra::title("My Plot");
//   spectra::show();
//
// For full control, use App directly:
//
//   spectra::App app;
//   auto& fig = app.figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//   ax.line(x, y);
//   app.run();
