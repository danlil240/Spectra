#pragma once

// RosTimeClock — master time bus for Spectra ROS (live vs bag playback).
//
// Panels that show time-varying data read playhead_sec / mode from
// RosWorkspaceState::clock and align their views to a single timeline.

#include <cstdint>

namespace spectra::adapters::ros2
{

enum class RosTimeMode
{
    Live,   // wall / ROS header time from live subscriptions
    Bag,    // bag playhead drives plots, echo, and transport readout
};

struct RosTimeClock
{
    RosTimeMode mode{RosTimeMode::Live};

    // Seconds from bag start (plot X axis in bag mode).
    double playhead_sec{0.0};
    double bag_duration_sec{0.0};

    bool   is_playing{false};
    double rate{1.0};

    // Absolute ROS epoch timestamp of the first bag message (nanoseconds).
    int64_t bag_start_time_ns{0};

    bool is_bag_mode() const { return mode == RosTimeMode::Bag; }

    // Plot scroll controller coordinate (bag-relative seconds).
    double plot_now_sec() const { return playhead_sec; }

    void enter_live_mode()
    {
        mode              = RosTimeMode::Live;
        playhead_sec      = 0.0;
        bag_duration_sec  = 0.0;
        is_playing        = false;
        rate              = 1.0;
        bag_start_time_ns = 0;
    }

    void enter_bag_mode(double duration_sec, int64_t start_time_ns)
    {
        mode              = RosTimeMode::Bag;
        playhead_sec      = 0.0;
        bag_duration_sec  = duration_sec;
        is_playing        = false;
        rate              = 1.0;
        bag_start_time_ns = start_time_ns;
    }

    void update_bag_transport(double playhead, bool playing, double playback_rate)
    {
        playhead_sec = playhead;
        is_playing   = playing;
        rate         = playback_rate;
    }
};

}   // namespace spectra::adapters::ros2
