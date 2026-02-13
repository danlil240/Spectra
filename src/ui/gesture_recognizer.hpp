#pragma once

#include <chrono>

namespace plotix {

// Detects trackpad pinch-to-zoom and distinguishes trackpad smooth scroll
// from discrete mouse wheel ticks. Tracks double-click timing.
class GestureRecognizer {
public:
    GestureRecognizer() = default;

    // Called on every scroll event. is_trackpad should be true for high-precision
    // trackpad events (GLFW doesn't distinguish natively, so we use heuristics).
    void on_scroll(double dx, double dy, bool is_trackpad);

    // Called on pinch gesture (macOS trackpad, or synthesized from Ctrl+scroll).
    void on_pinch(float scale, float cx, float cy);

    // Called on mouse button press/release for double-click detection.
    // Returns true if this press constitutes a double-click.
    bool on_click(double x, double y);

    // Query state
    bool  is_pinching() const { return pinching_; }
    float pinch_scale() const { return pinch_scale_; }
    float pinch_cx() const    { return pinch_cx_; }
    float pinch_cy() const    { return pinch_cy_; }

    // Accumulated smooth scroll delta since last consume (trackpad inertia).
    float consumed_scroll_dx();
    float consumed_scroll_dy();

    // Was the last scroll event from a trackpad?
    bool last_scroll_is_trackpad() const { return last_is_trackpad_; }

    // Reset pinch state (call when pinch gesture ends).
    void end_pinch();

    // Configuration
    void set_double_click_time_ms(int ms) { double_click_time_ms_ = ms; }
    void set_double_click_distance(float px) { double_click_dist_ = px; }

private:
    // Pinch state
    bool  pinching_    = false;
    float pinch_scale_ = 1.0f;
    float pinch_cx_    = 0.0f;
    float pinch_cy_    = 0.0f;

    // Smooth scroll accumulator
    float scroll_accum_dx_ = 0.0f;
    float scroll_accum_dy_ = 0.0f;
    bool  last_is_trackpad_ = false;

    // Trackpad detection heuristic: trackpad sends many small deltas in bursts
    int   rapid_scroll_count_ = 0;
    using Clock = std::chrono::steady_clock;
    Clock::time_point last_scroll_time_{};

    // Double-click detection
    Clock::time_point last_click_time_{};
    double last_click_x_ = 0.0;
    double last_click_y_ = 0.0;
    int    double_click_time_ms_ = 400;
    float  double_click_dist_    = 5.0f;
};

} // namespace plotix
