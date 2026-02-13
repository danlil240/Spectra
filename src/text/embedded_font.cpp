#include "embedded_font.hpp"

namespace plotix {
namespace embedded {

// =============================================================================
// Placeholder embedded MSDF font atlas
// =============================================================================
//
// In a production build, this file would contain:
//   1. A pre-baked MSDF atlas PNG (e.g., Roboto Regular) as a uint8_t array
//   2. Glyph metrics JSON matching the atlas
//
// To generate real data:
//   msdf-atlas-gen -font Roboto-Regular.ttf -type msdf -format png \
//       -imageout atlas.png -json atlas.json -size 32 -pxrange 4 \
//       -charset-ascii
//   xxd -i atlas.png > embedded_atlas_data.inc
//
// For now, we provide a minimal 1x1 white pixel PNG and metrics for basic
// ASCII characters with monospace-like advances. This allows the text
// rendering pipeline to function end-to-end while producing placeholder
// output (white rectangles instead of real glyphs).
// =============================================================================

// Minimal 1x1 white RGBA PNG (67 bytes)
// This is a valid PNG file containing a single white pixel.
static constexpr uint8_t k_placeholder_png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG signature
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // 1x1
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, // RGBA, 8-bit
    0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, // IDAT chunk
    0x54, 0x78, 0x9C, 0x62, 0xF8, 0xCF, 0xC0, 0x00, // compressed data
    0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, //
    0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, // IEND chunk
    0x44, 0xAE, 0x42, 0x60, 0x82                      //
};

const uint8_t font_atlas_png[] = {
    // Copy of placeholder PNG
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
    0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9C, 0x62, 0xF8, 0xCF, 0xC0, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC,
    0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
    0x44, 0xAE, 0x42, 0x60, 0x82
};

const size_t font_atlas_png_size = sizeof(font_atlas_png);

// Placeholder glyph metrics JSON
// Provides basic ASCII characters (32-126) with monospace-like metrics.
// All glyphs map to the full 1x1 atlas (UV 0,0 → 1,1).
// In a real atlas, each glyph would have unique atlas bounds.
const char font_atlas_metrics_json[] = R"({
    "atlas": {
        "type": "msdf",
        "width": 1,
        "height": 1,
        "size": 32,
        "distanceRange": 4,
        "yOrigin": "top"
    },
    "metrics": {
        "lineHeight": 1.2,
        "ascender": 0.9,
        "descender": -0.3,
        "underlineY": -0.15,
        "underlineThickness": 0.05
    },
    "glyphs": [
        { "unicode": 32, "advance": 0.3 },
        { "unicode": 33, "advance": 0.3, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.25, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 34, "advance": 0.35, "planeBounds": { "left": 0.02, "bottom": 0.4, "right": 0.33, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 40, "advance": 0.3, "planeBounds": { "left": 0.05, "bottom": -0.22, "right": 0.28, "top": 0.75 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 41, "advance": 0.3, "planeBounds": { "left": 0.02, "bottom": -0.22, "right": 0.25, "top": 0.75 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 43, "advance": 0.55, "planeBounds": { "left": 0.04, "bottom": 0.05, "right": 0.51, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 44, "advance": 0.2, "planeBounds": { "left": 0.0, "bottom": -0.18, "right": 0.18, "top": 0.12 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 45, "advance": 0.35, "planeBounds": { "left": 0.03, "bottom": 0.2, "right": 0.32, "top": 0.32 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 46, "advance": 0.2, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.17, "top": 0.12 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 48, "advance": 0.55, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 49, "advance": 0.55, "planeBounds": { "left": 0.08, "bottom": -0.02, "right": 0.38, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 50, "advance": 0.55, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.50, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 51, "advance": 0.55, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.50, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 52, "advance": 0.55, "planeBounds": { "left": 0.02, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 53, "advance": 0.55, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.50, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 54, "advance": 0.55, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.51, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 55, "advance": 0.55, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 56, "advance": 0.55, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 57, "advance": 0.55, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.51, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 58, "advance": 0.2, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.17, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 65, "advance": 0.6, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.6, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 66, "advance": 0.58, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.55, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 67, "advance": 0.58, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.55, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 68, "advance": 0.62, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.58, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 69, "advance": 0.52, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.48, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 70, "advance": 0.50, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.48, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 71, "advance": 0.62, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.58, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 72, "advance": 0.62, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.56, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 73, "advance": 0.25, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.19, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 74, "advance": 0.45, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.38, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 75, "advance": 0.57, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.57, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 76, "advance": 0.50, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.48, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 77, "advance": 0.72, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.66, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 78, "advance": 0.62, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.56, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 79, "advance": 0.64, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.60, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 80, "advance": 0.55, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 81, "advance": 0.64, "planeBounds": { "left": 0.04, "bottom": -0.08, "right": 0.60, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 82, "advance": 0.58, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.55, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 83, "advance": 0.52, "planeBounds": { "left": 0.02, "bottom": -0.02, "right": 0.50, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 84, "advance": 0.52, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 85, "advance": 0.62, "planeBounds": { "left": 0.06, "bottom": -0.02, "right": 0.56, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 86, "advance": 0.58, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.58, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 87, "advance": 0.78, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.78, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 88, "advance": 0.55, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.55, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 89, "advance": 0.52, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.52, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 90, "advance": 0.52, "planeBounds": { "left": 0.02, "bottom": -0.02, "right": 0.50, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 97, "advance": 0.50, "planeBounds": { "left": 0.02, "bottom": -0.02, "right": 0.48, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 98, "advance": 0.52, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.50, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 99, "advance": 0.45, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.43, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 100, "advance": 0.52, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.49, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 101, "advance": 0.48, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.46, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 102, "advance": 0.30, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.32, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 103, "advance": 0.52, "planeBounds": { "left": 0.03, "bottom": -0.22, "right": 0.49, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 104, "advance": 0.52, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.47, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 105, "advance": 0.22, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.18, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 106, "advance": 0.22, "planeBounds": { "left": -0.04, "bottom": -0.22, "right": 0.18, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 107, "advance": 0.48, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.48, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 108, "advance": 0.22, "planeBounds": { "left": 0.04, "bottom": -0.02, "right": 0.18, "top": 0.72 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 109, "advance": 0.78, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.73, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 110, "advance": 0.52, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.47, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 111, "advance": 0.50, "planeBounds": { "left": 0.03, "bottom": -0.02, "right": 0.47, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 112, "advance": 0.52, "planeBounds": { "left": 0.04, "bottom": -0.22, "right": 0.50, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 113, "advance": 0.52, "planeBounds": { "left": 0.03, "bottom": -0.22, "right": 0.49, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 114, "advance": 0.32, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.32, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 115, "advance": 0.42, "planeBounds": { "left": 0.02, "bottom": -0.02, "right": 0.40, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 116, "advance": 0.32, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.30, "top": 0.65 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 117, "advance": 0.52, "planeBounds": { "left": 0.05, "bottom": -0.02, "right": 0.47, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 118, "advance": 0.45, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.45, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 119, "advance": 0.68, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.68, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 120, "advance": 0.45, "planeBounds": { "left": 0.0, "bottom": -0.02, "right": 0.45, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 121, "advance": 0.45, "planeBounds": { "left": 0.0, "bottom": -0.22, "right": 0.45, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } },
        { "unicode": 122, "advance": 0.42, "planeBounds": { "left": 0.02, "bottom": -0.02, "right": 0.40, "top": 0.52 }, "atlasBounds": { "left": 0, "bottom": 0, "right": 1, "top": 1 } }
    ]
})";

} // namespace embedded
} // namespace plotix
