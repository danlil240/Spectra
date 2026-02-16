#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace plotix::data
{

/// Simple moving average (SMA) filter.
/// Each output sample is the mean of the surrounding `window_size` input samples
/// (centered window).  Output has the same length as input.
/// Edge samples use a smaller, asymmetric window (no padding).
[[nodiscard]] std::vector<float> moving_average(std::span<const float> values,
                                                std::size_t window_size);

/// Exponential moving average (EMA) filter.
/// alpha ∈ (0, 1] controls smoothing: higher alpha = less smoothing.
/// Output[0] = values[0]; Output[i] = alpha * values[i] + (1 - alpha) * Output[i-1].
[[nodiscard]] std::vector<float> exponential_smoothing(std::span<const float> values, float alpha);

/// Gaussian-weighted moving average.
/// `sigma` controls the width of the Gaussian kernel (in samples).
/// `radius` is the half-width of the kernel window (kernel size = 2*radius + 1).
/// If radius == 0, it is automatically set to ceil(3 * sigma).
[[nodiscard]] std::vector<float> gaussian_smooth(std::span<const float> values,
                                                 float sigma,
                                                 std::size_t radius = 0);

}  // namespace plotix::data
