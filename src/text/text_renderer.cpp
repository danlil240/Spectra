#include "text_renderer.hpp"

#include <cmath>

namespace plotix {

void TextRenderer::init(const FontAtlas* atlas) {
    atlas_ = atlas;
}

const std::vector<TextVertex>& TextRenderer::generate_quads(
    const std::string& text,
    float pos_x, float pos_y,
    float font_size) {

    vertices_.clear();
    indices_.clear();
    glyph_count_ = 0;

    if (!atlas_ || !atlas_->is_loaded() || text.empty()) {
        return vertices_;
    }

    float scale = font_size / atlas_->atlas_font_size();
    float cursor_x = pos_x;
    float cursor_y = pos_y;

    for (size_t i = 0; i < text.size(); ++i) {
        char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(text[i]));

        const GlyphMetrics* gm = atlas_->glyph(cp);
        if (!gm) {
            // Skip unknown glyphs, but still advance by a default amount
            cursor_x += font_size * 0.5f;
            continue;
        }

        // Compute glyph quad position
        float glyph_w = gm->width * scale;
        float glyph_h = gm->height * scale;
        float x0 = cursor_x + gm->bearing_x * scale * atlas_->atlas_font_size();
        float y0 = cursor_y - gm->bearing_y * scale * atlas_->atlas_font_size();
        float x1 = x0 + glyph_w;
        float y1 = y0 + glyph_h;

        // Only emit geometry for visible glyphs (skip spaces etc. with zero size)
        if (glyph_w > 0.0f && glyph_h > 0.0f) {
            uint32_t base_idx = static_cast<uint32_t>(vertices_.size());

            // 4 vertices per glyph quad
            // Top-left
            vertices_.push_back({x0, y0, gm->uv_x0, gm->uv_y0});
            // Top-right
            vertices_.push_back({x1, y0, gm->uv_x1, gm->uv_y0});
            // Bottom-left
            vertices_.push_back({x0, y1, gm->uv_x0, gm->uv_y1});
            // Bottom-right
            vertices_.push_back({x1, y1, gm->uv_x1, gm->uv_y1});

            // 6 indices per glyph (2 triangles)
            indices_.push_back(base_idx + 0);
            indices_.push_back(base_idx + 2);
            indices_.push_back(base_idx + 1);
            indices_.push_back(base_idx + 1);
            indices_.push_back(base_idx + 2);
            indices_.push_back(base_idx + 3);

            ++glyph_count_;
        }

        // Advance cursor
        cursor_x += gm->advance * scale * atlas_->atlas_font_size();
    }

    return vertices_;
}

void TextRenderer::generate_quads_indexed(
    const std::string& text,
    float pos_x, float pos_y,
    float font_size,
    std::vector<TextVertex>& out_vertices,
    std::vector<uint32_t>& out_indices) {

    generate_quads(text, pos_x, pos_y, font_size);
    out_vertices = vertices_;
    out_indices = indices_;
}

TextMeasurement TextRenderer::measure_text(const std::string& text,
                                            float font_size) const {
    TextMeasurement result;

    if (!atlas_ || !atlas_->is_loaded() || text.empty()) {
        return result;
    }

    float scale = font_size / atlas_->atlas_font_size();
    float cursor_x = 0.0f;
    float max_ascent = 0.0f;
    float max_descent = 0.0f;

    for (size_t i = 0; i < text.size(); ++i) {
        char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(text[i]));

        const GlyphMetrics* gm = atlas_->glyph(cp);
        if (!gm) {
            cursor_x += font_size * 0.5f;
            continue;
        }

        float ascent  = gm->bearing_y * scale * atlas_->atlas_font_size();
        float descent = (gm->height - gm->bearing_y * atlas_->atlas_font_size()) * scale;

        max_ascent  = std::max(max_ascent, ascent);
        max_descent = std::max(max_descent, descent);

        cursor_x += gm->advance * scale * atlas_->atlas_font_size();
    }

    result.width  = cursor_x;
    result.height = max_ascent + max_descent;

    return result;
}

} // namespace plotix
