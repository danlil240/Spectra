// Unit tests for ExpressionEngine (C5 — computed topic fields).
//
// No ROS2 runtime required — pure expression parser/evaluator logic.
// Registered in tests/CMakeLists.txt under SPECTRA_USE_ROS2 with gtest_main.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "expression_engine.hpp"

using spectra::adapters::ros2::CompileResult;
using spectra::adapters::ros2::ExpressionEngine;
using spectra::adapters::ros2::ExpressionPreset;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isnan_d(double v)
{
    return std::isnan(v);
}

static double eval(const std::string&                             expr,
                   const std::unordered_map<std::string, double>& vars = {})
{
    ExpressionEngine eng;
    auto             r = eng.compile(expr);
    if (!r.ok)
        return std::numeric_limits<double>::quiet_NaN();
    return eng.evaluate(vars);
}

// ---------------------------------------------------------------------------
// Suite: Compilation
// ---------------------------------------------------------------------------

TEST(ExpressionEngineCompile, EmptyExpressionFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(ExpressionEngineCompile, SimpleNumberSucceeds)
{
    ExpressionEngine eng;
    auto             r = eng.compile("42");
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(eng.is_compiled());
}

TEST(ExpressionEngineCompile, TrailingGarbageFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("1 + 2 ???");
    EXPECT_FALSE(r.ok);
}

TEST(ExpressionEngineCompile, UnknownIdentifierFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("foo");
    EXPECT_FALSE(r.ok);
}

TEST(ExpressionEngineCompile, MissingParenFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("(1 + 2");
    EXPECT_FALSE(r.ok);
}

TEST(ExpressionEngineCompile, UnknownFunctionFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("foobar(1)");
    EXPECT_FALSE(r.ok);
}

TEST(ExpressionEngineCompile, EmptyVariableNameFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("$");
    EXPECT_FALSE(r.ok);
}

TEST(ExpressionEngineCompile, RecompileReplacesOld)
{
    ExpressionEngine eng;
    auto             r1 = eng.compile("1 + 1");
    EXPECT_TRUE(r1.ok);

    auto r2 = eng.compile("2 + 2");
    EXPECT_TRUE(r2.ok);
    EXPECT_NEAR(eng.evaluate(), 4.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Suite: Arithmetic
// ---------------------------------------------------------------------------

TEST(ExpressionEngineArithmetic, Addition)
{
    EXPECT_NEAR(eval("1 + 2"), 3.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, Subtraction)
{
    EXPECT_NEAR(eval("5 - 3"), 2.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, Multiplication)
{
    EXPECT_NEAR(eval("3 * 4"), 12.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, Division)
{
    EXPECT_NEAR(eval("10 / 4"), 2.5, 1e-12);
}

TEST(ExpressionEngineArithmetic, DivisionByZeroIsNaN)
{
    EXPECT_TRUE(isnan_d(eval("1 / 0")));
}

TEST(ExpressionEngineArithmetic, Power)
{
    EXPECT_NEAR(eval("2 ^ 10"), 1024.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, PowerRightAssociative)
{
    // 2^3^2 should be 2^(3^2) = 2^9 = 512
    EXPECT_NEAR(eval("2^3^2"), 512.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, UnaryMinus)
{
    EXPECT_NEAR(eval("-5"), -5.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, UnaryMinusInExpr)
{
    EXPECT_NEAR(eval("3 + -2"), 1.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, NestedParens)
{
    EXPECT_NEAR(eval("(1 + 2) * (3 + 4)"), 21.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, OperatorPrecedence)
{
    // 2 + 3 * 4 = 14, not 20
    EXPECT_NEAR(eval("2 + 3 * 4"), 14.0, 1e-12);
}

TEST(ExpressionEngineArithmetic, FloatLiteral)
{
    EXPECT_NEAR(eval("3.14"), 3.14, 1e-9);
}

TEST(ExpressionEngineArithmetic, ScientificNotation)
{
    EXPECT_NEAR(eval("1e3"), 1000.0, 1e-9);
    EXPECT_NEAR(eval("1.5e-2"), 0.015, 1e-12);
}

// ---------------------------------------------------------------------------
// Suite: Functions
// ---------------------------------------------------------------------------

TEST(ExpressionEngineFunctions, Sqrt)
{
    EXPECT_NEAR(eval("sqrt(4)"), 2.0, 1e-12);
    EXPECT_NEAR(eval("sqrt(2)"), std::sqrt(2.0), 1e-12);
}

TEST(ExpressionEngineFunctions, Abs)
{
    EXPECT_NEAR(eval("abs(-7.5)"), 7.5, 1e-12);
    EXPECT_NEAR(eval("abs(3)"), 3.0, 1e-12);
}

TEST(ExpressionEngineFunctions, Sin)
{
    EXPECT_NEAR(eval("sin(0)"), 0.0, 1e-12);
    EXPECT_NEAR(eval("sin(pi/2)"), 1.0, 1e-9);
}

TEST(ExpressionEngineFunctions, Cos)
{
    EXPECT_NEAR(eval("cos(0)"), 1.0, 1e-12);
    EXPECT_NEAR(eval("cos(pi)"), -1.0, 1e-9);
}

TEST(ExpressionEngineFunctions, Tan)
{
    EXPECT_NEAR(eval("tan(0)"), 0.0, 1e-12);
}

TEST(ExpressionEngineFunctions, Atan)
{
    EXPECT_NEAR(eval("atan(1)"), M_PI / 4.0, 1e-9);
}

TEST(ExpressionEngineFunctions, Atan2)
{
    EXPECT_NEAR(eval("atan2(1, 1)"), M_PI / 4.0, 1e-9);
    EXPECT_NEAR(eval("atan2(0, -1)"), M_PI, 1e-9);
}

TEST(ExpressionEngineFunctions, Log)
{
    EXPECT_NEAR(eval("log(e)"), 1.0, 1e-9);
}

TEST(ExpressionEngineFunctions, Log10)
{
    EXPECT_NEAR(eval("log10(100)"), 2.0, 1e-12);
}

TEST(ExpressionEngineFunctions, Exp)
{
    EXPECT_NEAR(eval("exp(0)"), 1.0, 1e-12);
    EXPECT_NEAR(eval("exp(1)"), M_E, 1e-9);
}

TEST(ExpressionEngineFunctions, Floor)
{
    EXPECT_NEAR(eval("floor(3.7)"), 3.0, 1e-12);
    EXPECT_NEAR(eval("floor(-1.2)"), -2.0, 1e-12);
}

TEST(ExpressionEngineFunctions, Ceil)
{
    EXPECT_NEAR(eval("ceil(3.2)"), 4.0, 1e-12);
    EXPECT_NEAR(eval("ceil(-1.9)"), -1.0, 1e-12);
}

TEST(ExpressionEngineFunctions, Round)
{
    EXPECT_NEAR(eval("round(3.5)"), 4.0, 1e-12);
    EXPECT_NEAR(eval("round(3.4)"), 3.0, 1e-12);
}

TEST(ExpressionEngineFunctions, Asin)
{
    EXPECT_NEAR(eval("asin(1)"), M_PI / 2.0, 1e-9);
}

TEST(ExpressionEngineFunctions, Acos)
{
    EXPECT_NEAR(eval("acos(1)"), 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Suite: Constants
// ---------------------------------------------------------------------------

TEST(ExpressionEngineConstants, Pi)
{
    EXPECT_NEAR(eval("pi"), M_PI, 1e-12);
}

TEST(ExpressionEngineConstants, E)
{
    EXPECT_NEAR(eval("e"), M_E, 1e-12);
}

TEST(ExpressionEngineConstants, PiInExpr)
{
    EXPECT_NEAR(eval("2 * pi"), 2.0 * M_PI, 1e-12);
}

// ---------------------------------------------------------------------------
// Suite: Variables
// ---------------------------------------------------------------------------

TEST(ExpressionEngineVariables, SimpleVariable)
{
    ExpressionEngine eng;
    eng.compile("$x");
    eng.set_variable("$x", 7.5);
    EXPECT_NEAR(eng.evaluate(), 7.5, 1e-12);
}

TEST(ExpressionEngineVariables, VariableExtracted)
{
    ExpressionEngine eng;
    eng.compile("$a.b + $c.d");
    const auto& vars = eng.variables();
    ASSERT_EQ(vars.size(), 2u);
    EXPECT_TRUE(std::find(vars.begin(), vars.end(), "$a.b") != vars.end());
    EXPECT_TRUE(std::find(vars.begin(), vars.end(), "$c.d") != vars.end());
}

TEST(ExpressionEngineVariables, DeduplicatedVariables)
{
    ExpressionEngine eng;
    eng.compile("$x + $x * $x");
    EXPECT_EQ(eng.variables().size(), 1u);
    EXPECT_EQ(eng.variables()[0], "$x");
}

TEST(ExpressionEngineVariables, UnsetVariableIsNaN)
{
    ExpressionEngine eng;
    eng.compile("$x + 1");
    EXPECT_TRUE(isnan_d(eng.evaluate()));
}

TEST(ExpressionEngineVariables, NamespacedVariable)
{
    ExpressionEngine eng;
    eng.compile("$imu.linear_acceleration.x");
    eng.set_variable("$imu.linear_acceleration.x", 9.81);
    EXPECT_NEAR(eng.evaluate(), 9.81, 1e-12);
}

TEST(ExpressionEngineVariables, SetVariables_Map)
{
    ExpressionEngine eng;
    eng.compile("$a + $b");
    eng.set_variables({{"$a", 3.0}, {"$b", 4.0}});
    EXPECT_NEAR(eng.evaluate(), 7.0, 1e-12);
}

TEST(ExpressionEngineVariables, EvaluateWithVarMap)
{
    ExpressionEngine eng;
    eng.compile("$a * $b");
    EXPECT_NEAR(eng.evaluate({{"$a", 6.0}, {"$b", 7.0}}), 42.0, 1e-12);
    // Original state unchanged.
    EXPECT_TRUE(isnan_d(eng.evaluate()));
}

TEST(ExpressionEngineVariables, ResetVariables)
{
    ExpressionEngine eng;
    eng.compile("$x");
    eng.set_variable("$x", 99.0);
    eng.reset_variables();
    EXPECT_NEAR(eng.evaluate(), 0.0, 1e-12);
}

TEST(ExpressionEngineVariables, GetVariable)
{
    ExpressionEngine eng;
    eng.compile("$v");
    eng.set_variable("$v", 1.23);
    EXPECT_NEAR(eng.get_variable("$v"), 1.23, 1e-12);
}

TEST(ExpressionEngineVariables, GetUnknownVariableIsNaN)
{
    ExpressionEngine eng;
    eng.compile("1");
    EXPECT_TRUE(isnan_d(eng.get_variable("$missing")));
}

TEST(ExpressionEngineVariables, VariableWithSlash)
{
    ExpressionEngine eng;
    auto             r = eng.compile("$/ns/imu.acc.x");
    EXPECT_TRUE(r.ok) << r.error;
    const auto& vars = eng.variables();
    ASSERT_EQ(vars.size(), 1u);
    EXPECT_EQ(vars[0], "$/ns/imu.acc.x");
}

// ---------------------------------------------------------------------------
// Suite: Complex expressions (IMU norm pattern)
// ---------------------------------------------------------------------------

TEST(ExpressionEngineComplex, ImuNorm)
{
    ExpressionEngine eng;
    auto             r = eng.compile("sqrt($imu.acc.x^2 + $imu.acc.y^2 + $imu.acc.z^2)");
    EXPECT_TRUE(r.ok) << r.error;

    eng.set_variable("$imu.acc.x", 0.0);
    eng.set_variable("$imu.acc.y", 0.0);
    eng.set_variable("$imu.acc.z", 9.81);
    EXPECT_NEAR(eng.evaluate(), 9.81, 1e-6);
}

TEST(ExpressionEngineComplex, ImuNormGeneral)
{
    ExpressionEngine eng;
    eng.compile("sqrt($x^2 + $y^2 + $z^2)");
    eng.set_variable("$x", 3.0);
    eng.set_variable("$y", 4.0);
    eng.set_variable("$z", 0.0);
    EXPECT_NEAR(eng.evaluate(), 5.0, 1e-9);
}

TEST(ExpressionEngineComplex, Heading)
{
    ExpressionEngine eng;
    eng.compile("atan2($vy, $vx) * 180 / pi");
    eng.set_variable("$vx", 1.0);
    eng.set_variable("$vy", 1.0);
    EXPECT_NEAR(eng.evaluate(), 45.0, 1e-6);
}

TEST(ExpressionEngineComplex, NestedFunctions)
{
    EXPECT_NEAR(eval("abs(sin(pi/2) - 1.0)"), 0.0, 1e-9);
}

TEST(ExpressionEngineComplex, ChainedOperations)
{
    EXPECT_NEAR(eval("(3 + 4) * 2 / 7"), 2.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Suite: Static utilities
// ---------------------------------------------------------------------------

TEST(ExpressionEngineStatic, ExtractVariables)
{
    auto vars = ExpressionEngine::extract_variables("sqrt($imu.acc.x^2 + $imu.acc.y^2)");
    EXPECT_EQ(vars.size(), 2u);
    EXPECT_TRUE(std::find(vars.begin(), vars.end(), "$imu.acc.x") != vars.end());
    EXPECT_TRUE(std::find(vars.begin(), vars.end(), "$imu.acc.y") != vars.end());
}

TEST(ExpressionEngineStatic, ExtractVariables_Empty)
{
    auto vars = ExpressionEngine::extract_variables("1 + 2");
    EXPECT_TRUE(vars.empty());
}

TEST(ExpressionEngineStatic, IsValidVariable_Valid)
{
    EXPECT_TRUE(ExpressionEngine::is_valid_variable("$x"));
    EXPECT_TRUE(ExpressionEngine::is_valid_variable("$imu.acc.x"));
    EXPECT_TRUE(ExpressionEngine::is_valid_variable("$/ns/imu.acc.x"));
}

TEST(ExpressionEngineStatic, IsValidVariable_Invalid)
{
    EXPECT_FALSE(ExpressionEngine::is_valid_variable(""));
    EXPECT_FALSE(ExpressionEngine::is_valid_variable("x"));
    EXPECT_FALSE(ExpressionEngine::is_valid_variable("$"));
    EXPECT_FALSE(ExpressionEngine::is_valid_variable("$!invalid"));
}

TEST(ExpressionEngineStatic, SyntaxHelp_NotEmpty)
{
    const char* h = ExpressionEngine::syntax_help();
    EXPECT_NE(h, nullptr);
    EXPECT_GT(std::strlen(h), 10u);
}

// ---------------------------------------------------------------------------
// Suite: Presets
// ---------------------------------------------------------------------------

TEST(ExpressionEnginePresets, SaveAndLoadPreset)
{
    ExpressionEngine eng;
    eng.compile("$x + 1");
    eng.save_preset("add_one");

    ExpressionEngine eng2;
    eng2.deserialize_presets(eng.serialize_presets());
    bool ok = eng2.load_preset("add_one");
    EXPECT_TRUE(ok);
    EXPECT_EQ(eng2.expression(), "$x + 1");
}

TEST(ExpressionEnginePresets, LoadNonExistentFails)
{
    ExpressionEngine eng;
    EXPECT_FALSE(eng.load_preset("does_not_exist"));
}

TEST(ExpressionEnginePresets, OverwriteExistingPreset)
{
    ExpressionEngine eng;
    eng.compile("$x");
    eng.save_preset("my_preset");
    EXPECT_EQ(eng.presets().size(), 1u);

    eng.compile("$y");
    eng.save_preset("my_preset");
    EXPECT_EQ(eng.presets().size(), 1u);
    EXPECT_EQ(eng.presets()[0].expression, "$y");
}

TEST(ExpressionEnginePresets, RemovePreset)
{
    ExpressionEngine eng;
    eng.compile("$x");
    eng.save_preset("p1");
    eng.save_preset("p2");
    EXPECT_EQ(eng.presets().size(), 2u);

    EXPECT_TRUE(eng.remove_preset("p1"));
    EXPECT_EQ(eng.presets().size(), 1u);
    EXPECT_EQ(eng.presets()[0].name, "p2");
}

TEST(ExpressionEnginePresets, RemoveNonExistentReturnsFalse)
{
    ExpressionEngine eng;
    EXPECT_FALSE(eng.remove_preset("ghost"));
}

TEST(ExpressionEnginePresets, SerializeDeserializeRoundtrip)
{
    ExpressionEngine eng;
    eng.compile("sqrt($a^2 + $b^2)");
    eng.save_preset("norm2d");
    eng.compile("atan2($y, $x)");
    eng.save_preset("heading");

    std::string json = eng.serialize_presets();

    ExpressionEngine eng2;
    eng2.deserialize_presets(json);
    ASSERT_EQ(eng2.presets().size(), 2u);
    EXPECT_EQ(eng2.presets()[0].name, "norm2d");
    EXPECT_EQ(eng2.presets()[0].expression, "sqrt($a^2 + $b^2)");
    EXPECT_EQ(eng2.presets()[1].name, "heading");
    EXPECT_EQ(eng2.presets()[1].expression, "atan2($y, $x)");
}

TEST(ExpressionEnginePresets, PresetsContainVariables)
{
    ExpressionEngine eng;
    eng.compile("$ax + $ay");
    eng.save_preset("sum");

    ASSERT_EQ(eng.presets().size(), 1u);
    auto        presets_copy = eng.presets();
    const auto& p            = presets_copy[0];
    EXPECT_EQ(p.variables.size(), 2u);
    EXPECT_TRUE(std::find(p.variables.begin(), p.variables.end(), "$ax") != p.variables.end());
    EXPECT_TRUE(std::find(p.variables.begin(), p.variables.end(), "$ay") != p.variables.end());
}

TEST(ExpressionEnginePresets, PresetSerializeDeserializeWithEscape)
{
    ExpressionPreset p;
    p.name       = "test\"preset";
    p.expression = "1 + 2";
    p.variables  = {};

    std::string      json = p.serialize();
    ExpressionPreset p2   = ExpressionPreset::deserialize(json);
    EXPECT_EQ(p2.name, "test\"preset");
    EXPECT_EQ(p2.expression, "1 + 2");
}

// ---------------------------------------------------------------------------
// Suite: Error reporting
// ---------------------------------------------------------------------------

TEST(ExpressionEngineErrors, ErrorColReported)
{
    ExpressionEngine eng;
    auto             r = eng.compile("1 + $");
    EXPECT_FALSE(r.ok);
    // Column >= 4 (at or after the '$').
    EXPECT_GE(r.error_col, 4);
}

TEST(ExpressionEngineErrors, ErrorMessageNotEmpty)
{
    ExpressionEngine eng;
    auto             r = eng.compile("(1 + 2");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(ExpressionEngineErrors, Atan2MissingSecondArgFails)
{
    ExpressionEngine eng;
    auto             r = eng.compile("atan2(1)");
    EXPECT_FALSE(r.ok);
}

// ---------------------------------------------------------------------------
// Suite: Edge cases
// ---------------------------------------------------------------------------

TEST(ExpressionEngineEdge, WhitespaceOnly)
{
    ExpressionEngine eng;
    auto             r = eng.compile("   ");
    EXPECT_FALSE(r.ok);
}

TEST(ExpressionEngineEdge, LargeNumber)
{
    EXPECT_NEAR(eval("1e300"), 1e300, 1e288);
}

TEST(ExpressionEngineEdge, NegativePower)
{
    EXPECT_NEAR(eval("2^-1"), 0.5, 1e-12);
}

TEST(ExpressionEngineEdge, MultipleUnaryMinus)
{
    EXPECT_NEAR(eval("--5"), 5.0, 1e-12);
}

TEST(ExpressionEngineEdge, ZeroVariable)
{
    ExpressionEngine eng;
    eng.compile("$x");
    eng.reset_variables();
    EXPECT_NEAR(eng.evaluate(), 0.0, 1e-12);
}

TEST(ExpressionEngineEdge, EvaluateBeforeCompileIsNaN)
{
    ExpressionEngine eng;
    EXPECT_TRUE(isnan_d(eng.evaluate()));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
// Uses GTest::gtest_main — no custom main needed.
