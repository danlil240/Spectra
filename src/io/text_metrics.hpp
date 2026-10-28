#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace spectra
{

// Text measurement with the real metrics of the embedded Inter font (the face
// the renderer draws plot text with).  Headless and GPU-free: the font is parsed
// with stb_truetype on first use.

// The embedded Inter-Regular.ttf bytes (empty when the build has no embedded font).
std::span<const uint8_t> embedded_inter_font();

// True when Inter metrics are available (the font is embedded and parses).
bool inter_metrics_available();

// Advance width in pixels of UTF-8 `text` set in Inter at a CSS font size
// (em size) of `font_px` — what an SVG viewer lays out for font-size="font_px".
// Glyphs Inter lacks (e.g. CJK) use a per-script estimate, as does the whole
// string when the font is unavailable.  No kerning: kerning only tightens text,
// so the result is a safe upper bound for fitting boxes around the text.
float inter_text_width(std::string_view text, float font_px);

// True when Inter has a glyph for every codepoint of UTF-8 `text`, i.e. when
// inter_text_width() is an exact advance sum rather than an estimate.
bool inter_covers(std::string_view text);

// The embedded Inter font reduced to the glyphs of `codepoints` (plus .notdef and
// the components of composite glyphs), as a standalone TrueType file for an SVG
// @font-face.  Layout tables (GSUB/GPOS/GDEF) are dropped, so viewers set the text
// without kerning, exactly as inter_text_width() measures it.  Empty when the font
// is unavailable or cannot be subset.
std::vector<uint8_t> inter_font_subset(std::span<const uint32_t> codepoints);

// Codepoints of UTF-8 `text`, in order (malformed bytes decode as themselves).
std::vector<uint32_t> utf8_codepoints(std::string_view text);

}   // namespace spectra
