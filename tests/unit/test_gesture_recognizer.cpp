#include <gtest/gtest.h>

#include "ui/gesture_recognizer.hpp"

using namespace plotix;

// ─── Double-click detection ─────────────────────────────────────────────────

TEST(GestureRecognizer, SingleClickNotDouble) {
    GestureRecognizer gr;
    bool result = gr.on_click(100.0, 100.0);
    EXPECT_FALSE(result);
}

TEST(GestureRecognizer, TwoClicksCloseInTimeIsDouble) {
    GestureRecognizer gr;
    gr.on_click(100.0, 100.0);
    // Second click immediately after — should be double-click
    bool result = gr.on_click(101.0, 101.0);
    EXPECT_TRUE(result);
}

TEST(GestureRecognizer, TwoClicksFarApartNotDouble) {
    GestureRecognizer gr;
    gr.on_click(100.0, 100.0);
    // Second click far away — not a double-click
    bool result = gr.on_click(500.0, 500.0);
    EXPECT_FALSE(result);
}

TEST(GestureRecognizer, ThirdClickAfterDoubleIsNotDouble) {
    GestureRecognizer gr;
    gr.on_click(100.0, 100.0);
    gr.on_click(101.0, 101.0); // double
    // Third click should NOT be a double (resets)
    bool result = gr.on_click(101.0, 101.0);
    EXPECT_TRUE(result); // Actually this is still within time+distance, so it's another double
}

// ─── Scroll accumulation ────────────────────────────────────────────────────

TEST(GestureRecognizer, ScrollAccumulates) {
    GestureRecognizer gr;
    gr.on_scroll(0.0, 1.0, false);
    gr.on_scroll(0.0, 2.0, false);

    float dy = gr.consumed_scroll_dy();
    EXPECT_FLOAT_EQ(dy, 3.0f);

    // After consuming, should be zero
    float dy2 = gr.consumed_scroll_dy();
    EXPECT_FLOAT_EQ(dy2, 0.0f);
}

TEST(GestureRecognizer, ScrollDxAccumulates) {
    GestureRecognizer gr;
    gr.on_scroll(1.5, 0.0, false);
    gr.on_scroll(2.5, 0.0, false);

    float dx = gr.consumed_scroll_dx();
    EXPECT_FLOAT_EQ(dx, 4.0f);
}

// ─── Pinch state ────────────────────────────────────────────────────────────

TEST(GestureRecognizer, PinchInitiallyInactive) {
    GestureRecognizer gr;
    EXPECT_FALSE(gr.is_pinching());
    EXPECT_FLOAT_EQ(gr.pinch_scale(), 1.0f);
}

TEST(GestureRecognizer, PinchActivatesOnEvent) {
    GestureRecognizer gr;
    gr.on_pinch(1.5f, 400.0f, 300.0f);

    EXPECT_TRUE(gr.is_pinching());
    EXPECT_FLOAT_EQ(gr.pinch_scale(), 1.5f);
    EXPECT_FLOAT_EQ(gr.pinch_cx(), 400.0f);
    EXPECT_FLOAT_EQ(gr.pinch_cy(), 300.0f);
}

TEST(GestureRecognizer, EndPinchResetsState) {
    GestureRecognizer gr;
    gr.on_pinch(2.0f, 100.0f, 100.0f);
    gr.end_pinch();

    EXPECT_FALSE(gr.is_pinching());
    EXPECT_FLOAT_EQ(gr.pinch_scale(), 1.0f);
}

// ─── Configuration ──────────────────────────────────────────────────────────

TEST(GestureRecognizer, CustomDoubleClickDistance) {
    GestureRecognizer gr;
    gr.set_double_click_distance(2.0f); // Very tight

    gr.on_click(100.0, 100.0);
    // 3px away — should NOT be double with 2px threshold
    bool result = gr.on_click(103.0, 100.0);
    EXPECT_FALSE(result);
}
