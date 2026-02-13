#include <gtest/gtest.h>

#include "text/font_atlas.hpp"

using namespace plotix;

TEST(FontAtlas, LoadEmbedded) {
    FontAtlas atlas;
    EXPECT_FALSE(atlas.is_loaded());
    bool ok = atlas.load_embedded();
    EXPECT_TRUE(ok);
    EXPECT_TRUE(atlas.is_loaded());
}

TEST(FontAtlas, AtlasDimensions) {
    FontAtlas atlas;
    atlas.load_embedded();
    EXPECT_EQ(atlas.atlas_width(), 512);
    EXPECT_EQ(atlas.atlas_height(), 512);
}

TEST(FontAtlas, PixelDataNotNull) {
    FontAtlas atlas;
    atlas.load_embedded();
    EXPECT_NE(atlas.pixel_data(), nullptr);
}

TEST(FontAtlas, FontMetrics) {
    FontAtlas atlas;
    atlas.load_embedded();
    EXPECT_GT(atlas.line_height(), 0.0f);
    EXPECT_GT(atlas.ascender(), 0.0f);
    EXPECT_LT(atlas.descender(), 0.0f);
}

TEST(FontAtlas, GlyphLookupSpace) {
    FontAtlas atlas;
    atlas.load_embedded();
    auto* g = atlas.glyph(U' ');
    EXPECT_NE(g, nullptr);
    EXPECT_GT(g->advance, 0.0f);
}

TEST(FontAtlas, GlyphLookupA) {
    FontAtlas atlas;
    atlas.load_embedded();
    auto* g = atlas.glyph(U'A');
    EXPECT_NE(g, nullptr);
    EXPECT_GT(g->advance, 0.0f);
    EXPECT_GT(g->width, 0.0f);
    EXPECT_GT(g->height, 0.0f);
}

TEST(FontAtlas, GlyphLookupDigits) {
    FontAtlas atlas;
    atlas.load_embedded();
    for (char32_t c = U'0'; c <= U'9'; ++c) {
        auto* g = atlas.glyph(c);
        EXPECT_NE(g, nullptr) << "Missing glyph for digit " << static_cast<int>(c);
    }
}

TEST(FontAtlas, GlyphLookupMissing) {
    FontAtlas atlas;
    atlas.load_embedded();
    // Codepoint outside ASCII range should return nullptr
    auto* g = atlas.glyph(0x4E2D);  // Chinese character
    EXPECT_EQ(g, nullptr);
}

TEST(FontAtlas, AllAsciiGlyphs) {
    FontAtlas atlas;
    atlas.load_embedded();
    // All printable ASCII (32-126) should be present
    for (char32_t c = 32; c <= 126; ++c) {
        auto* g = atlas.glyph(c);
        EXPECT_NE(g, nullptr) << "Missing glyph for codepoint " << static_cast<int>(c);
    }
}
