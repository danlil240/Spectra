#pragma once

#include <spectra/axes.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{

// Sonification — audio representation of 2D series data.
//
// Maps the x-axis range to time and the y-axis range to pitch (frequency).
// Generates a monophonic WAV file that plays back as a tone sweep.
//
// Algorithm:
//   - Duration: `duration_sec` seconds total (default 3 s).
//   - Sample rate: 44100 Hz.
//   - Pitch mapping: y_min → `freq_lo` Hz, y_max → `freq_hi` Hz (linear).
//   - Amplitude: constant 0.5 (no clipping for any mapped value).

struct SonificationParams
{
    float    duration_sec = 3.0f;
    float    freq_lo_hz   = 220.0f;   // pitch for y_min (A3)
    float    freq_hi_hz   = 880.0f;   // pitch for y_max (A5)
    uint32_t sample_rate  = 44100u;
    float    amplitude    = 0.5f;
};

// Generate PCM audio (16-bit signed, mono) for the visible portion of the
// first LineSeries or ScatterSeries found in `axes`.
// Returns an empty vector if no compatible series is present.
std::vector<int16_t> sonify_axes(const Axes& axes, const SonificationParams& params = {});

// Encode PCM data to an uncompressed WAV file.
// Returns true on success.
bool write_wav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t sample_rate);

// Convenience: sonify + write to WAV in one call.
// Returns true on success.
bool sonify_axes_to_wav(const Axes&                axes,
                        const std::string&         path,
                        const SonificationParams&  params = {});

}   // namespace spectra
