#include "font_atlas.hpp"
#include "embedded_font.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

// Use stb_image for PNG decoding (read-only, header-only)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace plotix {

bool FontAtlas::load_from_memory(const uint8_t* png_data, size_t png_size,
                                  const char* json_metrics) {
    // Decode PNG atlas image
    int w = 0, h = 0, ch = 0;
    uint8_t* decoded = stbi_load_from_memory(
        png_data, static_cast<int>(png_size), &w, &h, &ch, 4);

    if (!decoded) {
        return false;
    }

    width_    = w;
    height_   = h;
    channels_ = 4; // forced RGBA
    pixels_.assign(decoded, decoded + w * h * 4);
    stbi_image_free(decoded);

    // Parse glyph metrics
    if (!parse_metrics_json(json_metrics)) {
        pixels_.clear();
        return false;
    }

    return true;
}

bool FontAtlas::load_from_files(const std::string& png_path,
                                 const std::string& json_path) {
    // Read PNG file
    std::ifstream png_file(png_path, std::ios::binary | std::ios::ate);
    if (!png_file.is_open()) return false;

    auto png_size = png_file.tellg();
    png_file.seekg(0);
    std::vector<uint8_t> png_data(static_cast<size_t>(png_size));
    png_file.read(reinterpret_cast<char*>(png_data.data()), png_size);
    png_file.close();

    // Read JSON file
    std::ifstream json_file(json_path);
    if (!json_file.is_open()) return false;

    std::ostringstream json_ss;
    json_ss << json_file.rdbuf();
    std::string json_str = json_ss.str();

    return load_from_memory(png_data.data(), png_data.size(), json_str.c_str());
}

bool FontAtlas::load_embedded() {
    return load_from_memory(
        embedded::font_atlas_png,
        embedded::font_atlas_png_size,
        embedded::font_atlas_metrics_json);
}

const GlyphMetrics* FontAtlas::glyph(char32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    return nullptr;
}

// Minimal JSON parser for glyph metrics
// Expected format:
// {
//   "atlas": { "width": N, "height": N, "size": N },
//   "metrics": { "lineHeight": N, "ascender": N, "descender": N },
//   "glyphs": [
//     { "unicode": 65, "advance": 0.5,
//       "planeBounds": { "left": 0.0, "bottom": 0.0, "right": 0.5, "top": 0.8 },
//       "atlasBounds": { "left": 0, "bottom": 0, "right": 32, "top": 40 }
//     }, ...
//   ]
// }
//
// This is a minimal parser — not a full JSON implementation.
// It handles the specific format produced by msdf-atlas-gen.

namespace {

// Skip whitespace
const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}

// Parse a number (int or float)
double parse_number(const char*& p) {
    p = skip_ws(p);
    char* end = nullptr;
    double val = std::strtod(p, &end);
    p = end;
    return val;
}

// Find a key in JSON (very simplistic)
const char* find_key(const char* p, const char* key) {
    const char* found = std::strstr(p, key);
    if (!found) return nullptr;
    found += std::strlen(key);
    found = skip_ws(found);
    if (*found == '"') ++found; // skip closing quote if present
    found = skip_ws(found);
    if (*found == ':') ++found;
    return skip_ws(found);
}

// Find next occurrence of character
const char* find_char(const char* p, char c) {
    while (*p && *p != c) ++p;
    return *p ? p : nullptr;
}

} // anonymous namespace

bool FontAtlas::parse_metrics_json(const char* json) {
    if (!json || !*json) return false;

    // Parse atlas info
    const char* atlas_section = find_key(json, "\"size\"");
    if (atlas_section) {
        atlas_font_size_ = static_cast<float>(parse_number(atlas_section));
    }

    // Parse font metrics
    const char* metrics_section = find_key(json, "\"lineHeight\"");
    if (metrics_section) {
        line_height_ = static_cast<float>(parse_number(metrics_section));
    }

    const char* asc = find_key(json, "\"ascender\"");
    if (asc) {
        ascender_ = static_cast<float>(parse_number(asc));
    }

    const char* desc = find_key(json, "\"descender\"");
    if (desc) {
        descender_ = static_cast<float>(parse_number(desc));
    }

    // Parse glyphs array
    const char* glyphs_start = find_key(json, "\"glyphs\"");
    if (!glyphs_start) return false;

    // Skip to opening bracket
    glyphs_start = find_char(glyphs_start, '[');
    if (!glyphs_start) return false;
    ++glyphs_start;

    const char* p = glyphs_start;
    float atlas_w = static_cast<float>(width_);
    float atlas_h = static_cast<float>(height_);

    while (*p) {
        // Find next glyph object
        p = find_char(p, '{');
        if (!p) break;

        // Find the end of this glyph object
        const char* obj_end = find_char(p, '}');
        if (!obj_end) break;

        // Extract unicode
        const char* uni = find_key(p, "\"unicode\"");
        if (!uni || uni > obj_end) { p = obj_end + 1; continue; }
        char32_t codepoint = static_cast<char32_t>(parse_number(uni));

        GlyphMetrics gm{};

        // Extract advance
        const char* adv = find_key(p, "\"advance\"");
        if (adv && adv < obj_end) {
            gm.advance = static_cast<float>(parse_number(adv));
        }

        // Extract planeBounds (normalized glyph coordinates)
        const char* pb = find_key(p, "\"planeBounds\"");
        if (pb && pb < obj_end) {
            const char* pl = find_key(pb, "\"left\"");
            const char* pbot = find_key(pb, "\"bottom\"");
            const char* pr = find_key(pb, "\"right\"");
            const char* pt = find_key(pb, "\"top\"");

            float plane_left = pl ? static_cast<float>(parse_number(pl)) : 0.0f;
            float plane_bottom = pbot ? static_cast<float>(parse_number(pbot)) : 0.0f;
            float plane_right = pr ? static_cast<float>(parse_number(pr)) : 0.0f;
            float plane_top = pt ? static_cast<float>(parse_number(pt)) : 0.0f;

            gm.bearing_x = plane_left;
            gm.bearing_y = plane_top;
            gm.width  = (plane_right - plane_left) * atlas_font_size_;
            gm.height = (plane_top - plane_bottom) * atlas_font_size_;
        }

        // Extract atlasBounds (pixel coordinates in atlas)
        const char* ab = find_key(p, "\"atlasBounds\"");
        if (ab && ab < obj_end) {
            const char* al = find_key(ab, "\"left\"");
            const char* abot = find_key(ab, "\"bottom\"");
            const char* ar = find_key(ab, "\"right\"");
            const char* at_key = find_key(ab, "\"top\"");

            float atlas_left   = al ? static_cast<float>(parse_number(al)) : 0.0f;
            float atlas_bottom = abot ? static_cast<float>(parse_number(abot)) : 0.0f;
            float atlas_right  = ar ? static_cast<float>(parse_number(ar)) : 0.0f;
            float atlas_top    = at_key ? static_cast<float>(parse_number(at_key)) : 0.0f;

            // Convert pixel coords to UV [0,1]
            if (atlas_w > 0.0f && atlas_h > 0.0f) {
                gm.uv_x0 = atlas_left   / atlas_w;
                gm.uv_y0 = atlas_top    / atlas_h;  // top in atlas = min V
                gm.uv_x1 = atlas_right  / atlas_w;
                gm.uv_y1 = atlas_bottom / atlas_h;  // bottom in atlas = max V
            }
        }

        glyphs_[codepoint] = gm;
        p = obj_end + 1;
    }

    return !glyphs_.empty();
}

} // namespace plotix
