#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

#include "math/expression_eval.hpp"

using namespace spectra;

// ─── Helper ─────────────────────────────────────────────────────────────────

static float eval(const std::string& expr, float x = 0.0f, float y = 0.0f, size_t i = 0, size_t n = 1)
{
    ExpressionParser parser;
    auto             ast = parser.parse(expr);
    if (!ast)
        return std::numeric_limits<float>::quiet_NaN();
    ExprContext ctx;
    ctx.x = x;
    ctx.y = y;
    ctx.t = x;
    ctx.i = i;
    ctx.n = n;
    return evaluate(*ast, ctx);
}

// ═══════════════════════════════════════════════════════════════════════════
// Basic arithmetic
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, Addition)
{
    EXPECT_FLOAT_EQ(eval("1 + 2"), 3.0f);
}

TEST(ExprEval, Subtraction)
{
    EXPECT_FLOAT_EQ(eval("5 - 3"), 2.0f);
}

TEST(ExprEval, Multiplication)
{
    EXPECT_FLOAT_EQ(eval("3 * 4"), 12.0f);
}

TEST(ExprEval, Division)
{
    EXPECT_FLOAT_EQ(eval("10 / 4"), 2.5f);
}

TEST(ExprEval, DivByZeroIsNaN)
{
    EXPECT_TRUE(std::isnan(eval("1 / 0")));
}

TEST(ExprEval, Power)
{
    EXPECT_FLOAT_EQ(eval("2 ^ 3"), 8.0f);
}

TEST(ExprEval, Modulo)
{
    EXPECT_FLOAT_EQ(eval("7 % 3"), 1.0f);
}

TEST(ExprEval, UnaryMinus)
{
    EXPECT_FLOAT_EQ(eval("-5"), -5.0f);
}

TEST(ExprEval, Precedence)
{
    EXPECT_FLOAT_EQ(eval("2 + 3 * 4"), 14.0f);
}

TEST(ExprEval, Parentheses)
{
    EXPECT_FLOAT_EQ(eval("(2 + 3) * 4"), 20.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Variables
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, VariableX)
{
    EXPECT_FLOAT_EQ(eval("x", 3.0f), 3.0f);
}

TEST(ExprEval, VariableY)
{
    EXPECT_FLOAT_EQ(eval("y", 0.0f, 7.0f), 7.0f);
}

TEST(ExprEval, VariableYTimesTwo)
{
    EXPECT_FLOAT_EQ(eval("y * 2", 0.0f, 5.0f), 10.0f);
}

TEST(ExprEval, VariableI)
{
    EXPECT_FLOAT_EQ(eval("i", 0.0f, 0.0f, 42), 42.0f);
}

TEST(ExprEval, VariableN)
{
    EXPECT_FLOAT_EQ(eval("n", 0.0f, 0.0f, 0, 100), 100.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Comparison operators
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, GreaterThanTrue)
{
    EXPECT_FLOAT_EQ(eval("y > 0", 0.0f, 5.0f), 1.0f);
}

TEST(ExprEval, GreaterThanFalse)
{
    EXPECT_FLOAT_EQ(eval("y > 0", 0.0f, -3.0f), 0.0f);
}

TEST(ExprEval, GreaterThanEqual)
{
    EXPECT_FLOAT_EQ(eval("y > 0", 0.0f, 0.0f), 0.0f);
}

TEST(ExprEval, LessThanTrue)
{
    EXPECT_FLOAT_EQ(eval("y < 10", 0.0f, 5.0f), 1.0f);
}

TEST(ExprEval, LessThanFalse)
{
    EXPECT_FLOAT_EQ(eval("y < 10", 0.0f, 15.0f), 0.0f);
}

TEST(ExprEval, GreaterEqualTrue)
{
    EXPECT_FLOAT_EQ(eval("y >= 5", 0.0f, 5.0f), 1.0f);
}

TEST(ExprEval, GreaterEqualFalse)
{
    EXPECT_FLOAT_EQ(eval("y >= 5", 0.0f, 4.0f), 0.0f);
}

TEST(ExprEval, LessEqualTrue)
{
    EXPECT_FLOAT_EQ(eval("y <= 5", 0.0f, 5.0f), 1.0f);
}

TEST(ExprEval, LessEqualFalse)
{
    EXPECT_FLOAT_EQ(eval("y <= 5", 0.0f, 6.0f), 0.0f);
}

TEST(ExprEval, EqualTrue)
{
    EXPECT_FLOAT_EQ(eval("y == 3", 0.0f, 3.0f), 1.0f);
}

TEST(ExprEval, EqualFalse)
{
    EXPECT_FLOAT_EQ(eval("y == 3", 0.0f, 4.0f), 0.0f);
}

TEST(ExprEval, NotEqualTrue)
{
    EXPECT_FLOAT_EQ(eval("y != 3", 0.0f, 4.0f), 1.0f);
}

TEST(ExprEval, NotEqualFalse)
{
    EXPECT_FLOAT_EQ(eval("y != 3", 0.0f, 3.0f), 0.0f);
}

TEST(ExprEval, ComparisonWithExpressions)
{
    // (2+3) > (1+3) => 5 > 4 => 1
    EXPECT_FLOAT_EQ(eval("(2+3) > (1+3)"), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Ternary operator
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, TernarySimple)
{
    EXPECT_FLOAT_EQ(eval("y ? y : 0", 0.0f, 5.0f), 5.0f);
    EXPECT_FLOAT_EQ(eval("y ? y : 0", 0.0f, 0.0f), 0.0f);
}

TEST(ExprEval, TernaryWithComparison)
{
    EXPECT_FLOAT_EQ(eval("y > 0 ? y : 0", 0.0f, 5.0f), 5.0f);
    EXPECT_FLOAT_EQ(eval("y > 0 ? y : 0", 0.0f, -3.0f), 0.0f);
}

TEST(ExprEval, TernaryWithComparisonBoundary)
{
    EXPECT_FLOAT_EQ(eval("y > 0 ? y : 0", 0.0f, 0.0f), 0.0f);
}

TEST(ExprEval, TernaryAbsValue)
{
    // Manual abs: y >= 0 ? y : -y
    EXPECT_FLOAT_EQ(eval("y >= 0 ? y : -y", 0.0f, 7.0f), 7.0f);
    EXPECT_FLOAT_EQ(eval("y >= 0 ? y : -y", 0.0f, -7.0f), 7.0f);
}

TEST(ExprEval, TernaryLessThan)
{
    EXPECT_FLOAT_EQ(eval("x < 5 ? 1 : 0", 3.0f), 1.0f);
    EXPECT_FLOAT_EQ(eval("x < 5 ? 1 : 0", 7.0f), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, Sin)
{
    EXPECT_NEAR(eval("sin(0)"), 0.0f, 1e-6f);
}

TEST(ExprEval, Abs)
{
    EXPECT_FLOAT_EQ(eval("abs(-5)"), 5.0f);
}

TEST(ExprEval, Sqrt)
{
    EXPECT_FLOAT_EQ(eval("sqrt(9)"), 3.0f);
}

TEST(ExprEval, Log10)
{
    EXPECT_NEAR(eval("log10(100)"), 2.0f, 1e-6f);
}

TEST(ExprEval, Clamp)
{
    EXPECT_FLOAT_EQ(eval("clamp(y, 0, 10)", 0.0f, 15.0f), 10.0f);
    EXPECT_FLOAT_EQ(eval("clamp(y, 0, 10)", 0.0f, -5.0f), 0.0f);
    EXPECT_FLOAT_EQ(eval("clamp(y, 0, 10)", 0.0f, 5.0f), 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Parse errors
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, EmptyExprFails)
{
    ExpressionParser p;
    EXPECT_EQ(p.parse(""), nullptr);
    EXPECT_FALSE(p.error().empty());
}

TEST(ExprEval, UnmatchedParenFails)
{
    ExpressionParser p;
    EXPECT_EQ(p.parse("(1 + 2"), nullptr);
}

TEST(ExprEval, TrailingGarbageFails)
{
    ExpressionParser p;
    EXPECT_EQ(p.parse("1 + 2 @"), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// parse_expression helper
// ═══════════════════════════════════════════════════════════════════════════

TEST(ExprEval, ParseExpressionValid)
{
    auto info = parse_expression("y > 0 ? y : -y");
    EXPECT_NE(info.ast, nullptr);
    EXPECT_TRUE(info.error.empty());
    EXPECT_FALSE(info.referenced_variables.empty());
}

TEST(ExprEval, ParseExpressionInvalid)
{
    auto info = parse_expression("");
    EXPECT_EQ(info.ast, nullptr);
    EXPECT_FALSE(info.error.empty());
}
