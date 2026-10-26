// Regression tests for bug fixes in src/math (data_transform, expression_eval).

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

#include "math/data_transform.hpp"
#include "math/expression_eval.hpp"

using namespace spectra;

namespace
{

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

std::vector<float> run_y(TransformType             type,
                         const std::vector<float>& y,
                         const TransformParams&    params = {})
{
    std::vector<float> x(y.size());
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(i);
    DataTransform      t(type, params);
    std::vector<float> x_out;
    std::vector<float> y_out;
    t.apply_y(x, y, x_out, y_out);
    return y_out;
}

float eval(const std::string& expr, float x = 0.0f, float y = 0.0f)
{
    ExpressionParser parser;
    auto             ast = parser.parse(expr);
    if (!ast)
        return kNaN;
    ExprContext ctx;
    ctx.x = x;
    ctx.y = y;
    ctx.t = x;
    ctx.n = 1;
    return evaluate(*ast, ctx);
}

bool parses(const std::string& expr)
{
    ExpressionParser parser;
    return parser.parse(expr) != nullptr;
}

}   // namespace

// ─── Bug #43: Normalize with NaN / Inf ──────────────────────────────────────

TEST(BugfixMath, NormalizeLeadingNaNKeepsFiniteValues)
{
    auto out = run_y(TransformType::Normalize, {kNaN, 1.0f, 2.0f, 3.0f});
    ASSERT_EQ(out.size(), 4u);
    EXPECT_TRUE(std::isnan(out[0]));
    EXPECT_FLOAT_EQ(out[1], 0.0f);
    EXPECT_FLOAT_EQ(out[2], 0.5f);
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(BugfixMath, NormalizeIgnoresInfinities)
{
    auto out = run_y(TransformType::Normalize, {-kInf, 0.0f, 10.0f, kInf});
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0], -kInf);
    EXPECT_FLOAT_EQ(out[1], 0.0f);
    EXPECT_FLOAT_EQ(out[2], 1.0f);
    EXPECT_EQ(out[3], kInf);
}

TEST(BugfixMath, NormalizeHugeRangeDoesNotOverflow)
{
    auto out = run_y(TransformType::Normalize, {-3e38f, 0.0f, 3e38f});
    ASSERT_EQ(out.size(), 3u);
    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_FLOAT_EQ(out[1], 0.5f);
    EXPECT_FLOAT_EQ(out[2], 1.0f);
}

TEST(BugfixMath, NormalizeConstantStillMapsToHalf)
{
    auto out = run_y(TransformType::Normalize, {2.0f, kNaN, 2.0f});
    ASSERT_EQ(out.size(), 3u);
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_TRUE(std::isnan(out[1]));
    EXPECT_FLOAT_EQ(out[2], 0.5f);
}

TEST(BugfixMath, NormalizeAllNaNPassesThrough)
{
    auto out = run_y(TransformType::Normalize, {kNaN, kNaN});
    ASSERT_EQ(out.size(), 2u);
    EXPECT_TRUE(std::isnan(out[0]));
    EXPECT_TRUE(std::isnan(out[1]));
}

// ─── Bug #44: Standardize with NaN / Inf ────────────────────────────────────

TEST(BugfixMath, StandardizeSkipsNaN)
{
    auto out = run_y(TransformType::Standardize, {1.0f, 2.0f, kNaN, 3.0f});
    ASSERT_EQ(out.size(), 4u);
    // finite samples {1,2,3}: mean 2, population stddev sqrt(2/3)
    const float sd = std::sqrt(2.0f / 3.0f);
    EXPECT_NEAR(out[0], -1.0f / sd, 1e-5f);
    EXPECT_NEAR(out[1], 0.0f, 1e-6f);
    EXPECT_TRUE(std::isnan(out[2]));
    EXPECT_NEAR(out[3], 1.0f / sd, 1e-5f);
}

TEST(BugfixMath, StandardizeSkipsInf)
{
    auto out = run_y(TransformType::Standardize, {kInf, 1.0f, 3.0f});
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], kInf);
    EXPECT_NEAR(out[1], -1.0f, 1e-6f);
    EXPECT_NEAR(out[2], 1.0f, 1e-6f);
}

// ─── Bug #45: FFT amplitude normalization ───────────────────────────────────

namespace
{

float fft_peak_for_length(size_t n)
{
    const float        fs = 1000.0f;
    const float        f  = 125.0f;   // exact bin for both 1024- and 2048-point FFTs
    std::vector<float> y(n);
    for (size_t i = 0; i < n; ++i)
        y[i] = std::sin(2.0f * 3.14159265358979f * f * static_cast<float>(i) / fs);
    TransformParams p;
    p.fft_sample_rate = fs;
    auto  out         = run_y(TransformType::FFT, y, p);
    float peak        = 0.0f;
    for (float v : out)
        peak = std::max(peak, v);
    return peak;
}

}   // namespace

TEST(BugfixMath, FftAmplitudeIndependentOfPadding)
{
    const float p1024 = fft_peak_for_length(1024);
    const float p1025 = fft_peak_for_length(1025);
    EXPECT_NEAR(p1024, 1.0f, 0.02f);
    EXPECT_NEAR(p1025, 1.0f, 0.05f);
}

// ─── Bug #46: unary minus vs '^' ────────────────────────────────────────────

TEST(BugfixMath, UnaryMinusBindsLooserThanPower)
{
    EXPECT_FLOAT_EQ(eval("-x^2", 3.0f), -9.0f);
    EXPECT_FLOAT_EQ(eval("-2^2"), -4.0f);
    EXPECT_FLOAT_EQ(eval("2^-1"), 0.5f);
    EXPECT_FLOAT_EQ(eval("2^3^2"), 512.0f);
    EXPECT_FLOAT_EQ(eval("(-2)^2"), 4.0f);
    EXPECT_FLOAT_EQ(eval("2*-3"), -6.0f);
    EXPECT_FLOAT_EQ(eval("--3"), 3.0f);
    EXPECT_FLOAT_EQ(eval("1 - x^2", 2.0f), -3.0f);
}

// ─── Bug #47: huge series index ─────────────────────────────────────────────

TEST(BugfixMath, HugeSeriesIndexReturnsNaNWithoutThrowing)
{
    ExpressionParser parser;
    auto             ast = parser.parse("s99999999999999999999999_x");
    ASSERT_NE(ast, nullptr);
    ExprContext ctx;
    float       v = 0.0f;
    EXPECT_NO_THROW(v = evaluate(*ast, ctx));
    EXPECT_TRUE(std::isnan(v));
}

TEST(BugfixMath, SeriesIndexStillResolves)
{
    std::vector<float> sx = {10.0f, 20.0f};
    std::vector<float> sy = {1.0f, 2.0f};
    ExpressionParser   parser;
    auto               ast = parser.parse("s0_y + s0_x");
    ASSERT_NE(ast, nullptr);
    ExprContext ctx;
    ctx.i = 1;
    ctx.series_data.push_back({sx, sy, "s"});
    EXPECT_FLOAT_EQ(evaluate(*ast, ctx), 22.0f);
}

// ─── Bug #91: chained ternary ───────────────────────────────────────────────

TEST(BugfixMath, ChainedTernaryIsRightAssociative)
{
    const char* expr = "x > 1 ? 2 : x > 0 ? 3 : 4";
    EXPECT_FLOAT_EQ(eval(expr, 5.0f), 2.0f);
    EXPECT_FLOAT_EQ(eval(expr, 0.5f), 3.0f);
    EXPECT_FLOAT_EQ(eval(expr, -1.0f), 4.0f);
    EXPECT_FLOAT_EQ(eval("1 ? 0 ? 5 : 6 : 7"), 6.0f);
}

// ─── Bug #92: number literals ───────────────────────────────────────────────

TEST(BugfixMath, OutOfRangeLiteralIsInfinity)
{
    EXPECT_EQ(eval("1e39"), kInf);
    EXPECT_EQ(eval("-1e39"), -kInf);
    EXPECT_FLOAT_EQ(eval("1e-50"), 0.0f);
    EXPECT_FLOAT_EQ(eval("1.5e2"), 150.0f);
    EXPECT_FLOAT_EQ(eval(".5"), 0.5f);
}

TEST(BugfixMath, LoneDotIsParseError)
{
    EXPECT_FALSE(parses("."));
    EXPECT_FALSE(parses("x + ."));
}

// ─── Bug #93: clamp with reversed bounds ────────────────────────────────────

TEST(BugfixMath, ClampReversedBoundsIsSafe)
{
    EXPECT_FLOAT_EQ(eval("clamp(x, 10, 0)", 5.0f), 5.0f);
    EXPECT_FLOAT_EQ(eval("clamp(x, 10, 0)", -5.0f), 0.0f);
    EXPECT_FLOAT_EQ(eval("clamp(x, 10, 0)", 50.0f), 10.0f);
    EXPECT_FLOAT_EQ(eval("clamp(x, 0, 10)", 50.0f), 10.0f);
}

// ─── Bug #94: function arity / unknown functions ────────────────────────────

TEST(BugfixMath, FunctionArityIsChecked)
{
    EXPECT_FALSE(parses("min(x)"));
    EXPECT_FALSE(parses("pow(x)"));
    EXPECT_FALSE(parses("max(1, 2, 3)"));
    EXPECT_FALSE(parses("sin(1, 2)"));
    EXPECT_FALSE(parses("clamp(x, 1)"));
    EXPECT_FALSE(parses("nosuchfunc(x)"));

    EXPECT_TRUE(parses("min(x, 1)"));
    EXPECT_TRUE(parses("atan(x)"));
    EXPECT_TRUE(parses("atan(y, x)"));
    EXPECT_TRUE(parses("clamp(x, 0, 1)"));

    ExpressionParser parser;
    EXPECT_EQ(parser.parse("min(x)"), nullptr);
    EXPECT_FALSE(parser.error().empty());
}
