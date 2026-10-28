#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace spectra
{

// A codepoint and the glyph the font's cmap maps it to.
struct FontSubsetGlyph
{
    uint32_t codepoint;
    uint16_t glyph;
};

// Minimal TrueType (glyf-outline) subsetter for embedding fonts in SVG exports.
//
// Glyph ids are kept, so hmtx/hhea/maxp stay valid unchanged: glyphs outside the
// subset (other than .notdef and components of kept composite glyphs) become empty
// outlines, and loca/glyf, cmap (rebuilt for exactly `glyphs`) and post (format 3,
// no glyph names) shrink.  GSUB/GPOS/GDEF and every table not needed to render are
// dropped; hinting tables (cvt/fpgm/prep) are kept because kept glyphs reference them.
//
// Returns an empty vector when `font` is not a TrueType font this can parse
// (e.g. CFF outlines, collections, truncated tables).
std::vector<uint8_t> subset_truetype(std::span<const uint8_t>         font,
                                     std::span<const FontSubsetGlyph> glyphs);

// Standard base64 (RFC 4648, with padding), e.g. for a data: URL.
std::string base64_encode(std::span<const uint8_t> data);

}   // namespace spectra
