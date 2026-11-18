#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Absolute baseline / output directories, set by tests/CMakeLists.txt so the result does
// not depend on the working directory (ctest runs from build/tests) or on how the
// compiler spells __FILE__ (ccache's base_dir makes it relative). The
// SPECTRA_GOLDEN_BASELINE_DIR / SPECTRA_GOLDEN_OUTPUT_DIR environment variables still
// override them at run time.
#ifndef SPECTRA_GOLDEN_BASELINE_DIR_DEFAULT
    #error "SPECTRA_GOLDEN_BASELINE_DIR_DEFAULT must be defined (see tests/CMakeLists.txt)"
#endif
#ifndef SPECTRA_GOLDEN_OUTPUT_DIR_DEFAULT
    #error "SPECTRA_GOLDEN_OUTPUT_DIR_DEFAULT must be defined (see tests/CMakeLists.txt)"
#endif

namespace spectra::test
{

// Golden-image comparison budget.
//
// A pixel "differs" when any channel differs by more than pixel_threshold. Three
// budgets then apply:
//   * global:  at most max_percent of all pixels (and at most max_differing_pixels)
//              may differ,
//   * local:   no tile_size x tile_size tile may have more than
//              max_tile_fraction of its pixels differing, and
//   * mean:    the mean absolute channel error stays below max_mae.
// The local budget is what catches small but real regressions -- a missing or
// flipped marker, a dropped glyph, a line end moved by a pixel -- that are far
// below any global percentage.
//
// The defaults are calibrated against lavapipe (Mesa 25.2 / LLVM 20, the CI
// driver), which renders every scene bit-identically from run to run. They leave
// room for the noise another llvmpipe build or CPU could plausibly add -- a
// uniform +-1/2 LSB drift (absorbed by pixel_threshold and max_mae) and a handful
// of scattered edge-coverage flips (at most 5 per 16 px tile, ~150 per 640x480
// frame) -- but fail on real changes: a colormap swap whose channels move by 3-6
// levels, a legend swatch or line cap one pixel shorter, a shifted line. The
// previous defaults (threshold 24, 0.5%, 10% per tile) let all of those through.
// Scenes that turn out to be noisier on CI get a per-test GoldenTolerance rather
// than a looser global one.
struct GoldenTolerance
{
    uint8_t pixel_threshold      = 2;
    double  max_percent          = 0.05;
    double  max_mae              = 1.0;
    double  max_tile_fraction    = 0.02;
    size_t  max_differing_pixels = std::numeric_limits<size_t>::max();
};

constexpr uint32_t kDiffTileSize = 16;

struct DiffResult
{
    double   mean_absolute_error  = 0.0;   // Average per-channel absolute diff [0..255]
    double   max_absolute_error   = 0.0;   // Worst single-channel diff
    size_t   differing_pixels     = 0;     // Pixels with any channel diff > threshold
    size_t   total_pixels         = 0;
    double   percent_different    = 0.0;   // differing_pixels / total_pixels * 100
    size_t   worst_tile_differing = 0;     // Differing pixels in the worst kDiffTileSize tile
    size_t   worst_tile_area      = 1;     // Pixel count of that tile (edge tiles are smaller)
    uint32_t worst_tile_x         = 0;     // Top-left pixel of that tile
    uint32_t worst_tile_y         = 0;

    double worst_tile_fraction() const
    {
        return static_cast<double>(worst_tile_differing) / static_cast<double>(worst_tile_area);
    }

    bool passed(double tolerance_percent = 1.0, double max_mae = 2.0) const
    {
        return percent_different <= tolerance_percent && mean_absolute_error <= max_mae;
    }

    bool passed(const GoldenTolerance& tol) const
    {
        return percent_different <= tol.max_percent && mean_absolute_error <= tol.max_mae
               && worst_tile_fraction() <= tol.max_tile_fraction
               && differing_pixels <= tol.max_differing_pixels;
    }

    std::string describe(const GoldenTolerance& tol) const
    {
        std::ostringstream os;
        os << "  Differing pixels:    " << differing_pixels << " / " << total_pixels << " ("
           << percent_different << "%, max allowed: " << tol.max_percent << "%)\n"
           << "  Mean absolute error: " << mean_absolute_error << " (max allowed: " << tol.max_mae
           << ")\n"
           << "  Worst " << kDiffTileSize << "px tile: " << worst_tile_differing << " / "
           << worst_tile_area << " differing at (" << worst_tile_x << ", " << worst_tile_y
           << ") (max allowed fraction: " << tol.max_tile_fraction << ")\n"
           << "  Max channel diff:    " << max_absolute_error
           << " (per-pixel threshold: " << static_cast<int>(tol.pixel_threshold) << ")\n";
        if (tol.max_differing_pixels != std::numeric_limits<size_t>::max())
            os << "  Differing-pixel cap: " << tol.max_differing_pixels << "\n";
        return os.str();
    }

    // One-line summary printed for every compared scene (passing or not), so CI logs
    // record how far each render is from its baseline and tolerances can be
    // re-calibrated from data.
    std::string stats_line(const std::string& scene) const
    {
        std::ostringstream os;
        os << "[GOLDEN] " << scene << ": max_channel_diff=" << max_absolute_error
           << " mae=" << mean_absolute_error << " differing=" << differing_pixels << "/"
           << total_pixels << " worst_tile=" << worst_tile_differing << "/" << worst_tile_area;
        return os.str();
    }
};

// Compare two RGBA pixel buffers of identical dimensions.
// threshold: per-channel difference below which a pixel is considered matching.
inline DiffResult compare_images(const uint8_t* actual,
                                 const uint8_t* expected,
                                 uint32_t       width,
                                 uint32_t       height,
                                 uint8_t        threshold = 2)
{
    DiffResult result;
    result.total_pixels = static_cast<size_t>(width) * height;

    if (result.total_pixels == 0)
        return result;

    double sum_abs = 0.0;
    double max_abs = 0.0;

    const uint32_t      tiles_x = (width + kDiffTileSize - 1) / kDiffTileSize;
    const uint32_t      tiles_y = (height + kDiffTileSize - 1) / kDiffTileSize;
    std::vector<size_t> tile_counts(static_cast<size_t>(tiles_x) * tiles_y, 0);

    for (size_t i = 0; i < result.total_pixels; ++i)
    {
        size_t base          = i * 4;
        bool   pixel_differs = false;

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
            const auto px = static_cast<uint32_t>(i % width);
            const auto py = static_cast<uint32_t>(i / width);
            ++tile_counts[static_cast<size_t>(py / kDiffTileSize) * tiles_x + px / kDiffTileSize];
        }
    }

    double worst = -1.0;
    for (uint32_t ty = 0; ty < tiles_y; ++ty)
    {
        for (uint32_t tx = 0; tx < tiles_x; ++tx)
        {
            const uint32_t tw   = std::min(kDiffTileSize, width - tx * kDiffTileSize);
            const uint32_t th   = std::min(kDiffTileSize, height - ty * kDiffTileSize);
            const size_t   area = static_cast<size_t>(tw) * th;
            const size_t   n    = tile_counts[static_cast<size_t>(ty) * tiles_x + tx];
            const double   frac = static_cast<double>(n) / static_cast<double>(area);
            if (frac > worst)
            {
                worst                       = frac;
                result.worst_tile_differing = n;
                result.worst_tile_area      = area;
                result.worst_tile_x         = tx * kDiffTileSize;
                result.worst_tile_y         = ty * kDiffTileSize;
            }
        }
    }

    result.mean_absolute_error = sum_abs / (result.total_pixels * 4.0);
    result.max_absolute_error  = max_abs;
    result.percent_different =
        (static_cast<double>(result.differing_pixels) / static_cast<double>(result.total_pixels))
        * 100.0;
    return result;
}

// Write a simple diff visualization: red pixels where images differ.
// Returns RGBA buffer of same dimensions.
inline std::vector<uint8_t> generate_diff_image(const uint8_t* actual,
                                                const uint8_t* expected,
                                                uint32_t       width,
                                                uint32_t       height,
                                                uint8_t        threshold = 2)
{
    size_t               total = static_cast<size_t>(width) * height;
    std::vector<uint8_t> diff(total * 4);

    for (size_t i = 0; i < total; ++i)
    {
        size_t base          = i * 4;
        bool   pixel_differs = false;

        for (int c = 0; c < 4; ++c)
        {
            int d =
                std::abs(static_cast<int>(actual[base + c]) - static_cast<int>(expected[base + c]));
            if (d > threshold)
                pixel_differs = true;
        }

        if (pixel_differs)
        {
            diff[base + 0] = 255;   // Red
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
inline bool load_raw_rgba(const std::string&    path,
                          std::vector<uint8_t>& pixels,
                          uint32_t&             width,
                          uint32_t&             height)
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
                          const uint8_t*     pixels,
                          uint32_t           width,
                          uint32_t           height)
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

// A missing baseline is a test failure by default, so a wrong baseline directory or a
// scene added without a committed baseline cannot pass silently. Set
// SPECTRA_GOLDEN_STRICT=0 to skip such scenes instead (e.g. while drafting a new scene).
inline bool golden_strict()
{
    const char* env = std::getenv("SPECTRA_GOLDEN_STRICT");
    return !(env && std::string(env) == "0");
}

}   // namespace spectra::test
