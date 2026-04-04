#include "sonification.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#include <spectra/series.hpp>

namespace spectra
{

namespace
{

// Write a little-endian integer of `bytes` width to a stream.
template <typename T>
static void write_le(std::ostream& out, T value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
    {
        char b = static_cast<char>(value & 0xFF);
        out.write(&b, 1);
        value >>= 8;
    }
}

}   // anonymous namespace

// ---------------------------------------------------------------------------
// sonify_axes
// ---------------------------------------------------------------------------
std::vector<int16_t> sonify_axes(const Axes& axes, const SonificationParams& params)
{
    // Find the first compatible 2D series.
    const LineSeries*    line    = nullptr;
    const ScatterSeries* scatter = nullptr;
    for (const auto& sp : axes.series())
    {
        if (!sp || !sp->visible())
            continue;
        if (auto* l = dynamic_cast<const LineSeries*>(sp.get()))
        {
            line = l;
            break;
        }
        if (auto* s = dynamic_cast<const ScatterSeries*>(sp.get()))
        {
            scatter = s;
            break;
        }
    }
    if (!line && !scatter)
        return {};

    const std::span<const float> xd = line ? line->x_data() : scatter->x_data();
    const std::span<const float> yd = line ? line->y_data() : scatter->y_data();
    if (xd.empty() || yd.empty())
        return {};

    // Compute y range for frequency mapping.
    float ymin = *std::min_element(yd.begin(), yd.end());
    float ymax = *std::max_element(yd.begin(), yd.end());
    if (ymax - ymin < std::numeric_limits<float>::epsilon())
        ymax = ymin + 1.0f;   // degenerate — use constant pitch

    const uint32_t total_samples =
        static_cast<uint32_t>(params.duration_sec * static_cast<float>(params.sample_rate));
    std::vector<int16_t> pcm(total_samples, 0);

    // Number of data points and total samples map linearly.
    const size_t n     = xd.size();
    double       phase = 0.0;   // accumulated phase in radians

    for (uint32_t si = 0; si < total_samples; ++si)
    {
        // Which data point does this sample correspond to?
        double t    = static_cast<double>(si) / static_cast<double>(total_samples - 1);
        size_t di   = static_cast<size_t>(t * static_cast<double>(n - 1));
        di          = std::min(di, n - 1);
        float y_val = yd[di];

        // Map y_val to frequency (linear).
        float  norm = (y_val - ymin) / (ymax - ymin);
        double freq = static_cast<double>(params.freq_lo_hz)
                      + static_cast<double>(norm)
                            * static_cast<double>(params.freq_hi_hz - params.freq_lo_hz);

        // Advance phase and generate sample.
        phase = std::fmod(phase + 2.0 * M_PI * freq / static_cast<double>(params.sample_rate),
                          2.0 * M_PI);

        float sample = params.amplitude * static_cast<float>(std::sin(phase));
        pcm[si]      = static_cast<int16_t>(sample * 32767.0f);
    }

    return pcm;
}

// ---------------------------------------------------------------------------
// write_wav
// ---------------------------------------------------------------------------
bool write_wav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t sample_rate)
{
    if (pcm.empty())
        return false;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    constexpr uint16_t channels    = 1;
    constexpr uint16_t bit_depth   = 16;
    uint32_t           byte_rate   = sample_rate * channels * (bit_depth / 8u);
    uint16_t           block_align = static_cast<uint16_t>(channels * (bit_depth / 8u));
    uint32_t           data_bytes  = static_cast<uint32_t>(pcm.size()) * sizeof(int16_t);
    uint32_t           riff_size   = 36u + data_bytes;

    // RIFF chunk descriptor
    f.write("RIFF", 4);
    write_le(f, riff_size, 4);
    f.write("WAVE", 4);

    // fmt sub-chunk
    f.write("fmt ", 4);
    write_le(f, uint32_t{16}, 4);   // PCM sub-chunk size
    write_le(f, uint16_t{1}, 2);    // PCM format
    write_le(f, channels, 2);
    write_le(f, sample_rate, 4);
    write_le(f, byte_rate, 4);
    write_le(f, block_align, 2);
    write_le(f, bit_depth, 2);

    // data sub-chunk
    f.write("data", 4);
    write_le(f, data_bytes, 4);
    f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_bytes));

    return f.good();
}

// ---------------------------------------------------------------------------
// sonify_axes_to_wav
// ---------------------------------------------------------------------------
bool sonify_axes_to_wav(const Axes& axes, const std::string& path, const SonificationParams& params)
{
    auto pcm = sonify_axes(axes, params);
    if (pcm.empty())
        return false;
    return write_wav(path, pcm, params.sample_rate);
}

}   // namespace spectra
