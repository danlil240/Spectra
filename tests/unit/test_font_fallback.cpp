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

// ─── Outline check / fontconfig / per-codepoint system lookup ───────────────

namespace
{
// Restores an environment variable on scope exit.
class ScopedEnv
{
   public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        const char* old = std::getenv(name);
        had_            = old != nullptr;
        if (had_)
            saved_ = old;
        set(value);
    }
    ~ScopedEnv() { set(had_ ? saved_.c_str() : nullptr); }

   private:
    void set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(name_, value ? value : "");
#else
        if (value)
            setenv(name_, value, 1);
        else
            unsetenv(name_);
#endif
    }
    const char* name_;
    bool        had_ = false;
    std::string saved_;
};

bool file_exists(const std::string& p)
{
    FontFileData f;
    return f.load(p);
}
}   // namespace

TEST(FontFallback, OutlineFontCheck)
{
    FontFileData f;
    ASSERT_TRUE(f.load(INTER));
    EXPECT_TRUE(is_outline_font(f.data(), f.size()));
    EXPECT_FALSE(is_outline_font(f.data(), f.size(), 3));   // not a collection
    const uint8_t junk[64] = {0x00, 0x01, 0x00, 0x00};
    EXPECT_FALSE(is_outline_font(junk, sizeof(junk)));
    EXPECT_FALSE(is_outline_font(nullptr, 0));
}

TEST(FontFallback, ColourBitmapEmojiFontIsNotAnOutlineFont)
{
    const std::string colour = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf";
    FontFileData      f;
    if (!f.load(colour))
        GTEST_SKIP() << "Noto Color Emoji not installed";
    EXPECT_TRUE(is_font_signature(f.data(), f.size()));
    EXPECT_FALSE(is_outline_font(f.data(), f.size()));   // CBDT bitmaps only

    // A chain holding it never claims emoji: the glyphs could not be rasterised.
    FontFallbackChain chain({colour});
    EXPECT_EQ(chain.find(0x1F600), -1);
}

TEST(FontFallback, ChainAsksSystemLookupOncePerMissingCodepoint)
{
    FontFallbackChain chain;
    int               calls = 0;
    chain.set_system_lookup(
        [&](uint32_t cp, SystemFontMatch& out)
        {
            ++calls;
            if (cp == 'A' || cp == 0x6E29)   // "Inter" for A, and wrongly for CJK
            {
                out.path = INTER;
                return true;
            }
            return false;
        });

    EXPECT_EQ(chain.find('A'), 0);   // appended from the lookup
    EXPECT_EQ(calls, 1);
    ASSERT_EQ(chain.paths().size(), 1u);
    EXPECT_EQ(chain.paths()[0], INTER);
    EXPECT_EQ(chain.face_index(0), 0);
    EXPECT_EQ(chain.find('B'), 0);   // now in the chain: no lookup needed
    EXPECT_EQ(calls, 1);

    // The lookup names a font already in the chain that lacks the glyph: a miss,
    // remembered so the (slow) system query is not repeated.
    EXPECT_EQ(chain.find(0x6E29), -1);
    EXPECT_EQ(chain.find(0x6E29), -1);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(chain.paths().size(), 1u);

    EXPECT_EQ(chain.find(0x4E2D), -1);   // lookup has nothing
    EXPECT_EQ(chain.find(0x4E2D), -1);
    EXPECT_EQ(calls, 3);
}

TEST(FontFallback, AddFontDeduplicates)
{
    FontFallbackChain chain({INTER});
    EXPECT_EQ(chain.add_font(INTER), 0);
    EXPECT_EQ(chain.add_font("/nonexistent/x.ttc", 2), 1);
    EXPECT_EQ(chain.add_font("/nonexistent/x.ttc", 2), 1);
    EXPECT_EQ(chain.add_font("/nonexistent/x.ttc", 1), 2);
    EXPECT_EQ(chain.paths().size(), 3u);
    EXPECT_EQ(chain.find('A'), 0);
}

TEST(FontFallback, FontconfigFindsOutlineFonts)
{
    if (!system_font_lookup_available())
        GTEST_SKIP() << "fontconfig not available";
    SystemFontMatch m;
    ASSERT_TRUE(find_system_font_for('A', m));
    EXPECT_TRUE(file_exists(m.path)) << m.path;

    // Whatever the system picks for an emoji / CJK / maths codepoint has outlines
    // and really contains the glyph.
    for (uint32_t cp : {0x1F600u, 0x4E2Du, 0x222Eu})
    {
        SystemFontMatch e;
        if (!find_system_font_for(cp, e))
            continue;
        FontFileData f;
        ASSERT_TRUE(f.load(e.path)) << e.path;
        EXPECT_TRUE(is_outline_font(f.data(), f.size(), e.face_index)) << e.path;
        FontFallbackChain chain;
        EXPECT_EQ(chain.add_font(e.path, e.face_index), 0);
        EXPECT_EQ(chain.find(cp), 0) << std::hex << cp << " " << e.path;
    }

    // Discovery never returns a colour-only font (ImGui and stb_truetype cannot use it).
    for (const auto& p : discover_fallback_fonts())
    {
        FontFileData f;
        ASSERT_TRUE(f.load(p)) << p;
        EXPECT_TRUE(is_outline_font(f.data(), f.size())) << p;
    }
}

TEST(FontFallback, FontconfigCanBeDisabled)
{
    ScopedEnv       env("SPECTRA_FONTCONFIG", "0");
    SystemFontMatch m;
    EXPECT_FALSE(system_font_lookup_available());
    EXPECT_FALSE(find_system_font_for('A', m));
    // The fixed candidate list still applies.
    for (const auto& p : discover_fallback_fonts())
        EXPECT_TRUE(file_exists(p)) << p;
}

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_internal.h>

// ImGui is built with IMGUI_USE_WCHAR32: code points above U+FFFF (emoji, maths
// alphanumerics) decode to themselves instead of U+FFFD.
TEST(FontFallback, ImGuiDecodesSupplementaryPlaneCodepoints)
{
    static_assert(sizeof(ImWchar) == 4, "IMGUI_USE_WCHAR32 must be defined");
    const char*  s  = "\xF0\x9F\x98\x80";   // U+1F600
    unsigned int cp = 0;
    EXPECT_EQ(ImTextCharFromUtf8(&cp, s, nullptr), 4);
    EXPECT_EQ(cp, 0x1F600u);
    ImWchar buf[4] = {};
    ImTextStrFromUtf8(buf, 4, s, nullptr);
    EXPECT_EQ(static_cast<uint32_t>(buf[0]), 0x1F600u);
    EXPECT_EQ(buf[1], 0u);
}
#endif
