#pragma once

// event_wait.hpp — backend-neutral helpers for event-loop waits.

#include <cmath>
#include <cstdint>
#include <limits>

namespace spectra
{

// Millisecond timeout for SDL_WaitEventTimeout() equivalent to a
// glfwWaitEventsTimeout(seconds) call: rounded up so a sub-millisecond wait
// still sleeps instead of busy-spinning, clamped to [0, INT32_MAX], and 0 for
// non-positive or NaN input.
inline int32_t sdl3_wait_timeout_ms(double seconds)
{
    if (!(seconds > 0.0))
        return 0;
    const double ms = std::ceil(seconds * 1000.0);
    if (ms >= static_cast<double>(std::numeric_limits<int32_t>::max()))
        return std::numeric_limits<int32_t>::max();
    return static_cast<int32_t>(ms);
}

}   // namespace spectra
