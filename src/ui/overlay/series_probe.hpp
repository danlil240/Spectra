#pragma once

// Series probing for cursor readouts (no ImGui):
//   - the value of a series at a given X (compare-at-X readout), interpolated
//     between samples or snapped to the nearest sample;
//   - geometric hit tests used by the hover tooltip and data tips: line
//     segments, bars, histogram bins, image cells and contour levels.
//
// Sorted X data (LineSeries::x_is_sorted(), chunked series) is searched with a
// binary search, so probing stays O(log n) for large series.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <spectra/axes.hpp>
#include <vector>

namespace spectra
{

class Series;
class BarSeries;
class HistogramSeries;
class ContourSeries;
class ChunkedLineSeries;

namespace probe
{

/// How the compare-at-X readout reads a series between its samples.
enum class ProbeMode : uint8_t
{
    Interpolate,   ///< Linear interpolation between the two samples around X
    Nearest,       ///< The sample whose X is closest
};

/// Row order of the compare-at-X readout.
enum class CompareSort : uint8_t
{
    Legend,            ///< Order the series were added (legend order)
    ValueDescending,   ///< Largest value first (top of the plot first)
};

/// A series value at one X.  x/y are absolute (series offsets included) for
/// the series-level functions and local (as stored) for the raw-array ones.
struct ProbeSample
{
    bool   valid        = false;
    double x            = 0.0;
    double y            = 0.0;
    size_t index        = 0;   // nearest stored sample (or bar / bin index)
    bool   interpolated = false;
};

/// Axis scales and series offsets used to interpolate the way lines are drawn:
/// segments are straight in axis space (core/axis_scale.hpp), so on a log or
/// sqrt axis the value between two samples is interpolated there, on the
/// absolute (offset) coordinates, and mapped back.
struct ProbeScale
{
    ScaleType x    = ScaleType::Linear;
    ScaleType y    = ScaleType::Linear;
    double    xoff = 0.0;   // series offsets added to the stored values
    double    yoff = 0.0;
};

/// Sample (x, y) at @p lx where @p x is sorted ascending (binary search).
/// Invalid outside [x.front(), x.back()] or across a NaN gap (Interpolate);
/// an exact hit on a finite sample is valid even next to a gap.
ProbeSample sample_sorted_at_x(std::span<const float> x,
                               std::span<const float> y,
                               double                 lx,
                               ProbeMode              mode,
                               const ProbeScale&      scale = {});

/// Same for unsorted x (linear scan).  Interpolate uses the first segment that
/// spans @p lx; Nearest the sample with the closest x inside the data's x range.
ProbeSample sample_unsorted_at_x(std::span<const float> x,
                                 std::span<const float> y,
                                 double                 lx,
                                 ProbeMode              mode,
                                 const ProbeScale&      scale = {});

/// Chunked series (x sorted ascending, as streaming storage assumes).  @p lx is
/// in stored (un-offset) coordinates.
ProbeSample sample_chunked_at_x(const ChunkedLineSeries& s,
                                double                   lx,
                                ProbeMode                mode,
                                const ProbeScale&        scale = {});

/// Value of a 2D series at absolute @p abs_x for the compare readout:
/// lines (interpolated or nearest), scatter sorted by x and stems (nearest
/// sample; an unsorted scatter cloud has no value at X), chunked
/// lines, vertical bars (the bar under X) and histograms (the bin under X).
/// Other series types return an invalid sample.  @p xscale / @p yscale are the
/// axes' scales (interpolation happens in axis space).
ProbeSample sample_series_at_x(const Series& s,
                               double        abs_x,
                               ProbeMode     mode,
                               ScaleType     xscale = ScaleType::Linear,
                               ScaleType     yscale = ScaleType::Linear);

/// One row of the compare-at-X readout.
struct CompareRow
{
    const Series* series       = nullptr;
    int           series_index = -1;   // position in Axes::series()
    ProbeSample   sample;              // absolute coordinates
};

/// Every visible, non-reference series of @p axes that has a value at
/// @p abs_x, in the requested order (non-finite values last).
std::vector<CompareRow> compare_at_x(const Axes& axes,
                                     double      abs_x,
                                     ProbeMode   mode,
                                     CompareSort sort);

/// Order @p rows in place (exposed for tests).
void sort_compare_rows(std::vector<CompareRow>& rows, CompareSort sort);

// ─── Hit tests ───────────────────────────────────────────────────────────────

/// Closest point of segment (ax, ay)-(bx, by) to (px, py): parameter t in
/// [0, 1] along the segment and the squared distance.
struct SegmentClosest
{
    double t     = 0.0;
    double dist2 = 0.0;
};
SegmentClosest closest_on_segment(double ax, double ay, double bx, double by, double px, double py);

/// Index range [begin, end) of samples of sorted @p x within
/// [@p lo, @p hi], widened by one sample on each side so segments that cross
/// the window are included.
struct IndexRange
{
    size_t begin = 0;
    size_t end   = 0;
};
IndexRange sorted_window(std::span<const float> x, double lo, double hi);

/// Bar of @p bars under absolute data point (x, y) (inside its rectangle,
/// either orientation); the last drawn bar wins where bars overlap.
std::optional<size_t> bar_at(const BarSeries& bars, double x, double y);

/// Histogram bin under absolute data point (x, y): x inside [edge_i, edge_i+1)
/// (the last bin includes its right edge) and y between 0 and the bin height.
std::optional<size_t> histogram_bin_at(const HistogramSeries& hist, double x, double y);

/// Histogram bin whose x range contains @p x (ignoring y).
std::optional<size_t> histogram_bin_at_x(const HistogramSeries& hist, double x);

/// Contour level of the geometry point @p point_index (x_data()/y_data()).
std::optional<float> contour_level_for_point(const ContourSeries& contour, size_t point_index);

}   // namespace probe
}   // namespace spectra
