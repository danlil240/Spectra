#include "data/filters.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace plotix::data
{

std::vector<float> moving_average(std::span<const float> values, std::size_t window_size)
{
    const std::size_t n = values.size();
    if (n == 0)
        return {};
    if (window_size == 0)
        window_size = 1;

    std::vector<float> out(n);

    // Half-window (centered)
    const std::size_t half = window_size / 2;

    // Use a running sum for O(n) performance
    // First compute prefix sums
    std::vector<double> prefix(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + static_cast<double>(values[i]);

    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t lo = (i >= half) ? (i - half) : 0;
        const std::size_t hi = std::min(i + half, n - 1);
        const auto count = static_cast<double>(hi - lo + 1);
        out[i] = static_cast<float>((prefix[hi + 1] - prefix[lo]) / count);
    }

    return out;
}

std::vector<float> exponential_smoothing(std::span<const float> values, float alpha)
{
    const std::size_t n = values.size();
    if (n == 0)
        return {};

    assert(alpha > 0.0f && alpha <= 1.0f);

    std::vector<float> out(n);
    out[0] = values[0];

    const float one_minus_alpha = 1.0f - alpha;
    for (std::size_t i = 1; i < n; ++i)
        out[i] = alpha * values[i] + one_minus_alpha * out[i - 1];

    return out;
}

std::vector<float> gaussian_smooth(std::span<const float> values, float sigma, std::size_t radius)
{
    const std::size_t n = values.size();
    if (n == 0)
        return {};
    if (sigma <= 0.0f)
    {
        return {values.begin(), values.end()};
    }

    if (radius == 0)
        radius = static_cast<std::size_t>(std::ceil(3.0f * sigma));

    // Build kernel
    const std::size_t kernel_size = 2 * radius + 1;
    std::vector<float> kernel(kernel_size);
    const float inv_2sigma2 = 1.0f / (2.0f * sigma * sigma);
    float sum = 0.0f;
    for (std::size_t k = 0; k < kernel_size; ++k)
    {
        const auto d = static_cast<float>(static_cast<int>(k) - static_cast<int>(radius));
        kernel[k] = std::exp(-d * d * inv_2sigma2);
        sum += kernel[k];
    }
    // Normalize
    for (auto& v : kernel)
        v /= sum;

    // Convolve
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float acc = 0.0f;
        float w_sum = 0.0f;
        for (std::size_t k = 0; k < kernel_size; ++k)
        {
            const auto j_signed =
                static_cast<int>(i) + static_cast<int>(k) - static_cast<int>(radius);
            if (j_signed >= 0 && static_cast<std::size_t>(j_signed) < n)
            {
                acc += kernel[k] * values[static_cast<std::size_t>(j_signed)];
                w_sum += kernel[k];
            }
        }
        out[i] = acc / w_sum;
    }

    return out;
}

}  // namespace plotix::data
