#pragma once

#include <cstdint>

namespace spectra
{

struct Frame
{
    float elapsed_sec = 0.0f;
    float dt = 0.0f;
    uint64_t number = 0;
    bool paused = false;

    float elapsed_seconds() const { return elapsed_sec; }
    float delta_time() const { return dt; }
    uint64_t frame_number() const { return number; }
};

}  // namespace spectra
