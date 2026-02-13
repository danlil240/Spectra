#include <gtest/gtest.h>

#include "core/layout.hpp"

using namespace plotix;

TEST(SubplotLayout, SingleCell) {
    auto rects = compute_subplot_layout(1280.0f, 720.0f, 1, 1);
    ASSERT_EQ(rects.size(), 1u);

    // Default margins: left=60, right=40, top=40, bottom=50
    auto& r = rects[0];
    EXPECT_FLOAT_EQ(r.x, 60.0f);
    EXPECT_FLOAT_EQ(r.y, 40.0f);
    EXPECT_FLOAT_EQ(r.w, 1280.0f - 60.0f - 40.0f);  // 1180
    EXPECT_FLOAT_EQ(r.h, 720.0f - 40.0f - 50.0f);    // 630
}

TEST(SubplotLayout, TwoByOneGrid) {
    auto rects = compute_subplot_layout(1920.0f, 1080.0f, 2, 1);
    ASSERT_EQ(rects.size(), 2u);

    float cell_h = 1080.0f / 2.0f;  // 540

    // Row 0, Col 0 (top)
    EXPECT_FLOAT_EQ(rects[0].x, 60.0f);
    EXPECT_FLOAT_EQ(rects[0].y, 40.0f);
    EXPECT_FLOAT_EQ(rects[0].w, 1920.0f - 60.0f - 40.0f);
    EXPECT_FLOAT_EQ(rects[0].h, cell_h - 40.0f - 50.0f);

    // Row 1, Col 0 (bottom)
    EXPECT_FLOAT_EQ(rects[1].x, 60.0f);
    EXPECT_FLOAT_EQ(rects[1].y, cell_h + 40.0f);
    EXPECT_FLOAT_EQ(rects[1].w, 1920.0f - 60.0f - 40.0f);
    EXPECT_FLOAT_EQ(rects[1].h, cell_h - 40.0f - 50.0f);
}

TEST(SubplotLayout, OneByTwoGrid) {
    auto rects = compute_subplot_layout(1000.0f, 500.0f, 1, 2);
    ASSERT_EQ(rects.size(), 2u);

    float cell_w = 1000.0f / 2.0f;  // 500

    // Row 0, Col 0 (left)
    EXPECT_FLOAT_EQ(rects[0].x, 60.0f);
    EXPECT_FLOAT_EQ(rects[0].y, 40.0f);
    EXPECT_FLOAT_EQ(rects[0].w, cell_w - 60.0f - 40.0f);
    EXPECT_FLOAT_EQ(rects[0].h, 500.0f - 40.0f - 50.0f);

    // Row 0, Col 1 (right)
    EXPECT_FLOAT_EQ(rects[1].x, cell_w + 60.0f);
    EXPECT_FLOAT_EQ(rects[1].y, 40.0f);
    EXPECT_FLOAT_EQ(rects[1].w, cell_w - 60.0f - 40.0f);
    EXPECT_FLOAT_EQ(rects[1].h, 500.0f - 40.0f - 50.0f);
}

TEST(SubplotLayout, TwoByTwoGrid) {
    auto rects = compute_subplot_layout(800.0f, 600.0f, 2, 2);
    ASSERT_EQ(rects.size(), 4u);

    float cell_w = 400.0f;
    float cell_h = 300.0f;

    // All cells should have the same plot area dimensions
    float expected_w = cell_w - 60.0f - 40.0f;
    float expected_h = cell_h - 40.0f - 50.0f;

    for (size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(rects[i].w, expected_w) << "cell " << i;
        EXPECT_FLOAT_EQ(rects[i].h, expected_h) << "cell " << i;
    }

    // Check positions: row-major order
    // (0,0) top-left
    EXPECT_FLOAT_EQ(rects[0].x, 60.0f);
    EXPECT_FLOAT_EQ(rects[0].y, 40.0f);
    // (0,1) top-right
    EXPECT_FLOAT_EQ(rects[1].x, cell_w + 60.0f);
    EXPECT_FLOAT_EQ(rects[1].y, 40.0f);
    // (1,0) bottom-left
    EXPECT_FLOAT_EQ(rects[2].x, 60.0f);
    EXPECT_FLOAT_EQ(rects[2].y, cell_h + 40.0f);
    // (1,1) bottom-right
    EXPECT_FLOAT_EQ(rects[3].x, cell_w + 60.0f);
    EXPECT_FLOAT_EQ(rects[3].y, cell_h + 40.0f);
}

TEST(SubplotLayout, CustomMargins) {
    Margins m{10.0f, 10.0f, 10.0f, 10.0f};
    auto rects = compute_subplot_layout(100.0f, 100.0f, 1, 1, m);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_FLOAT_EQ(rects[0].x, 10.0f);
    EXPECT_FLOAT_EQ(rects[0].y, 10.0f);
    EXPECT_FLOAT_EQ(rects[0].w, 80.0f);
    EXPECT_FLOAT_EQ(rects[0].h, 80.0f);
}

TEST(SubplotLayout, TinyFigureClampsToZero) {
    // Margins larger than cell — should clamp to 0 width/height
    auto rects = compute_subplot_layout(50.0f, 50.0f, 1, 1);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GE(rects[0].w, 0.0f);
    EXPECT_GE(rects[0].h, 0.0f);
}
