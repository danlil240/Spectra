#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace plotix {

struct GlyphMetrics {
    float uv_x0 = 0.0f;      // atlas UV left
    float uv_y0 = 0.0f;      // atlas UV top
    float uv_x1 = 0.0f;      // atlas UV right
    float uv_y1 = 0.0f;      // atlas UV bottom
    float width  = 0.0f;     // glyph width in pixels (at atlas font size)
    float height = 0.0f;     // glyph height in pixels
    float bearing_x = 0.0f;  // horizontal bearing
    float bearing_y = 0.0f;  // vertical bearing (baseline to top)
    float advance   = 0.0f;  // horizontal advance to next glyph
};

class FontAtlas {
public:
    FontAtlas() = default;

    // Load a pre-baked MSDF atlas from PNG image data + JSON glyph metrics
    bool load_from_memory(const uint8_t* png_data, size_t png_size,
                          const char* json_metrics);

    // Load from file paths
    bool load_from_files(const std::string& png_path,
                         const std::string& json_path);

    // Load from embedded font data (see embedded_font.hpp)
    bool load_embedded();

    // Glyph lookup
    const GlyphMetrics* glyph(char32_t codepoint) const;

    // Atlas texture data (RGBA)
    const uint8_t* pixel_data() const { return pixels_.data(); }
    int atlas_width()  const { return width_; }
    int atlas_height() const { return height_; }
    int atlas_channels() const { return channels_; }
    bool is_loaded() const { return !pixels_.empty(); }

    // Font metrics
    float line_height() const { return line_height_; }
    float ascender()    const { return ascender_; }
    float descender()   const { return descender_; }
    float atlas_font_size() const { return atlas_font_size_; }

private:
    bool parse_metrics_json(const char* json);

    std::unordered_map<char32_t, GlyphMetrics> glyphs_;
    std::vector<uint8_t> pixels_;
    int width_    = 0;
    int height_   = 0;
    int channels_ = 0;

    float line_height_    = 0.0f;
    float ascender_       = 0.0f;
    float descender_      = 0.0f;
    float atlas_font_size_ = 32.0f;  // default atlas size
};

} // namespace plotix
