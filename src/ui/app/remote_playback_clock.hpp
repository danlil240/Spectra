#pragma once

// Timeline ↔ animation clock sync across processes (multiproc runtime).
//
// In multiproc the timeline panel lives in the window agent while the user's
// on_frame callback runs in the app process.  The agent sends the timeline
// transport (state + playhead) as DiffOp::SET_ANIM_PLAYBACK whenever it
// changes; the app keeps a RemotePlaybackClock per figure and renders the
// callback at that time, extrapolating between messages while playing.
//
// Header-only and free of IPC / UI dependencies so both sides are unit
// tested directly (tests/unit/test_remote_playback_clock.cpp).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spectra
{

// Wire values of SET_ANIM_PLAYBACK's series_index field.
enum class RemotePlaybackState : uint8_t
{
    Stopped = 0,
    Playing = 1,
    Paused  = 2,
};

// App side: the animation clock of one figure, driven by the agent.
class RemotePlaybackClock
{
   public:
    // Adopt a transport update received at `now_s` (any monotonic seconds).
    void apply(RemotePlaybackState state, double playhead, double now_s)
    {
        state_    = state;
        base_     = std::max(0.0, playhead);
        base_now_ = now_s;
        synced_   = true;
    }

    // False until the first update: the app keeps its own clock until then.
    bool                synced() const { return synced_; }
    RemotePlaybackState state() const { return state_; }
    bool                playing() const { return state_ == RemotePlaybackState::Playing; }

    // Animation time at `now_s`.
    double time(double now_s) const
    {
        if (!playing())
            return base_;
        return base_ + std::max(0.0, now_s - base_now_);
    }

   private:
    RemotePlaybackState state_    = RemotePlaybackState::Stopped;
    double              base_     = 0.0;
    double              base_now_ = 0.0;
    bool                synced_   = false;
};

// Agent side: decides which frames send a SET_ANIM_PLAYBACK.  Sends on the
// first frame, a figure switch, a state change, any playhead change while not
// playing, a jump while playing (scrub, step, loop wrap) and a periodic
// resync while playing so the app's extrapolation cannot drift.
class PlaybackSendFilter
{
   public:
    static constexpr double JUMP_S   = 0.05;   // playhead deviation that counts as a jump
    static constexpr double RESYNC_S = 1.0;    // periodic resync interval while playing

    bool should_send(uint64_t figure_id, RemotePlaybackState state, double playhead, double now_s)
    {
        bool send = !has_prev_ || figure_id != figure_ || state != state_;
        if (!send)
        {
            if (state == RemotePlaybackState::Playing)
            {
                const double expected = playhead_ + (now_s - now_);
                send = std::abs(playhead - expected) > JUMP_S || now_s - last_send_ >= RESYNC_S;
            }
            else
            {
                send = std::abs(playhead - playhead_) > 1e-6;
            }
        }
        figure_   = figure_id;
        state_    = state;
        playhead_ = playhead;
        now_      = now_s;
        has_prev_ = true;
        if (send)
            last_send_ = now_s;
        return send;
    }

   private:
    uint64_t            figure_    = 0;
    RemotePlaybackState state_     = RemotePlaybackState::Stopped;
    double              playhead_  = 0.0;
    double              now_       = 0.0;
    double              last_send_ = 0.0;
    bool                has_prev_  = false;
};

}   // namespace spectra
