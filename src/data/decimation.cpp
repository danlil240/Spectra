#include "data/decimation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace spectra::data
{

std::vector<std::pair<float, float>> lttb(std::span<const float> x,
                                          std::span<const float> y,
                                          std::size_t target_count)
{
    assert(x.size() == y.size());
    const std::size_t n = x.size();

    if (n == 0)
        return {};
    if (target_count >= n || target_count < 3)
    {
        // Nothing to decimate, or too few buckets to run the algorithm
        std::vector<std::pair<float, float>> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            out.emplace_back(x[i], y[i]);
        return out;
    }

    std::vector<std::pair<float, float>> out;
    out.reserve(target_count);

    // Always keep the first point
    out.emplace_back(x[0], y[0]);

    const double bucket_size = static_cast<double>(n - 2) / static_cast<double>(target_count - 2);

    std::size_t prev_selected = 0;

    for (std::size_t bucket = 0; bucket < target_count - 2; ++bucket)
    {
        // Current bucket range
        const auto bucket_start =
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket) * bucket_size)) + 1;
        const auto bucket_end =
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket + 1) * bucket_size)) + 1;

        // Next bucket range (for computing the average point)
        const auto next_start =
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket + 1) * bucket_size)) + 1;
        const auto next_end = (bucket + 2 < target_count - 1)
                                  ? static_cast<std::size_t>(
                                        std::floor(static_cast<double>(bucket + 2) * bucket_size))
                                        + 1
                                  : n;

        // Compute average of next bucket
        double avg_x = 0.0, avg_y = 0.0;
        const std::size_t next_count = std::min(next_end, n) - std::min(next_start, n);
        if (next_count > 0)
        {
            for (std::size_t i = next_start; i < next_end && i < n; ++i)
            {
                avg_x += x[i];
                avg_y += y[i];
            }
            avg_x /= static_cast<double>(next_count);
            avg_y /= static_cast<double>(next_count);
        }

        // Find the point in the current bucket that forms the largest triangle
        // with the previously selected point and the average of the next bucket
        double max_area = -1.0;
        std::size_t best = bucket_start;

        const double px = x[prev_selected];
        const double py = y[prev_selected];

        for (std::size_t i = bucket_start; i < bucket_end && i < n; ++i)
        {
            // Triangle area = 0.5 * |x_a(y_b - y_c) + x_b(y_c - y_a) + x_c(y_a - y_b)|
            const double area = std::abs((px - avg_x) * (static_cast<double>(y[i]) - py)
                                         - (px - static_cast<double>(x[i])) * (avg_y - py));

            if (area > max_area)
            {
                max_area = area;
                best = i;
            }
        }

        out.emplace_back(x[best], y[best]);
        prev_selected = best;
    }

    // Always keep the last point
    out.emplace_back(x[n - 1], y[n - 1]);

    return out;
}

std::vector<std::pair<float, float>> min_max_decimate(std::span<const float> x,
                                                      std::span<const float> y,
                                                      std::size_t bucket_count)
{
    assert(x.size() == y.size());
    const std::size_t n = x.size();

    if (n == 0 || bucket_count == 0)
        return {};
    if (n <= bucket_count * 2)
    {
        std::vector<std::pair<float, float>> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            out.emplace_back(x[i], y[i]);
        return out;
    }

    const float x_min = x.front();
    const float x_max = x.back();
    const float range = x_max - x_min;

    if (range <= 0.0f)
    {
        // All x values are the same; just return first and last
        return {{x.front(), y.front()}, {x.back(), y.back()}};
    }

    const float bucket_width = range / static_cast<float>(bucket_count);

    struct BucketInfo
    {
        float min_y = std::numeric_limits<float>::max();
        float max_y = -std::numeric_limits<float>::max();
        float min_x = 0.0f, max_x = 0.0f;
        std::size_t min_idx = 0, max_idx = 0;
        bool has_data = false;
    };

    std::vector<BucketInfo> buckets(bucket_count);

    for (std::size_t i = 0; i < n; ++i)
    {
        auto bi = static_cast<std::size_t>((x[i] - x_min) / bucket_width);
        if (bi >= bucket_count)
            bi = bucket_count - 1;

        auto& b = buckets[bi];
        b.has_data = true;
        if (y[i] < b.min_y)
        {
            b.min_y = y[i];
            b.min_x = x[i];
            b.min_idx = i;
        }
        if (y[i] > b.max_y)
        {
            b.max_y = y[i];
            b.max_x = x[i];
            b.max_idx = i;
        }
    }

    std::vector<std::pair<float, float>> out;
    out.reserve(bucket_count * 2);

    for (auto& b : buckets)
    {
        if (!b.has_data)
            continue;
        // Emit min before max if min comes first in original data, to preserve ordering
        if (b.min_idx <= b.max_idx)
        {
            out.emplace_back(b.min_x, b.min_y);
            if (b.min_idx != b.max_idx)
                out.emplace_back(b.max_x, b.max_y);
        }
        else
        {
            out.emplace_back(b.max_x, b.max_y);
            out.emplace_back(b.min_x, b.min_y);
        }
    }

    return out;
}

std::vector<std::pair<float, float>> resample_uniform(std::span<const float> x,
                                                      std::span<const float> y,
                                                      std::size_t output_count)
{
    assert(x.size() == y.size());
    const std::size_t n = x.size();

    if (n == 0 || output_count == 0)
        return {};
    if (n == 1)
        return {{x[0], y[0]}};

    std::vector<std::pair<float, float>> out;
    out.reserve(output_count);

    const float x_start = x.front();
    const float x_end = x.back();
    const float step =
        (output_count > 1) ? (x_end - x_start) / static_cast<float>(output_count - 1) : 0.0f;

    std::size_t j = 0;  // current index into input arrays

    for (std::size_t i = 0; i < output_count; ++i)
    {
        const float xi = x_start + static_cast<float>(i) * step;

        // Advance j so that x[j] <= xi < x[j+1]
        while (j + 1 < n && x[j + 1] < xi)
            ++j;

        if (j + 1 >= n)
        {
            out.emplace_back(xi, y[n - 1]);
        }
        else
        {
            const float dx = x[j + 1] - x[j];
            if (dx <= 0.0f)
            {
                out.emplace_back(xi, y[j]);
            }
            else
            {
                const float t = (xi - x[j]) / dx;
                out.emplace_back(xi, y[j] + t * (y[j + 1] - y[j]));
            }
        }
    }

    return out;
}

}  // namespace spectra::data
