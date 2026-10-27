// Fallback-font discovery / selection (CPU only, no GPU).

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "render/font_fallback.hpp"

using namespace spectra;

#ifndef SPECTRA_SOURCE_DIR
    #define SPECTRA_SOURCE_DIR "."
#endif

namespace
{
const std::string INTER = std::string(SPECTRA_SOURCE_DIR) + "/third_party/Inter-Regular.ttf";
}

TEST(FontFallback, FontFileDataLoadsAndMoves)
{
    FontFileData f;
    ASSERT_TRUE(f.load(INTER));
    EXPECT_GT(f.size(), 1000u);
    EXPECT_TRUE(is_font_signature(f.data(), f.size()));

    const uint8_t* p = f.data();
    FontFileData   g(std::move(f));
    EXPECT_EQ(g.data(), p);
    EXPECT_TRUE(f.empty());

    FontFileData missing;
    EXPECT_FALSE(missing.load("/nonexistent/font.ttf"));
    EXPECT_TRUE(missing.empty());
}

TEST(FontFallback, SignatureCheck)
{
    const uint8_t ttf[12]  = {0x00, 0x01, 0x00, 0x00};
    const uint8_t otf[12]  = {'O', 'T', 'T', 'O'};
    const uint8_t ttc[12]  = {'t', 't', 'c', 'f'};
    const uint8_t junk[12] = {'P', 'K', 3, 4};
    EXPECT_TRUE(is_font_signature(ttf, sizeof(ttf)));
    EXPECT_TRUE(is_font_signature(otf, sizeof(otf)));
    EXPECT_TRUE(is_font_signature(ttc, sizeof(ttc)));
    EXPECT_FALSE(is_font_signature(junk, sizeof(junk)));
    EXPECT_FALSE(is_font_signature(ttf, 4));
    EXPECT_FALSE(is_font_signature(nullptr, 100));
}

TEST(FontFallback, SplitPathList)
{
#ifdef _WIN32
    auto v = split_font_path_list("a.ttf;;b.otf;");
#else
    auto v = split_font_path_list("a.ttf::b.otf:");
#endif
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0], "a.ttf");
    EXPECT_EQ(v[1], "b.otf");
    EXPECT_TRUE(split_font_path_list("").empty());
}

TEST(FontFallback, DefaultIgnorable)
{
    EXPECT_TRUE(is_default_ignorable(0x200B));   // zero-width space
    EXPECT_TRUE(is_default_ignorable(0xFE0F));   // variation selector-16
    EXPECT_TRUE(is_default_ignorable(0xFEFF));   // BOM
    EXPECT_FALSE(is_default_ignorable('A'));
    EXPECT_FALSE(is_default_ignorable(0x222B));   // integral
    EXPECT_FALSE(is_default_ignorable(0x6E29));   // CJK
}

TEST(FontFallback, ChainLoadsLazilyAndSkipsMissingFiles)
{
    FontFallbackChain chain({"/nonexistent/a.ttf", INTER});
    EXPECT_EQ(chain.loaded_count(), 0u);   // nothing mapped until needed

    EXPECT_EQ(chain.find('A'), 1);   // first font that exists and has the glyph
    EXPECT_EQ(chain.loaded_count(), 1u);
    EXPECT_NE(chain.font_data(1), nullptr);
    EXPECT_NE(chain.font_info(1), nullptr);
    EXPECT_EQ(chain.font_data(0), nullptr);
    EXPECT_EQ(chain.font_data(7), nullptr);

    EXPECT_EQ(chain.find(0x222B), 1);    // Inter has the integral sign
    EXPECT_EQ(chain.find(0x6E29), -1);   // ...but no CJK
}

TEST(FontFallback, ChainPriorityOrder)
{
    FontFallbackChain chain({INTER, INTER});
    EXPECT_EQ(chain.find('x'), 0);
    EXPECT_EQ(chain.loaded_count(), 1u);   // the second font is never touched
}

TEST(FontFallback, EmptyChainFindsNothing)
{
    FontFallbackChain chain;
    EXPECT_EQ(chain.find('A'), -1);
    EXPECT_EQ(chain.loaded_count(), 0u);
}

TEST(FontFallback, DiscoverHonoursEnvironment)
{
    const char* old   = std::getenv("SPECTRA_FALLBACK_FONTS");
    std::string saved = old ? old : "";
#ifdef _WIN32
    _putenv_s("SPECTRA_FALLBACK_FONTS", (INTER + ";/nonexistent/x.ttf").c_str());
#else
    setenv("SPECTRA_FALLBACK_FONTS", (INTER + ":/nonexistent/x.ttf").c_str(), 1);
#endif
    auto fonts = discover_fallback_fonts();
    ASSERT_FALSE(fonts.empty());
    EXPECT_EQ(fonts.front(), INTER);   // user fonts come first
    for (const auto& p : fonts)
    {
        EXPECT_NE(p, "/nonexistent/x.ttf");   // missing files are dropped
    }
#ifdef _WIN32
    _putenv_s("SPECTRA_FALLBACK_FONTS", saved.c_str());
#else
    if (old)
        setenv("SPECTRA_FALLBACK_FONTS", saved.c_str(), 1);
    else
        unsetenv("SPECTRA_FALLBACK_FONTS");
#endif
}
