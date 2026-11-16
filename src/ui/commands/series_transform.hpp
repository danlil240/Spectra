#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

namespace spectra
{

class UndoManager;

namespace ui
{
struct SelectionContext;
}

// ─── Series transform application ───────────────────────────────────────────
//
// Shared by the Transforms menu and the Custom Formula dialog: decides which
// series a transform applies to, applies it either in place (overwrite) or as
// a new series next to the source (copy), and records one undoable action.

enum class SeriesTransformMode
{
    Copy,        // add "<name> · <suffix>" next to the source, source untouched
    Overwrite,   // replace the source data and append the suffix to its name
};

struct SeriesTransformTarget
{
    Axes*   axes   = nullptr;
    Series* series = nullptr;   // a LineSeries or ScatterSeries owned by `axes`
};

struct SeriesTransformTargets
{
    std::vector<SeriesTransformTarget> targets;
    bool                               from_selection = false;
    Axes*                              plot           = nullptr;   // current plot (no selection)
};

// Target rule: the selected line/scatter series of `figure` (inspector or
// legend selection, hidden ones included) when the selection holds any;
// otherwise every visible line/scatter series of the current plot.  The
// current plot is the selected axes, else `active` when it belongs to
// `figure`, else the figure's first 2D axes.
SeriesTransformTargets resolve_transform_targets(const ui::SelectionContext& sel,
                                                 Figure*                     figure,
                                                 Axes*                       active);

// Menu caption, e.g. "Applies to: 2 selected series" or
// "Applies to: all series in current plot (Pressure)" (plot title when set).
std::string describe_transform_targets(const SeriesTransformTargets& targets);

struct SeriesTransformSpec
{
    // Computes the output from x (as stored, relative to Series::x_offset())
    // and logical y (Series::y_offset() added).  Output y is logical; output x
    // is relative to the source's x offset unless `x_absolute` is set.
    using Fn = std::function<void(std::span<const float> x_in,
                                  std::span<const float> y_in,
                                  std::vector<float>&    x_out,
                                  std::vector<float>&    y_out)>;
    Fn          fn;
    std::string suffix;        // appended to series names: "<name> · <suffix>"
    std::string description;   // undo/redo menu text
    // Output is in different units (derivative, FFT, log): in overwrite mode,
    // when every series of an axes was transformed, the axes' y label gets the
    // suffix too.  Copy mode never touches labels (the source keeps its units).
    bool changes_units = false;
    // Output x is a new absolute domain (FFT frequencies): x offset is cleared.
    bool x_absolute = false;
};

// Spec for a TransformRegistry entry (built-in or registered by name).
// Returns false when `name` is not available.
bool make_registry_transform_spec(const std::string& name, SeriesTransformSpec& out);

// Short suffix for a registry transform name ("Derivative" -> "d/dt").
std::string transform_suffix(const std::string& registry_name);

// "<name> · <suffix>"; an unnamed series is called "series <index + 1>".
std::string transformed_series_name(const std::string& name,
                                    const std::string& suffix,
                                    size_t             series_index);

struct SeriesTransformHooks
{
    // Removes a series created by a copy-mode transform (undo).  The app
    // defers this to a safe point in the frame; null removes it immediately.
    std::function<void(AxesBase*, Series*)> remove_series;
};

struct SeriesTransformResult
{
    size_t               applied = 0;   // targets transformed
    std::vector<Series*> created;       // copies added (copy mode)
    std::vector<Axes*>   rescaled;      // axes whose view was auto-fitted
    std::vector<Axes*>   relabeled;     // axes whose y label got the suffix
};

// Applies `spec` to `targets` and pushes one action to `undo` (when non-null
// and something changed).  Undo restores data, offsets, names, labels and the
// previous view, or removes the created copies; redo re-applies.
//
// Autoscale rule: an affected axes is auto-fitted when, after the transform,
// the visible data extends outside the current view or spans less than a
// quarter of it on either axis.
SeriesTransformResult apply_series_transform(const std::vector<SeriesTransformTarget>& targets,
                                             const SeriesTransformSpec&                spec,
                                             SeriesTransformMode                       mode,
                                             UndoManager*                              undo,
                                             const SeriesTransformHooks&               hooks = {});

}   // namespace spectra
