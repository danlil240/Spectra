#include <algorithm>
#include <gtest/gtest.h>
#include <string>

#include "render/text_fit.hpp"
#include "render/text_renderer.hpp"

using namespace spectra;

namespace
{

// Fake metrics: every codepoint is a fixed width that depends on the font size.
float glyph_width(FontSize fs)
{
    switch (fs)
    {
        case FontSize::Title:
            return 10.0f;
        case FontSize::Label:
            return 8.0f;
        default:
            return 6.0f;
    }
}

float fake_measure(const std::string& s, FontSize fs)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size();)
    {
        uint32_t cp = 0;
        i += TextRenderer::decode_utf8(s, i, cp);
        ++n;
    }
    return static_cast<float>(n) * glyph_width(fs);
}

}   // namespace

TEST(TextFit, FitsAtPreferredSize)
{
    auto r = fit_text_to_width("Hello", 50.0f, FontSize::Title, FontSize::Label, fake_measure);
    EXPECT_EQ(r.text, "Hello");
    EXPECT_EQ(r.size, FontSize::Title);
    EXPECT_FALSE(r.truncated);
}

TEST(TextFit, ShrinksBeforeEllipsizing)
{
    // 10 chars: 100px at Title, 80px at Label.  85px lies between the two tiers, so the
    // Title glyphs are scaled to fill exactly 85px rather than snapping down to Label.
    auto r = fit_text_to_width("abcdefghij", 85.0f, FontSize::Title, FontSize::Label, fake_measure);
    EXPECT_EQ(r.text, "abcdefghij");
    EXPECT_EQ(r.size, FontSize::Title);
    EXPECT_FLOAT_EQ(r.scale, 0.85f);
    EXPECT_FALSE(r.truncated);

    // Exactly the Label width: the Label tier is used unscaled.
    r = fit_text_to_width("abcdefghij", 80.0f, FontSize::Title, FontSize::Label, fake_measure);
    EXPECT_EQ(r.size, FontSize::Label);
    EXPECT_FLOAT_EQ(r.scale, 1.0f);
    EXPECT_FALSE(r.truncated);
}

TEST(TextFit, TitleShrinkIsContinuous)
{
    // Drawn width (tier width x scale) as the budget shrinks from 120px to 80px: the text
    // fills the whole budget once it no longer fits at Title size, without the jump from
    // 100px straight to 80px that snapping to the Label tier would cause.
    const std::string text = "abcdefghij";
    float             prev = 1e9f;
    for (float budget = 120.0f; budget >= 80.0f; budget -= 0.5f)
    {
        const auto r =
            fit_text_to_width(text, budget, FontSize::Title, FontSize::Label, fake_measure);
        const float drawn = fake_measure(r.text, r.size) * r.scale;
        EXPECT_FALSE(r.truncated) << budget;
        EXPECT_LE(drawn, budget + 1e-3f) << budget;
        EXPECT_NEAR(drawn, std::min(budget, 100.0f), 1e-3f) << budget;
        EXPECT_LE(drawn, prev + 1e-3f) << budget;
        prev = drawn;
    }
}

TEST(TextFit, DoesNotShrinkBelowMinimum)
{
    // Would fit at Tick (60px) but the minimum is Label, so it is ellipsized.
    auto r = fit_text_to_width("abcdefghij", 64.0f, FontSize::Title, FontSize::Label, fake_measure);
    EXPECT_EQ(r.size, FontSize::Label);
    EXPECT_TRUE(r.truncated);
    // 64px / 8px = 8 glyphs: 5 kept + "..."
    EXPECT_EQ(r.text, "abcde...");
    EXPECT_LE(fake_measure(r.text, r.size), 64.0f);
}

TEST(TextFit, EllipsizedTextIsLongestFittingPrefix)
{
    const std::string text = "Subplot 1 with a rather long descriptive title";
    for (float w = 40.0f; w < 400.0f; w += 7.0f)
    {
        auto r = fit_text_to_width(text, w, FontSize::Label, FontSize::Label, fake_measure, "...");
        EXPECT_LE(fake_measure(r.text, r.size), w) << "width " << w;
        if (r.truncated)
        {
            ASSERT_GE(r.text.size(), 3u);
            EXPECT_EQ(r.text.substr(r.text.size() - 3), "...");
        }
    }
}

TEST(TextFit, TrimsTrailingSpaceBeforeEllipsis)
{
    // 40px fits 5 glyphs of 8px: "ab" + "..." — the space before the cut is dropped.
    auto r = fit_text_to_width("ab cdef", 40.0f, FontSize::Label, FontSize::Label, fake_measure);
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.text, "ab...");
}

TEST(TextFit, Utf8EllipsisAndCodepointBoundaries)
{
    const std::string ellipsis = "\xE2\x80\xA6";                       // U+2026
    const std::string text     = "\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4";   // αβγδ
    // 3 glyphs fit: 2 Greek letters + ellipsis, never split a multi-byte sequence.
    auto r =
        fit_text_to_width(text, 24.0f, FontSize::Label, FontSize::Label, fake_measure, ellipsis);
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.text, "\xCE\xB1\xCE\xB2" + ellipsis);
}

TEST(TextFit, EmptyWhenNothingFits)
{
    auto r = fit_text_to_width("abc", 0.0f, FontSize::Title, FontSize::Label, fake_measure);
    EXPECT_TRUE(r.truncated);
    EXPECT_TRUE(r.text.empty());
}

TEST(TextFit, EmptyInputUnchanged)
{
    auto r = fit_text_to_width("", 0.0f, FontSize::Title, FontSize::Label, fake_measure);
    EXPECT_TRUE(r.text.empty());
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(r.size, FontSize::Title);
}
