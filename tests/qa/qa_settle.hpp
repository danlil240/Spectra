#pragma once
// qa_settle.hpp — bounded "wait until stable" loop of the Spectra QA agent.
//
// Kept header-only and free of Spectra/Vulkan dependencies so the rule is unit
// tested (tests/unit/test_qa_settle.cpp).
//
// UI animations (layout slides, nav-rail width, theme cross-fades, crosshair
// fade-in) advance by elapsed time (dt), not by frame count, and window-system
// effects (swapchain recreation after a resize, window moves) complete after an
// unknown number of frames.  A fixed `pump_frames(N)` before asserting on such
// a value therefore races the animation: on a fast machine N frames cover too
// little time, on a slow one the asynchronous event may not have arrived yet.
//
// wait_until_stable() pumps frames until a predicate has held for
// `stable_frames` consecutive frames.  It gives up only once BOTH the frame
// budget and the wall-time budget are spent, so a slow software rasterizer
// still gets enough frames and a fast GPU still gets enough elapsed time.  A
// hard frame cap guards against a stalled clock.

#include <cstdint>

namespace spectra::qa
{

struct SettleBudget
{
    uint64_t max_frames      = 180;     // give up after at least this many frames ...
    double   max_seconds     = 5.0;     // ... AND at least this much wall time
    uint32_t stable_frames   = 2;       // consecutive frames the predicate must hold
    uint64_t hard_max_frames = 20000;   // absolute cap, whatever the clock says
};

struct SettleResult
{
    bool     settled = false;   // predicate held for `stable_frames` frames
    bool     aborted = false;   // step() asked to stop (critical issue, run deadline)
    uint64_t frames  = 0;       // frames pumped
    double   seconds = 0.0;     // wall time spent
};

// step():    pump one frame; return false when the run must stop.
// settled(): the state predicate, evaluated after every pumped frame.
// now():     monotonic time in seconds.
template <class Step, class Pred, class Clock>
SettleResult wait_until_stable(Step&&              step,
                               Pred&&              settled,
                               Clock&&             now,
                               const SettleBudget& budget = {})
{
    SettleResult   r;
    const double   t0     = now();
    const uint32_t need   = budget.stable_frames > 0 ? budget.stable_frames : 1;
    uint32_t       streak = 0;
    for (;;)
    {
        if (!step())
        {
            r.aborted = true;
            break;
        }
        ++r.frames;
        r.seconds = now() - t0;
        streak    = settled() ? streak + 1 : 0;
        if (streak >= need)
        {
            r.settled = true;
            break;
        }
        const bool budget_spent = r.frames >= budget.max_frames && r.seconds >= budget.max_seconds;
        if (budget_spent || r.frames >= budget.hard_max_frames)
            break;
    }
    r.seconds = now() - t0;
    return r;
}

}   // namespace spectra::qa
