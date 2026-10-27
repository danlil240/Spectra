#pragma once

// Internal data-extent queries on a 2D Axes (implemented in axes.cpp next to
// the autoscale extent so both cover the same series types).

#include <spectra/axes.hpp>

namespace spectra::detail
{

/// Centre of the data of every visible, autoscale-included series of @p axes:
/// lines, scatters, chunked lines, bars, histograms, boxes (with outliers),
/// violins, bands, stems, 2D shapes, and plugin (custom) series whose bounds
/// the host has computed.  Per-series double offsets are honoured; on a log
/// axis the centre is the geometric middle of the positive data.  Returns
/// false (leaving @p cx / @p cy untouched) when there is no finite data.
bool data_center(const Axes& axes, double& cx, double& cy);

}   // namespace spectra::detail
