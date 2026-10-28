#include "io/text_metrics.hpp"

#include <algorithm>
#include <vector>

#include "io/font_subset.hpp"

// stb_truetype with internal linkage, so it cannot clash with the renderer's
// own (external) stb_truetype implementation in text_renderer.cpp.
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wall"
    #pragma clang diagnostic ignored "-Wextra"
    #pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#if __has_include("inter_font_embedded.hpp")
    #include "inter_font_embedded.hpp"
    #define SPECTRA_HAS_EMBEDDED_INTER 1
#endif

namespace spectra
{

namespace
{

// Parsed font plus a cache of ASCII advances (in em units).  Immutable once
// built; function-local static initialisation makes the lazy build thread-safe.
struct InterMetrics
{
    bool           ok = false;
    stbtt_fontinfo info{};
    float          em_scale = 0.0f;   // font units -> em
    float          ascii_em[128]{};

    InterMetrics()
    {
        const auto font = embedded_inter_font();
        if (font.empty())
            return;
        const int offset = stbtt_GetFontOffsetForIndex(font.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, font.data(), offset))
            return;
        em_scale = stbtt_ScaleForMappingEmToPixels(&info, 1.0f);
        if (!(em_scale > 0.0f))
            return;
        for (int cp = 0; cp < 128; ++cp)
            ascii_em[cp] = glyph_em(cp);
        ok = true;
    }

    // Advance of `cp` in em, or a negative value if Inter has no glyph for it.
    [[nodiscard]] float glyph_em(int cp) const
    {
        const int g = stbtt_FindGlyphIndex(&info, cp);
        if (g == 0)
            return -1.0f;
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info, g, &advance, &lsb);
        return static_cast<float>(advance) * em_scale;
    }
};

const InterMetrics& inter_metrics()
{
    static const InterMetrics m;
    return m;
}

// Width estimate for a codepoint the font does not cover.
float fallback_em(uint32_t cp)
{
    if (cp >= 0x2E80)
        return 1.0f;   // CJK and other full-width scripts
    return 0.62f;
}

uint32_t next_codepoint(std::string_view s, size_t& i)
{
    const auto c0  = static_cast<unsigned char>(s[i]);
    uint32_t   cp  = c0;
    size_t     len = 1;
    if (c0 >= 0xF0)
        len = 4;
    else if (c0 >= 0xE0)
        len = 3;
    else if (c0 >= 0xC0)
        len = 2;
    if (len > 1 && i + len <= s.size())
    {
        cp = c0 & (0xFFu >> (len + 1));
        for (size_t k = 1; k < len; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
    }
    else
        len = 1;
    i += len;
    return cp;
}

}   // namespace

std::span<const uint8_t> embedded_inter_font()
{
#ifdef SPECTRA_HAS_EMBEDDED_INTER
    return {InterFont_ttf_data, static_cast<size_t>(InterFont_ttf_size)};
#else
    return {};
#endif
}

bool inter_metrics_available()
{
    return inter_metrics().ok;
}

float inter_text_width(std::string_view text, float font_px)
{
    const InterMetrics& m  = inter_metrics();
    float               em = 0.0f;
    size_t              i  = 0;
    while (i < text.size())
    {
        const uint32_t cp = next_codepoint(text, i);
        float          w  = -1.0f;
        if (m.ok)
            w = cp < 128 ? m.ascii_em[cp] : m.glyph_em(static_cast<int>(cp));
        em += w >= 0.0f ? w : fallback_em(cp);
    }
    return em * font_px;
}

bool inter_covers(std::string_view text)
{
    const InterMetrics& m = inter_metrics();
    if (!m.ok)
        return false;
    size_t i = 0;
    while (i < text.size())
    {
        const uint32_t cp = next_codepoint(text, i);
        const float    w  = cp < 128 ? m.ascii_em[cp] : m.glyph_em(static_cast<int>(cp));
        if (w < 0.0f)
            return false;
    }
    return true;
}

std::vector<uint32_t> utf8_codepoints(std::string_view text)
{
    std::vector<uint32_t> out;
    size_t                i = 0;
    while (i < text.size())
        out.push_back(next_codepoint(text, i));
    return out;
}

std::vector<uint8_t> inter_font_subset(std::span<const uint32_t> codepoints)
{
    const InterMetrics& m = inter_metrics();
    if (!m.ok)
        return {};
    std::vector<FontSubsetGlyph> glyphs;
    glyphs.reserve(codepoints.size());
    for (const uint32_t cp : codepoints)
    {
        const int g = stbtt_FindGlyphIndex(&m.info, static_cast<int>(cp));
        if (g > 0)
            glyphs.push_back({cp, static_cast<uint16_t>(g)});
    }
    return subset_truetype(embedded_inter_font(), glyphs);
}

}   // namespace spectra
