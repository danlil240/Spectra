// CPU helpers of the 2D line pipeline (render/line_geometry.hpp): dash phase
// precomputation and the polyline range packed into the push constants.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "render/line_geometry.hpp"

using namespace spectra;

namespace
{

std::vector<float> phases(const std::vector<float>& out)
{
    std::vector<float> p;
    for (size_t i = 2; i < out.size(); i += 3)
        p.push_back(out[i]);
    return p;
}

}   // namespace

TEST(LineDashPhase, StraightLineAccumulatesScreenLength)
{
    // 4 points one axis unit apart, 10 px per unit in x: 0, 10, 20, 30 px.
    const std::vector<float> xy = {0, 0, 1, 0, 2, 0, 3, 0};
    std::vector<float>       out;
    build_line_dash_phase(xy, 10.0, 5.0, 0.0, out);
    ASSERT_EQ(out.size(), 12u);
    EXPECT_FLOAT_EQ(out[3], 1.0f);   // positions copied through
    EXPECT_FLOAT_EQ(out[4], 0.0f);
    const auto p = phases(out);
    EXPECT_FLOAT_EQ(p[0], 0.0f);
    EXPECT_FLOAT_EQ(p[1], 10.0f);
    EXPECT_FLOAT_EQ(p[2], 20.0f);
    EXPECT_FLOAT_EQ(p[3], 30.0f);
}

TEST(LineDashPhase, UsesAnisotropicScale)
{
    // A 3-4-5 triangle in px: dx = 1 unit * 3 px, dy = 2 units * 2 px.
    const std::vector<float> xy = {0, 0, 1, 2};
    std::vector<float>       out;
    build_line_dash_phase(xy, 3.0, 2.0, 0.0, out);
    EXPECT_FLOAT_EQ(phases(out)[1], 5.0f);
}

TEST(LineDashPhase, WrapsModuloPeriod)
{
    const std::vector<float> xy = {0, 0, 1, 0, 2, 0, 3, 0};
    std::vector<float>       out;
    build_line_dash_phase(xy, 7.0, 1.0, 12.0, out);
    const auto p = phases(out);
    EXPECT_FLOAT_EQ(p[1], 7.0f);
    EXPECT_FLOAT_EQ(p[2], 2.0f);   // 14 mod 12
    EXPECT_FLOAT_EQ(p[3], 9.0f);   // 21 mod 12
}

TEST(LineDashPhase, NanStartsANewRunAtZero)
{
    const float              nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> xy  = {0, 0, 1, 0, nan, nan, 5, 0, 6, 0};
    std::vector<float>       out;
    build_line_dash_phase(xy, 4.0, 4.0, 0.0, out);
    const auto p = phases(out);
    EXPECT_FLOAT_EQ(p[1], 4.0f);
    EXPECT_FLOAT_EQ(p[2], 0.0f);
    EXPECT_FLOAT_EQ(p[3], 0.0f);   // the jump across the gap is not counted
    EXPECT_FLOAT_EQ(p[4], 4.0f);
    EXPECT_TRUE(std::isnan(out[6]));
}

TEST(LineDashPhase, LongLineKeepsExactPhase)
{
    // 200k unit steps of 0.1 px with a 24 px period: summing in float would
    // drift by whole pixels; the double accumulation stays exact to ~1e-3.
    const size_t       n = 200000;
    std::vector<float> xy(n * 2, 0.0f);
    for (size_t i = 0; i < n; ++i)
        xy[2 * i] = static_cast<float>(i);
    std::vector<float> out;
    build_line_dash_phase(xy, 0.1, 1.0, 24.0, out);
    const double expected = std::fmod(static_cast<double>(n - 1) * 0.1, 24.0);
    EXPECT_NEAR(out.back(), expected, 1e-3);
}

TEST(LineDashPhase, EmptyAndSinglePoint)
{
    std::vector<float> out = {1, 2, 3};
    build_line_dash_phase({}, 1.0, 1.0, 1.0, out);
    EXPECT_TRUE(out.empty());
    const std::vector<float> one = {2, 3};
    build_line_dash_phase(one, 1.0, 1.0, 1.0, out);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_FLOAT_EQ(out[2], 0.0f);
}

TEST(LinePolylineRange, PacksIndicesBitExactly)
{
    SeriesPushConstants pc{};
    set_line_polyline(pc, 7, 3000000, true);
    uint32_t v[2];
    std::memcpy(v, pc._pad2, sizeof(v));
    EXPECT_EQ(v[0], 7u);
    EXPECT_EQ(v[1] & ~LINE_PHASE_FLAG, 3000000u);
    EXPECT_NE(v[1] & LINE_PHASE_FLAG, 0u);

    set_line_polyline(pc, 7, 9);
    std::memcpy(v, pc._pad2, sizeof(v));
    EXPECT_EQ(v[1], 9u);

    set_line_isolated(pc);
    std::memcpy(v, pc._pad2, sizeof(v));
    EXPECT_EQ(v[0], 0u);
    EXPECT_EQ(v[1], 0u);
}
