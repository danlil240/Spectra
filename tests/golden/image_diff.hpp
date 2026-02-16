#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace spectra::test
{

struct DiffResult
{
    double mean_absolute_error = 0.0;  // Average per-channel absolute diff [0..255]
    double max_absolute_error = 0.0;   // Worst single-channel diff
    size_t differing_pixels = 0;       // Pixels with any channel diff > threshold
    size_t total_pixels = 0;
    double percent_different = 0.0;  // differing_pixels / total_pixels * 100

    bool passed(double tolerance_percent = 1.0, double max_mae = 2.0) const
    {
        return percent_different <= tolerance_percent && mean_absolute_error <= max_mae;
    }
};

// Compare two RGBA pixel buffers of identical dimensions.
// threshold: per-channel difference below which a pixel is considered matching.
inline DiffResult compare_images(const uint8_t* actual,
                                 const uint8_t* expected,
                                 uint32_t width,
                                 uint32_t height,
                                 uint8_t threshold = 2)
{
    DiffResult result;
    result.total_pixels = static_cast<size_t>(width) * height;

    if (result.total_pixels == 0)
        return result;

    double sum_abs = 0.0;
    double max_abs = 0.0;

    for (size_t i = 0; i < result.total_pixels; ++i)
    {
        size_t base = i * 4;
        bool pixel_differs = false;

        for (int c = 0; c < 4; ++c)
        {
            double diff = std::abs(static_cast<double>(actual[base + c])
                                   - static_cast<double>(expected[base + c]));
            sum_abs += diff;
            if (diff > max_abs)
                max_abs = diff;
            if (diff > threshold)
                pixel_differs = true;
        }

        if (pixel_differs)
        {
            ++result.differing_pixels;
        }
    }

    result.mean_absolute_error = sum_abs / (result.total_pixels * 4.0);
    result.max_absolute_error = max_abs;
    result.percent_different =
        (static_cast<double>(result.differing_pixels) / static_cast<double>(result.total_pixels))
        * 100.0;
    return result;
}

// Write a simple diff visualization: red pixels where images differ.
// Returns RGBA buffer of same dimensions.
inline std::vector<uint8_t> generate_diff_image(const uint8_t* actual,
                                                const uint8_t* expected,
                                                uint32_t width,
                                                uint32_t height,
                                                uint8_t threshold = 2)
{
    size_t total = static_cast<size_t>(width) * height;
    std::vector<uint8_t> diff(total * 4);

    for (size_t i = 0; i < total; ++i)
    {
        size_t base = i * 4;
        bool pixel_differs = false;

        for (int c = 0; c < 4; ++c)
        {
            int d =
                std::abs(static_cast<int>(actual[base + c]) - static_cast<int>(expected[base + c]));
            if (d > threshold)
                pixel_differs = true;
        }

        if (pixel_differs)
        {
            diff[base + 0] = 255;  // Red
            diff[base + 1] = 0;
            diff[base + 2] = 0;
            diff[base + 3] = 255;
        }
        else
        {
            // Dimmed version of actual
            diff[base + 0] = actual[base + 0] / 3;
            diff[base + 1] = actual[base + 1] / 3;
            diff[base + 2] = actual[base + 2] / 3;
            diff[base + 3] = 255;
        }
    }

    return diff;
}

// Load raw RGBA from a simple binary file (header: uint32_t width, uint32_t height, then RGBA data)
inline bool load_raw_rgba(const std::string& path,
                          std::vector<uint8_t>& pixels,
                          uint32_t& width,
                          uint32_t& height)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    f.read(reinterpret_cast<char*>(&width), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&height), sizeof(uint32_t));

    if (width == 0 || height == 0 || width > 16384 || height > 16384)
        return false;

    size_t size = static_cast<size_t>(width) * height * 4;
    pixels.resize(size);
    f.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(size));

    return f.good();
}

// Save raw RGBA to a simple binary file
inline bool save_raw_rgba(const std::string& path,
                          const uint8_t* pixels,
                          uint32_t width,
                          uint32_t height)
{
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    f.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));

    size_t size = static_cast<size_t>(width) * height * 4;
    f.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(size));

    return f.good();
}

}  // namespace spectra::test
