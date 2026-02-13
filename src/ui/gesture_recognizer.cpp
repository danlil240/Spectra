#include "gesture_recognizer.hpp"

#include <cmath>

namespace plotix {

// ─── Scroll ─────────────────────────────────────────────────────────────────

void GestureRecognizer::on_scroll(double dx, double dy, bool is_trackpad) {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_scroll_time_).count();

    // Trackpad heuristic: if we get many scroll events within 50ms,
    // it's likely a trackpad (mouse wheels send discrete, spaced events).
    if (elapsed < 50) {
        ++rapid_scroll_count_;
    } else {
        rapid_scroll_count_ = 1;
    }
    last_scroll_time_ = now;

    // Override caller's hint if heuristic strongly suggests trackpad
    if (rapid_scroll_count_ > 3) {
        is_trackpad = true;
    }

    last_is_trackpad_ = is_trackpad;
    scroll_accum_dx_ += static_cast<float>(dx);
    scroll_accum_dy_ += static_cast<float>(dy);
}

// ─── Pinch ──────────────────────────────────────────────────────────────────

void GestureRecognizer::on_pinch(float scale, float cx, float cy) {
    pinching_    = true;
    pinch_scale_ = scale;
    pinch_cx_    = cx;
    pinch_cy_    = cy;
}

void GestureRecognizer::end_pinch() {
    pinching_    = false;
    pinch_scale_ = 1.0f;
}

// ─── Scroll accumulator ────────────────────────────────────────────────────

float GestureRecognizer::consumed_scroll_dx() {
    float v = scroll_accum_dx_;
    scroll_accum_dx_ = 0.0f;
    return v;
}

float GestureRecognizer::consumed_scroll_dy() {
    float v = scroll_accum_dy_;
    scroll_accum_dy_ = 0.0f;
    return v;
}

// ─── Double-click ───────────────────────────────────────────────────────────

bool GestureRecognizer::on_click(double x, double y) {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_click_time_).count();

    float dist = std::sqrt(
        static_cast<float>((x - last_click_x_) * (x - last_click_x_) +
                           (y - last_click_y_) * (y - last_click_y_)));

    bool is_double = (elapsed < double_click_time_ms_) &&
                     (dist < double_click_dist_);

    last_click_time_ = now;
    last_click_x_ = x;
    last_click_y_ = y;

    return is_double;
}

} // namespace plotix
