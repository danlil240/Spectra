#pragma once

#include "font_atlas.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace plotix {

struct TextVertex {
    float pos_x;
    float pos_y;
    float uv_x;
    float uv_y;
};

struct TextMeasurement {
    float width  = 0.0f;
    float height = 0.0f;
};

class TextRenderer {
public:
    TextRenderer() = default;

    // Initialize with a font atlas (must remain valid for lifetime of TextRenderer)
    void init(const FontAtlas* atlas);

    // Generate a batch of textured quads for a string
    // position is the baseline-left origin of the text
    // font_size is the desired rendering size in pixels
    // Returns the generated vertices (4 per glyph, forming quads)
    const std::vector<TextVertex>& generate_quads(
        const std::string& text,
        float pos_x, float pos_y,
        float font_size);

    // Generate quads and return index buffer too (6 indices per glyph: 2 triangles)
    void generate_quads_indexed(
        const std::string& text,
        float pos_x, float pos_y,
        float font_size,
        std::vector<TextVertex>& out_vertices,
        std::vector<uint32_t>& out_indices);

    // Measure text dimensions without generating geometry
    TextMeasurement measure_text(const std::string& text, float font_size) const;

    // Access last generated batch
    const std::vector<TextVertex>& vertices() const { return vertices_; }
    const std::vector<uint32_t>& indices() const { return indices_; }
    size_t glyph_count() const { return glyph_count_; }

    // Access the atlas
    const FontAtlas* atlas() const { return atlas_; }

private:
    const FontAtlas* atlas_ = nullptr;
    std::vector<TextVertex> vertices_;
    std::vector<uint32_t> indices_;
    size_t glyph_count_ = 0;
};

} // namespace plotix
