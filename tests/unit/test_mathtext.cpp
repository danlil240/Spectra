#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "core/label_layout.hpp"
#include "core/mathtext.hpp"
#include "io/text_metrics.hpp"

using namespace spectra;
using MRun = mathtext::Run;

namespace
{

// Text runs only (markers dropped).
std::vector<MRun> text_runs(std::string_view s)
{
    std::vector<MRun> out;
    for (MRun& r : mathtext::parse(s))
        if (r.kind == MRun::Kind::Text)
            out.push_back(std::move(r));
    return out;
}

// One advance unit per byte, scaled: easy to reason about in layout tests.
float byte_advance(std::string_view text, float scale)
{
    return static_cast<float>(text.size()) * 10.0f * scale;
}

}   // namespace

// ─── Detection ──────────────────────────────────────────────────────────────

TEST(MathText, DetectsMathSegments)
{
    EXPECT_FALSE(mathtext::has_markup("plain"));
    EXPECT_TRUE(mathtext::has_markup("cost ($)"));
    EXPECT_FALSE(mathtext::is_math_text("plain"));
    EXPECT_FALSE(mathtext::is_math_text("cost ($)"));        // one dollar: plain
    EXPECT_FALSE(mathtext::is_math_text("$a$ and $b"));      // odd: plain
    EXPECT_FALSE(mathtext::is_math_text("\\$5 and \\$6"));   // escaped only
    EXPECT_TRUE(mathtext::is_math_text("$a$"));
    EXPECT_TRUE(mathtext::is_math_text("$a$ \\$ $b$"));   // escaped one does not count
}

// ─── Plain text ─────────────────────────────────────────────────────────────

TEST(MathText, PlainTextIsOneRun)
{
    auto runs = mathtext::parse("Velocity (m/s)");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Velocity (m/s)");
    EXPECT_FLOAT_EQ(runs[0].scale, 1.0f);
    EXPECT_FLOAT_EQ(runs[0].shift, 0.0f);
    EXPECT_TRUE(mathtext::parse("").empty());
}

TEST(MathText, UnbalancedDollarRendersPlain)
{
    // Odd number of unescaped '$': the whole string is plain text, backslashes and
    // scripts kept as typed.
    EXPECT_EQ(mathtext::to_plain_text("price $5 x_1"), "price $5 x_1");
    EXPECT_EQ(mathtext::to_plain_text("$\\alpha"), "$\\alpha");
    ASSERT_EQ(text_runs("$\\alpha").size(), 1u);
}

TEST(MathText, EscapedDollarIsLiteral)
{
    EXPECT_EQ(mathtext::to_plain_text("cost \\$5"), "cost $5");
    EXPECT_EQ(mathtext::to_plain_text("\\$ $\\alpha$ \\$"), "$ α $");
    // Inside math too.
    EXPECT_EQ(mathtext::to_plain_text("$x\\$y$"), "x$y");
}

// ─── Symbols ────────────────────────────────────────────────────────────────

TEST(MathText, GreekAndSymbols)
{
    EXPECT_EQ(mathtext::to_plain_text("$\\alpha\\beta\\omega$"), "αβω");
    EXPECT_EQ(mathtext::to_plain_text("$\\Gamma\\Delta\\Omega\\Alpha$"), "ΓΔΩΑ");
    EXPECT_EQ(mathtext::to_plain_text("$\\mu$s"), "μs");
    EXPECT_EQ(mathtext::to_plain_text("$\\pm\\times\\cdot\\degree\\infty$"), "±×·°∞");
    EXPECT_EQ(mathtext::to_plain_text("$\\partial\\nabla\\sum\\int$"), "∂∇∑∫");
    EXPECT_EQ(mathtext::to_plain_text("$\\approx\\neq\\leq\\geq\\rightarrow$"), "≈≠≤≥→");
}

TEST(MathText, FunctionNamesAreUpright)
{
    EXPECT_EQ(mathtext::to_plain_text("$\\sin(\\omega t) + \\log_{10} x$"),
              "sin(\u03C9 t) + log10 x");
}

TEST(MathText, HyphenBecomesMinusInMathOnly)
{
    EXPECT_EQ(mathtext::to_plain_text("a-b $a-b$"), "a-b a−b");
}

TEST(MathText, UnknownCommandKeptLiterally)
{
    EXPECT_EQ(mathtext::to_plain_text("$\\foo + \\alpha$"), "\\foo + α");
    EXPECT_EQ(mathtext::to_plain_text("$\\frac{1}{2}$"), "\\frac12");
    EXPECT_EQ(mathtext::to_plain_text("$a\\$"), "$a$");       // odd after escape: plain, \$ -> $
    EXPECT_EQ(mathtext::to_plain_text("$a\\@b$"), "a\\@b");   // unknown escape
}

TEST(MathText, MathrmAndText)
{
    EXPECT_EQ(mathtext::to_plain_text("$\\mathrm{kg}$"), "kg");
    EXPECT_EQ(mathtext::to_plain_text("$\\text{max speed}_{x}$"), "max speedx");
    // \text keeps hyphens and backslash-letter sequences literally.
    EXPECT_EQ(mathtext::to_plain_text("$\\text{a-b \\alpha}$"), "a-b \\alpha");
    // \mathrm parses its contents as math (symbols are still converted).
    EXPECT_EQ(mathtext::to_plain_text("$\\mathrm{\\mu s}$"), "μ s");
}

// ─── Scripts ────────────────────────────────────────────────────────────────

TEST(MathText, SubscriptAndSuperscript)
{
    auto runs = text_runs("$\\alpha_1 = 10^{-3}$ s");
    ASSERT_EQ(runs.size(), 5u);
    EXPECT_EQ(runs[0].text, "α");
    EXPECT_FLOAT_EQ(runs[0].scale, 1.0f);
    EXPECT_EQ(runs[1].text, "1");
    EXPECT_FLOAT_EQ(runs[1].scale, mathtext::SCRIPT_SCALE);
    EXPECT_FLOAT_EQ(runs[1].shift, -mathtext::SUBSCRIPT_DROP);
    EXPECT_EQ(runs[2].text, " = 10");
    EXPECT_FLOAT_EQ(runs[2].scale, 1.0f);
    EXPECT_EQ(runs[3].text, "−3");
    EXPECT_FLOAT_EQ(runs[3].scale, mathtext::SCRIPT_SCALE);
    EXPECT_FLOAT_EQ(runs[3].shift, mathtext::SUPERSCRIPT_RISE);
    EXPECT_EQ(runs[4].text, " s");
    EXPECT_FLOAT_EQ(runs[4].shift, 0.0f);
}

TEST(MathText, SingleCharacterAndCommandScripts)
{
    auto runs = text_runs("$x^2y_\\mu$");
    ASSERT_EQ(runs.size(), 4u);
    EXPECT_EQ(runs[1].text, "2");
    EXPECT_EQ(runs[2].text, "y");
    EXPECT_FLOAT_EQ(runs[2].scale, 1.0f);   // only one character is scripted
    EXPECT_EQ(runs[3].text, "μ");
    EXPECT_FLOAT_EQ(runs[3].shift, -mathtext::SUBSCRIPT_DROP);
}

TEST(MathText, NestedBracesAndScripts)
{
    auto runs = text_runs("$e^{-t^{2}_{k}}$");
    ASSERT_GE(runs.size(), 3u);
    EXPECT_EQ(runs[0].text, "e");
    EXPECT_EQ(runs[1].text, "−t");
    EXPECT_FLOAT_EQ(runs[1].scale, mathtext::SCRIPT_SCALE);
    // Script of a script: smaller once more, raised / lowered relative to its base.
    EXPECT_EQ(runs[2].text, "2");
    EXPECT_FLOAT_EQ(runs[2].scale, mathtext::MIN_SCRIPT_SCALE);
    EXPECT_FLOAT_EQ(
        runs[2].shift,
        mathtext::SUPERSCRIPT_RISE + mathtext::SUPERSCRIPT_RISE * mathtext::SCRIPT_SCALE);
    ASSERT_EQ(runs.size(), 4u);
    EXPECT_EQ(runs[3].text, "k");
    EXPECT_FLOAT_EQ(runs[3].shift,
                    mathtext::SUPERSCRIPT_RISE - mathtext::SUBSCRIPT_DROP * mathtext::SCRIPT_SCALE);
    // Deeper levels keep the minimum size.
    auto deep = text_runs("$a^{b^{c^{d}}}$");
    ASSERT_EQ(deep.size(), 4u);
    EXPECT_FLOAT_EQ(deep[3].scale, mathtext::MIN_SCRIPT_SCALE);
}

TEST(MathText, PlainBraceGroupsAreTransparent)
{
    EXPECT_EQ(mathtext::to_plain_text("${a{b}}c$"), "abc");
    auto runs = text_runs("${a{b}}c$");
    ASSERT_EQ(runs.size(), 1u);
}

TEST(MathText, UnbalancedBracesTolerated)
{
    EXPECT_EQ(mathtext::to_plain_text("$x^{ab$"), "xab");   // unclosed: to segment end
    EXPECT_EQ(mathtext::to_plain_text("$a}b$"), "ab");      // stray close: dropped
}

TEST(MathText, DanglingScriptOperatorKeptLiterally)
{
    EXPECT_EQ(mathtext::to_plain_text("$x^$"), "x^");
    EXPECT_EQ(mathtext::to_plain_text("$x_{}$ y"), "x y");
    EXPECT_EQ(mathtext::to_plain_text("$a\\_b$"), "a_b");
}

// ─── Layout ─────────────────────────────────────────────────────────────────

TEST(MathText, LayoutAdvancesRunsByScaledWidth)
{
    auto        runs = mathtext::parse("$ab^{cd}e$");
    const float w    = mathtext::layout(runs, byte_advance);
    // "ab" 20, "cd" at 0.7 = 14, "e" 10.
    EXPECT_FLOAT_EQ(w, 44.0f);
    std::vector<MRun> t;
    for (const MRun& r : runs)
        if (r.kind == MRun::Kind::Text)
            t.push_back(r);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_FLOAT_EQ(t[0].x, 0.0f);
    EXPECT_FLOAT_EQ(t[1].x, 20.0f);
    EXPECT_FLOAT_EQ(t[2].x, 34.0f);
}

TEST(MathText, StackedSubAndSuperscriptShareX)
{
    auto              runs = mathtext::parse("$x_i^{2k}y$");
    const float       w    = mathtext::layout(runs, byte_advance);
    std::vector<MRun> t;
    for (const MRun& r : runs)
        if (r.kind == MRun::Kind::Text)
            t.push_back(r);
    ASSERT_EQ(t.size(), 4u);
    EXPECT_EQ(t[1].text, "i");
    EXPECT_EQ(t[2].text, "2k");
    EXPECT_FLOAT_EQ(t[1].x, 10.0f);
    EXPECT_FLOAT_EQ(t[2].x, 10.0f);   // stacked over the subscript
    // y follows the wider script ("2k" = 14 px).
    EXPECT_FLOAT_EQ(t[3].x, 24.0f);
    EXPECT_FLOAT_EQ(w, 34.0f);
}

TEST(MathText, OverhangGrowsOnlyForScripts)
{
    auto plain = mathtext::parse("$abc$");
    auto o     = mathtext::overhang(plain, 10.0f, 8.0f, 2.0f);
    EXPECT_FLOAT_EQ(o.above, 0.0f);
    EXPECT_FLOAT_EQ(o.below, 0.0f);

    auto sup = mathtext::parse("$x^2$");
    o        = mathtext::overhang(sup, 10.0f, 8.0f, 2.0f);
    // rise 3.8 + 0.7 * 8 = 9.4 above the baseline, 1.4 over the ascent.
    EXPECT_NEAR(o.above, 1.4f, 1e-4f);
    EXPECT_FLOAT_EQ(o.below, 0.0f);

    auto sub = mathtext::parse("$x_2$");
    o        = mathtext::overhang(sub, 10.0f, 8.0f, 2.0f);
    // drop 1.7 + 0.7 * 2 = 3.1 below the baseline, 1.1 under the descent.
    EXPECT_FLOAT_EQ(o.above, 0.0f);
    EXPECT_NEAR(o.below, 1.1f, 1e-4f);
}

// ─── Truncation ─────────────────────────────────────────────────────────────

TEST(MathText, CloseTruncatedPrefix)
{
    auto closed = [](std::string s)
    {
        mathtext::close_truncated(s);
        return s;
    };
    EXPECT_EQ(closed("plain text"), "plain text");
    EXPECT_EQ(closed("$a$ b"), "$a$ b");   // cut outside math
    EXPECT_EQ(closed("$\\alpha + b"), "$\\alpha + b$");
    EXPECT_EQ(closed("$\\alp"), "");                   // command cut short, empty math
    EXPECT_EQ(closed("x $\\alpha_"), "x $\\alpha$");   // dangling operator
    EXPECT_EQ(closed("x $10^{"), "x $10$");
    EXPECT_EQ(closed("$a\\_"), "$a\\_$");   // escaped underscore kept
}

TEST(MathText, LabelFittingKeepsTruncatedMathRenderable)
{
    // Width = bytes of the rendered text: the fitted label must stay math (balanced $).
    const LabelWidthFn measure = [](const std::string& t, LabelFont)
    { return static_cast<float>(mathtext::to_plain_text(t).size()); };
    const std::string text = "$\\alpha_1 + \\beta_2 + \\gamma_3 + \\delta_4$";
    const FittedLabel fit =
        fit_label_to_width(text, 12.0f, LabelFont::Label, LabelFont::Label, measure, "...");
    EXPECT_TRUE(fit.truncated);
    EXPECT_TRUE(mathtext::is_math_text(fit.text)) << fit.text;
    EXPECT_LE(measure(fit.text, LabelFont::Label), 12.0f);
}

// ─── Inter metrics (vector export) ──────────────────────────────────────────

TEST(MathText, InterMathtextWidth)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "Inter not embedded";
    // Plain text: identical to the plain measurement.
    EXPECT_FLOAT_EQ(inter_mathtext_width("Time (s)", 14.0f), inter_text_width("Time (s)", 14.0f));
    // A superscript is narrower than the same characters at full size.
    const float full   = inter_text_width("x23", 14.0f);
    const float script = inter_mathtext_width("$x^{23}$", 14.0f);
    EXPECT_LT(script, full);
    EXPECT_NEAR(script, inter_text_width("x", 14.0f) + inter_text_width("23", 14.0f * 0.7f), 1e-3f);
    // Greek command measured as its glyph, not as the source.
    EXPECT_FLOAT_EQ(inter_mathtext_width("$\\alpha$", 14.0f), inter_text_width("α", 14.0f));
}
