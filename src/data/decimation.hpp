#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace plotix::data {

/// Largest-Triangle-Three-Buckets (LTTB) decimation.
/// Reduces N 2D points to `target_count` representative points while
/// preserving the visual shape of the data.  O(N) time, O(target_count) space.
/// Returns pairs of (x, y) values.
/// If target_count >= input size, returns a copy of the input unchanged.
[[nodiscard]] std::vector<std::pair<float, float>> lttb(
    std::span<const float> x,
    std::span<const float> y,
    std::size_t target_count);

/// Min-max decimation: for each of `bucket_count` equal-width buckets along x,
/// emit the point with the minimum y and the point with the maximum y.
/// Produces up to 2 * bucket_count output points (fewer if buckets are empty).
/// Ideal for waveform-style rendering where preserving peaks matters.
[[nodiscard]] std::vector<std::pair<float, float>> min_max_decimate(
    std::span<const float> x,
    std::span<const float> y,
    std::size_t bucket_count);

/// Uniform resampling of irregularly-spaced data via linear interpolation.
/// Produces `output_count` evenly-spaced samples in [x.front(), x.back()].
/// Input x must be sorted in ascending order.
[[nodiscard]] std::vector<std::pair<float, float>> resample_uniform(
    std::span<const float> x,
    std::span<const float> y,
    std::size_t output_count);

}  // namespace plotix::data
