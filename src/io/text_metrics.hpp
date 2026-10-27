#pragma once

#include <cstdint>
#include <span>
#include <string_view>

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

}   // namespace spectra
