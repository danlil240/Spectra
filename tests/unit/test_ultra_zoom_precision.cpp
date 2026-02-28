// Regression tests for camera-relative rendering precision at ultra zoom.
// Verifies that the double→float conversion via origin subtraction preserves
// sub-pixel accuracy even when data is at large absolute offsets.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers — replicate the key precision-critical paths from renderer.cpp
// without needing GPU or Vulkan.
// ---------------------------------------------------------------------------

// Simulates build_ortho_projection: maps [left, right] × [bottom, top] → NDC.
static void build_ortho(double left, double right, double bottom, double top, float* m)
{
    std::memset(m, 0, 16 * sizeof(float));
    double rl = right - left;
    double tb = top - bottom;
    if (rl == 0.0)
        rl = 1.0;
    if (tb == 0.0)
        tb = 1.0;
    m[0]  = static_cast<float>(2.0 / rl);
    m[5]  = static_cast<float>(-2.0 / tb);
    m[10] = -1.0f;
    m[12] = static_cast<float>(-(right + left) / rl);
    m[13] = static_cast<float>((top + bottom) / tb);
    m[15] = 1.0f;
}

// Simulate the GPU transform: clip_x = m[0]*x + m[12], clip_y = m[5]*y + m[13]
static float ndc_x(const float* m, float x) { return m[0] * x + m[12]; }
static float ndc_y(const float* m, float y) { return m[5] * y + m[13]; }

// Convert NDC [-1,1] to pixel [0, viewport_size]
static float ndc_to_px(float ndc, float viewport_size)
{
    return (ndc * 0.5f + 0.5f) * viewport_size;
}

// ---------------------------------------------------------------------------
// OLD path (absolute coordinates) — reproduces the pre-fix precision issue.
// ---------------------------------------------------------------------------
struct OldResult
{
    float ndc;
    float px;
};

static OldResult old_pipeline(double xlim_min, double xlim_max, float data_x, float vp_w)
{
    float m[16];
    build_ortho(xlim_min, xlim_max, 0.0, 1.0, m);
    float n = ndc_x(m, data_x);
    return {n, ndc_to_px(n, vp_w)};
}

// ---------------------------------------------------------------------------
// NEW path (camera-relative coordinates).
// ---------------------------------------------------------------------------
struct NewResult
{
    float ndc;
    float px;
};

static NewResult new_pipeline(double xlim_min, double xlim_max, float data_x, double origin_x,
                              float vp_w)
{
    double view_cx = (xlim_min + xlim_max) * 0.5;
    double half_rx = (xlim_max - xlim_min) * 0.5;

    // Centered projection (translation ≈ 0)
    float m[16];
    build_ortho(-half_rx, half_rx, -0.5, 0.5, m);

    // Camera-relative data
    float rel_x      = static_cast<float>(static_cast<double>(data_x) - origin_x);
    float data_off_x = static_cast<float>(origin_x - view_cx);
    float gpu_x      = rel_x + data_off_x;

    float n = ndc_x(m, gpu_x);
    return {n, ndc_to_px(n, vp_w)};
}

// ===== Tests =================================================================

class UltraZoomPrecision : public ::testing::Test
{
  protected:
    static constexpr float VP_W = 1920.0f;
};

// At moderate zoom, both old and new paths should give similar results.
TEST_F(UltraZoomPrecision, ModerateZoom_BothPathsAgree)
{
    double xmin = 0.0, xmax = 10.0;
    float  data_x  = 5.0f;
    double origin_x = (xmin + xmax) * 0.5;

    auto old_r = old_pipeline(xmin, xmax, data_x, VP_W);
    auto new_r = new_pipeline(xmin, xmax, data_x, origin_x, VP_W);

    EXPECT_NEAR(old_r.px, new_r.px, 0.5f);   // within half a pixel
}

// Deep zoom at large offset — demonstrates the camera-relative fix.
// At x=1e8, float ULP = 8.  With view range = 100, two data points
// separated by 16 (2 ULPs) should map to visually distinct pixels.
// The old path loses precision because m[0]*p ≈ 2e6 (ULP=0.25),
// but the new path computes on small view-relative values.
TEST_F(UltraZoomPrecision, DeepZoom_LargeOffset_NewPathDistinguishesPoints)
{
    double center = 1.0e8;
    double range  = 100.0;
    double xmin   = center - range * 0.5;
    double xmax   = center + range * 0.5;
    double origin = center;

    // Two points separated by 2 ULPs (16.0 at 1e8) — clearly distinguishable
    float p1 = static_cast<float>(center - 8.0);
    float p2 = static_cast<float>(center + 8.0);

    auto r1 = new_pipeline(xmin, xmax, p1, origin, VP_W);
    auto r2 = new_pipeline(xmin, xmax, p2, origin, VP_W);

    // Expected separation: 2*16/100 = 0.32 NDC → ~307 pixels
    float px_diff = std::abs(r2.px - r1.px);
    EXPECT_GT(px_diff, 250.0f) << "New path should visually separate points at deep zoom";

    // Verify accuracy: expected pixel positions
    float expected_ndc1 = -0.16f;   // 2*(-8)/100
    float expected_ndc2 = 0.16f;
    EXPECT_NEAR(r1.ndc, expected_ndc1, 0.01f) << "NDC of p1 should be accurate";
    EXPECT_NEAR(r2.ndc, expected_ndc2, 0.01f) << "NDC of p2 should be accurate";
}

// Verify that old path has significant error at deep zoom (demonstrates the bug).
// Same scenario as above: center = 1e8, range = 100, points ±8.
// Old path computes m[0]*p ≈ 2e6 where float ULP is 0.25, introducing
// large rounding error in the NDC computation.
TEST_F(UltraZoomPrecision, DeepZoom_LargeOffset_OldPathHasError)
{
    double center = 1.0e8;
    double range  = 100.0;
    double xmin   = center - range * 0.5;
    double xmax   = center + range * 0.5;

    float p1 = static_cast<float>(center - 8.0);   // 1 ULP below center
    float p2 = static_cast<float>(center + 8.0);   // 1 ULP above center

    auto r1 = old_pipeline(xmin, xmax, p1, VP_W);
    auto r2 = old_pipeline(xmin, xmax, p2, VP_W);

    float expected_ndc1 = -0.16f;
    float expected_ndc2 = 0.16f;

    // Old path has measurable NDC error due to float product rounding
    float err1 = std::abs(r1.ndc - expected_ndc1);
    float err2 = std::abs(r2.ndc - expected_ndc2);
    float max_err = std::max(err1, err2);
    // Error should be at least 0.02 NDC (~19 pixels) — catastrophic for precise plotting
    EXPECT_GT(max_err, 0.02f) << "Old path should have measurable error at deep zoom";
}

// New path vs old path accuracy comparison at moderate zoom.
// Center = 1e7, range = 20.  Points ±5 (5 ULPs at 1e7 where ULP=1).
// Old path has float product ≈ 1e6 (ULP=0.125), causing quantization.
// New path works on small values, giving near-exact results.
TEST_F(UltraZoomPrecision, DeepZoom_NewVsOld_AccuracyComparison)
{
    double center = 1.0e7;
    double range  = 20.0;
    double xmin   = center - range * 0.5;
    double xmax   = center + range * 0.5;
    double origin = center;

    float p = static_cast<float>(center + 3.0);   // 3 ULPs above center

    auto old_r = old_pipeline(xmin, xmax, p, VP_W);
    auto new_r = new_pipeline(xmin, xmax, p, origin, VP_W);

    // Expected NDC: 2 * 3 / 20 = 0.3
    float expected_ndc = 0.3f;

    float old_err = std::abs(old_r.ndc - expected_ndc);
    float new_err = std::abs(new_r.ndc - expected_ndc);

    // New path should be significantly more accurate
    EXPECT_LT(new_err, 0.01f) << "New path should be near-exact";
    // Old path has larger error from float product rounding
    EXPECT_GT(old_err, new_err) << "Old path should have more error than new path";
}

// Projection matrix translation term should be zero for centered projection.
TEST_F(UltraZoomPrecision, CenteredProjection_TranslationIsZero)
{
    double half_rx = 0.5e-6;
    float  m[16];
    build_ortho(-half_rx, half_rx, -0.5, 0.5, m);

    // m[12] should be exactly 0 (or extremely close)
    EXPECT_NEAR(m[12], 0.0f, 1e-10f) << "Centered projection should have zero x-translation";
    EXPECT_NEAR(m[13], 0.0f, 1e-10f) << "Centered projection should have zero y-translation";
}

// Origin drift: data_offset bridges the gap between upload origin and view center.
// Verify sub-pixel accuracy when origin is close to view center.
TEST_F(UltraZoomPrecision, OriginDrift_SmallOffset_PrecisionPreserved)
{
    double center = 1.0e6;
    double range  = 1.0e-3;
    double xmin   = center - range * 0.5;
    double xmax   = center + range * 0.5;

    // Origin was set when view was slightly different (simulates pan)
    double origin = center - range * 10.0;   // 10× range drift

    float data_x = static_cast<float>(center);

    auto r = new_pipeline(xmin, xmax, data_x, origin, VP_W);
    // Data at center should map to ~center of viewport (960px)
    EXPECT_NEAR(r.px, VP_W * 0.5f, 5.0f) << "Center point should be near viewport center";
}

// Extreme zoom: range = 1e-10 around x=1e8.
// Float at 1e8 has granularity ~8, so the data itself can't distinguish
// points — but the projection/offset math should not introduce NaN/Inf.
TEST_F(UltraZoomPrecision, ExtremeZoom_NoNanInf)
{
    double center = 1.0e8;
    double range  = 1.0e-10;
    double xmin   = center - range * 0.5;
    double xmax   = center + range * 0.5;
    double origin = center;

    float data_x = static_cast<float>(center);

    auto r = new_pipeline(xmin, xmax, data_x, origin, VP_W);
    EXPECT_TRUE(std::isfinite(r.ndc)) << "NDC should be finite at extreme zoom";
    EXPECT_TRUE(std::isfinite(r.px)) << "Pixel pos should be finite at extreme zoom";
}

// Multiple points across view range — verify monotonic pixel ordering.
TEST_F(UltraZoomPrecision, MultiplePoints_MonotonicPixelOrder)
{
    double center = 5000.0;
    double range  = 1.0e-4;
    double xmin   = center - range * 0.5;
    double xmax   = center + range * 0.5;
    double origin = center;

    constexpr int N = 10;
    float         prev_px = -1e30f;
    for (int i = 0; i < N; ++i)
    {
        double t      = static_cast<double>(i) / (N - 1);   // 0..1
        double data_d = xmin + t * range;
        float  data_x = static_cast<float>(data_d);

        auto r = new_pipeline(xmin, xmax, data_x, origin, VP_W);
        EXPECT_GE(r.px, prev_px) << "Pixel positions should be monotonically increasing (i=" << i
                                 << ")";
        prev_px = r.px;
    }
}
