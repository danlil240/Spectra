#include <gtest/gtest.h>

#include "ui/figures/tab_title_fit.hpp"

using spectra::ui::fit_tab_title;

namespace
{
// One unit per codepoint (UTF-8 lead bytes), like a monospace font.
float measure(std::string_view s)
{
    float n = 0.0f;
    for (unsigned char c : s)
        n += (c & 0xC0) != 0x80 ? 1.0f : 0.0f;
    return n;
}
}   // namespace

TEST(TabTitleFit, FittingTitleIsUnchanged)
{
    EXPECT_EQ(fit_tab_title("Figure 3", 8.0f, measure), "Figure 3");
}

TEST(TabTitleFit, KeepsTheTrailingFigureNumber)
{
    EXPECT_EQ(fit_tab_title("Figure 12", 6.0f, measure),
              "Fig\xE2\x80\xA6"
              "12");
    EXPECT_EQ(fit_tab_title("Figure 12", 3.0f, measure),
              "\xE2\x80\xA6"
              "12");
    EXPECT_EQ(fit_tab_title("Figure 12", 8.0f, measure),
              "Figur\xE2\x80\xA6"
              "12");
}

TEST(TabTitleFit, BareNumberWhenEllipsisAndNumberDoNotFit)
{
    EXPECT_EQ(fit_tab_title("Figure 12", 2.0f, measure), "12");
}

TEST(TabTitleFit, TitlesWithoutNumberGetEndEllipsis)
{
    EXPECT_EQ(fit_tab_title("Temperature", 5.0f, measure), "Temp\xE2\x80\xA6");
}

TEST(TabTitleFit, NeverSplitsAMultibyteCharacter)
{
    // "Δt series": the head must not end inside the two-byte Δ.
    const std::string out = fit_tab_title("\xCE\x94t series", 3.0f, measure);
    EXPECT_EQ(out, "\xCE\x94t\xE2\x80\xA6");
}

TEST(TabTitleFit, NothingFitsGivesEmpty)
{
    EXPECT_EQ(fit_tab_title("Figure 1", 0.5f, measure), "");
    // Only one character fits: the bare number wins over "…".
    EXPECT_EQ(fit_tab_title("Figure 1", 1.0f, measure), "1");
    EXPECT_EQ(fit_tab_title("Temperature", 1.0f, measure), "\xE2\x80\xA6");
}
