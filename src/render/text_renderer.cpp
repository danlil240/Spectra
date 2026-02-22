#include "text_renderer.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace spectra
{

// Pixel sizes for each FontSize preset — tuned for premium scientific visualization.
// Slightly larger than typical UI fonts for readability at a glance.
static constexpr float FONT_PIXEL_SIZES[] = {
    14.0f,  // Tick  — tick labels (compact but legible)
    16.0f,  // Label — axis labels
    20.0f,  // Title — plot title (clearly distinguished)
};

// Oversampling factor: 2x gives excellent sub-pixel positioning with
// minimal atlas size overhead. This is the key to uniform letter sizing —
// stb_truetype rasterizes at 2x resolution then downsamples, producing
// proper anti-aliased coverage values and fractional glyph offsets.
static constexpr unsigned int OVERSAMPLE = 2;

// ASCII range to bake (space through tilde)
static constexpr int FIRST_CHAR = 32;
static constexpr int NUM_CHARS = 95;  // 32..126 inclusive

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer() = default;

bool TextRenderer::init(Backend& backend, const uint8_t* font_data, size_t font_data_size)
{
    if (!font_data || font_data_size < 12)
        return false;

    // Validate TrueType/OpenType signature before passing to stb_truetype.
    // TrueType: 00 01 00 00, OpenType: 4F 54 54 4F ('OTTO'), TTC: 74 74 63 66
    uint32_t tag =
        (static_cast<uint32_t>(font_data[0]) << 24) | (static_cast<uint32_t>(font_data[1]) << 16)
        | (static_cast<uint32_t>(font_data[2]) << 8) | static_cast<uint32_t>(font_data[3]);
    bool valid_sig = (tag == 0x00010000u)      // TrueType
                     || (tag == 0x4F54544Fu)   // 'OTTO' (OpenType/CFF)
                     || (tag == 0x74746366u);  // 'ttcf' (TrueType Collection)
    if (!valid_sig)
        return false;

    // Atlas size: 1024x1024 is ample for 3 ASCII font sizes with 2x oversampling.
    atlas_width_ = 1024;
    atlas_height_ = 1024;

    // Allocate single-channel bitmap for the atlas
    std::vector<uint8_t> atlas_bitmap(atlas_width_ * atlas_height_, 0);

    // Use stbtt_PackBegin/PackRange/PackEnd for proper rect packing with
    // oversampling. This is the correct API for high-quality text — it handles
    // sub-pixel positioning, padding, and anti-aliasing automatically.
    stbtt_pack_context pack_ctx;
    if (!stbtt_PackBegin(&pack_ctx,
                         atlas_bitmap.data(),
                         static_cast<int>(atlas_width_),
                         static_cast<int>(atlas_height_),
                         0,  // stride_in_bytes (0 = tightly packed)
                         1,  // padding between glyphs
                         nullptr))
    {
        return false;
    }

    // Enable oversampling for crisp sub-pixel positioning.
    // This is the single most important setting for text quality.
    stbtt_PackSetOversampling(&pack_ctx, OVERSAMPLE, OVERSAMPLE);

    // Pack all 3 font sizes. Each gets its own chardata array.
    static constexpr size_t FONT_COUNT = static_cast<size_t>(FontSize::Count);
    stbtt_packedchar chardata[FONT_COUNT][NUM_CHARS];

    stbtt_pack_range ranges[FONT_COUNT];
    for (size_t si = 0; si < FONT_COUNT; ++si)
    {
        ranges[si].font_size = FONT_PIXEL_SIZES[si];
        ranges[si].first_unicode_codepoint_in_range = FIRST_CHAR;
        ranges[si].num_chars = NUM_CHARS;
        ranges[si].chardata_for_range = chardata[si];
        ranges[si].array_of_unicode_codepoints = nullptr;
    }

    int pack_ok = stbtt_PackFontRanges(&pack_ctx,
                                       font_data,
                                       0,  // font_index
                                       ranges,
                                       static_cast<int>(FONT_COUNT));
    stbtt_PackEnd(&pack_ctx);

    if (!pack_ok)
        return false;

    // Extract font metrics and glyph info from packed chardata.
    // stbtt_PackFontRanges produces sub-pixel-accurate quad coordinates
    // that we store in our GlyphInfo for use at draw time.
    stbtt_fontinfo font_info;
    int offset = stbtt_GetFontOffsetForIndex(font_data, 0);
    if (offset < 0)
        return false;
    if (!stbtt_InitFont(&font_info, font_data, offset))
        return false;

    for (size_t si = 0; si < FONT_COUNT; ++si)
    {
        float pixel_size = FONT_PIXEL_SIZES[si];
        float scale = stbtt_ScaleForPixelHeight(&font_info, pixel_size);

        int ascent_i, descent_i, line_gap_i;
        stbtt_GetFontVMetrics(&font_info, &ascent_i, &descent_i, &line_gap_i);

        fonts_[si].pixel_size = pixel_size;
        fonts_[si].ascent = static_cast<float>(ascent_i) * scale;
        fonts_[si].descent = static_cast<float>(descent_i) * scale;
        fonts_[si].line_height = (static_cast<float>(ascent_i - descent_i + line_gap_i)) * scale;

        for (int ci = 0; ci < NUM_CHARS; ++ci)
        {
            uint32_t cp = static_cast<uint32_t>(FIRST_CHAR + ci);
            const stbtt_packedchar& pc = chardata[si][ci];

            // stbtt_GetPackedQuad gives us the exact screen-space quad
            // with sub-pixel offsets baked in from oversampling.
            stbtt_aligned_quad q;
            float dummy_x = 0.0f, dummy_y = 0.0f;
            stbtt_GetPackedQuad(chardata[si],
                                static_cast<int>(atlas_width_),
                                static_cast<int>(atlas_height_),
                                ci,
                                &dummy_x,
                                &dummy_y,
                                &q,
                                0);  // align_to_integer = 0 for sub-pixel

            GlyphInfo gi{};
            gi.u0 = q.s0;
            gi.v0 = q.t0;
            gi.u1 = q.s1;
            gi.v1 = q.t1;
            // Quad offsets from the cursor position (baseline-relative)
            gi.x_offset = q.x0;
            gi.y_offset = q.y0;
            gi.width = q.x1 - q.x0;
            gi.height = q.y1 - q.y0;
            gi.x_advance = pc.xadvance;

            fonts_[si].glyphs[cp] = gi;
        }
    }

    // Convert single-channel atlas to RGBA (Backend::create_texture expects RGBA).
    // Store coverage in all channels — the fragment shader reads .r as alpha.
    std::vector<uint8_t> atlas_rgba(atlas_width_ * atlas_height_ * 4);
    for (uint32_t i = 0; i < atlas_width_ * atlas_height_; ++i)
    {
        uint8_t a = atlas_bitmap[i];
        atlas_rgba[i * 4 + 0] = 255;  // R — white glyph
        atlas_rgba[i * 4 + 1] = 255;  // G
        atlas_rgba[i * 4 + 2] = 255;  // B
        atlas_rgba[i * 4 + 3] = a;    // A — coverage from rasterizer
    }

    // Upload atlas texture
    atlas_texture_ = backend.create_texture(atlas_width_, atlas_height_, atlas_rgba.data());
    if (!atlas_texture_)
        return false;

    // Create text pipelines
    text_pipeline_ = backend.create_pipeline(PipelineType::Text);
    if (!text_pipeline_)
        return false;
    text_depth_pipeline_ = backend.create_pipeline(PipelineType::TextDepth);
    if (!text_depth_pipeline_)
        return false;

    // Create UBO for screen-space ortho projection
    text_ubo_ = backend.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));
    if (!text_ubo_)
        return false;

    initialized_ = true;
    return true;
}

bool TextRenderer::init_from_file(Backend& backend, const std::string& ttf_path)
{
    std::ifstream file(ttf_path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    auto size = file.tellg();
    if (size <= 0)
        return false;

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file)
        return false;

    return init(backend, data.data(), data.size());
}

void TextRenderer::shutdown(Backend& backend)
{
    if (vertex_buffer_)
    {
        backend.destroy_buffer(vertex_buffer_);
        vertex_buffer_ = {};
        vertex_buffer_capacity_ = 0;
    }
    if (depth_vertex_buffer_)
    {
        backend.destroy_buffer(depth_vertex_buffer_);
        depth_vertex_buffer_ = {};
        depth_vertex_buffer_capacity_ = 0;
    }
    if (atlas_texture_)
    {
        backend.destroy_texture(atlas_texture_);
        atlas_texture_ = {};
    }
    if (text_ubo_)
    {
        backend.destroy_buffer(text_ubo_);
        text_ubo_ = {};
    }
    // Pipeline is destroyed by Backend::shutdown() (tracked in pipelines_ map)
    initialized_ = false;
}

void TextRenderer::append_glyph(std::vector<TextVertex>& target,
                                const GlyphInfo& g,
                                float cursor_x,
                                float cursor_y,
                                float z,
                                uint32_t color,
                                float cos_a,
                                float sin_a,
                                float pivot_x,
                                float pivot_y)
{
    float x0 = cursor_x + g.x_offset;
    float y0 = cursor_y + g.y_offset;
    float x1 = x0 + g.width;
    float y1 = y0 + g.height;

    // Apply rotation around pivot if needed
    auto rotate = [&](float px, float py, float& ox, float& oy)
    {
        float dx = px - pivot_x;
        float dy = py - pivot_y;
        ox = pivot_x + dx * cos_a - dy * sin_a;
        oy = pivot_y + dx * sin_a + dy * cos_a;
    };

    float rx0, ry0, rx1, ry1, rx2, ry2, rx3, ry3;
    if (cos_a != 1.0f || sin_a != 0.0f)
    {
        rotate(x0, y0, rx0, ry0);
        rotate(x1, y0, rx1, ry1);
        rotate(x1, y1, rx2, ry2);
        rotate(x0, y1, rx3, ry3);
    }
    else
    {
        rx0 = x0;
        ry0 = y0;
        rx1 = x1;
        ry1 = y0;
        rx2 = x1;
        ry2 = y1;
        rx3 = x0;
        ry3 = y1;
    }

    // Two triangles: (0,1,2) and (0,2,3)
    target.push_back({rx0, ry0, z, g.u0, g.v0, color});
    target.push_back({rx1, ry1, z, g.u1, g.v0, color});
    target.push_back({rx2, ry2, z, g.u1, g.v1, color});

    target.push_back({rx0, ry0, z, g.u0, g.v0, color});
    target.push_back({rx2, ry2, z, g.u1, g.v1, color});
    target.push_back({rx3, ry3, z, g.u0, g.v1, color});
}

TextRenderer::TextExtent TextRenderer::measure_text(const std::string& text, FontSize size) const
{
    const auto& fd = font(size);
    float width = 0.0f;

    for (char c : text)
    {
        uint32_t cp = static_cast<uint32_t>(static_cast<uint8_t>(c));
        auto it = fd.glyphs.find(cp);
        if (it != fd.glyphs.end())
        {
            width += it->second.x_advance;
        }
    }

    return {width, fd.line_height};
}

void TextRenderer::draw_text(const std::string& text,
                             float x,
                             float y,
                             FontSize size,
                             uint32_t color_rgba,
                             TextAlign align,
                             TextVAlign valign)
{
    if (!initialized_ || text.empty())
        return;

    const auto& fd = font(size);

    // Compute alignment offset
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    if (align != TextAlign::Left)
    {
        auto ext = measure_text(text, size);
        if (align == TextAlign::Center)
            offset_x = -ext.width * 0.5f;
        else if (align == TextAlign::Right)
            offset_x = -ext.width;
    }

    if (valign == TextVAlign::Middle)
        offset_y = -fd.line_height * 0.5f;
    else if (valign == TextVAlign::Bottom)
        offset_y = -fd.line_height;

    // Cursor positioned at baseline. GetPackedQuad offsets are baseline-relative.
    float cursor_x = x + offset_x;
    float cursor_y = y + offset_y + fd.ascent;

    for (char c : text)
    {
        uint32_t cp = static_cast<uint32_t>(static_cast<uint8_t>(c));
        auto it = fd.glyphs.find(cp);
        if (it == fd.glyphs.end())
            continue;

        const auto& g = it->second;
        if (g.width > 0 && g.height > 0)
        {
            append_glyph(vertices_, g, cursor_x, cursor_y, 0.0f, color_rgba);
        }
        cursor_x += g.x_advance;
    }
}

void TextRenderer::draw_text_depth(const std::string& text,
                                   float x,
                                   float y,
                                   float ndc_depth,
                                   FontSize size,
                                   uint32_t color_rgba,
                                   TextAlign align,
                                   TextVAlign valign)
{
    if (!initialized_ || text.empty())
        return;

    const auto& fd = font(size);

    float offset_x = 0.0f;
    float offset_y = 0.0f;

    if (align != TextAlign::Left)
    {
        auto ext = measure_text(text, size);
        if (align == TextAlign::Center)
            offset_x = -ext.width * 0.5f;
        else if (align == TextAlign::Right)
            offset_x = -ext.width;
    }

    if (valign == TextVAlign::Middle)
        offset_y = -fd.line_height * 0.5f;
    else if (valign == TextVAlign::Bottom)
        offset_y = -fd.line_height;

    float cursor_x = x + offset_x;
    float cursor_y = y + offset_y + fd.ascent;

    for (char c : text)
    {
        uint32_t cp = static_cast<uint32_t>(static_cast<uint8_t>(c));
        auto it = fd.glyphs.find(cp);
        if (it == fd.glyphs.end())
            continue;

        const auto& g = it->second;
        if (g.width > 0 && g.height > 0)
        {
            append_glyph(depth_vertices_, g, cursor_x, cursor_y, ndc_depth, color_rgba);
        }
        cursor_x += g.x_advance;
    }
}

void TextRenderer::draw_text_rotated(const std::string& text,
                                     float x,
                                     float y,
                                     float angle_rad,
                                     FontSize size,
                                     uint32_t color_rgba,
                                     TextAlign align,
                                     TextVAlign valign)
{
    if (!initialized_ || text.empty())
        return;

    const auto& fd = font(size);
    float cos_a = std::cos(angle_rad);
    float sin_a = std::sin(angle_rad);

    // Compute alignment offset (in unrotated text space)
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    if (align != TextAlign::Left)
    {
        auto ext = measure_text(text, size);
        if (align == TextAlign::Center)
            offset_x = -ext.width * 0.5f;
        else if (align == TextAlign::Right)
            offset_x = -ext.width;
    }

    if (valign == TextVAlign::Middle)
        offset_y = -fd.line_height * 0.5f;
    else if (valign == TextVAlign::Bottom)
        offset_y = -fd.line_height;

    float cursor_x = x + offset_x;
    float cursor_y = y + offset_y + fd.ascent;

    for (char c : text)
    {
        uint32_t cp = static_cast<uint32_t>(static_cast<uint8_t>(c));
        auto it = fd.glyphs.find(cp);
        if (it == fd.glyphs.end())
            continue;

        const auto& g = it->second;
        if (g.width > 0 && g.height > 0)
        {
            append_glyph(vertices_, g, cursor_x, cursor_y, 0.0f, color_rgba, cos_a, sin_a, x, y);
        }
        cursor_x += g.x_advance;
    }
}

void TextRenderer::flush_batch(Backend& backend,
                               std::vector<TextVertex>& verts,
                               BufferHandle& vb,
                               size_t& vb_capacity,
                               PipelineHandle pipeline,
                               float screen_width,
                               float screen_height)
{
    if (verts.empty())
        return;

    // Build screen-space orthographic projection:
    // Maps (0,0)-(w,h) to Vulkan clip space [-1,1] x [-1,1]
    // Vulkan Y is top-down, so (0,0) = top-left matches screen coords.
    // Z maps [0,1] -> [0,1] for depth buffer compatibility.
    FrameUBO ubo{};
    std::memset(&ubo, 0, sizeof(ubo));

    // Column-major ortho: [0, screen_width] -> [-1, 1], [0, screen_height] -> [-1, 1]
    ubo.projection[0] = 2.0f / screen_width;
    ubo.projection[5] = 2.0f / screen_height;  // Positive: Y-down in Vulkan
    ubo.projection[10] = 1.0f;                 // Z passthrough: NDC z = vertex z
    ubo.projection[12] = -1.0f;
    ubo.projection[13] = -1.0f;
    ubo.projection[15] = 1.0f;

    // Identity view
    ubo.view[0] = 1.0f;
    ubo.view[5] = 1.0f;
    ubo.view[10] = 1.0f;
    ubo.view[15] = 1.0f;
    // Identity model
    ubo.model[0] = 1.0f;
    ubo.model[5] = 1.0f;
    ubo.model[10] = 1.0f;
    ubo.model[15] = 1.0f;

    ubo.viewport_width = screen_width;
    ubo.viewport_height = screen_height;

    // Upload UBO data (must happen before bind, but bind must happen after pipeline)
    backend.upload_buffer(text_ubo_, &ubo, sizeof(FrameUBO));

    // Upload vertex data
    size_t byte_size = verts.size() * sizeof(TextVertex);

    if (!vb || vb_capacity < byte_size)
    {
        if (vb)
            backend.destroy_buffer(vb);
        size_t alloc = byte_size * 2;  // 2x headroom
        vb = backend.create_buffer(BufferUsage::Vertex, alloc);
        vb_capacity = alloc;
    }
    backend.upload_buffer(vb, verts.data(), byte_size);

    // Bind text pipeline FIRST so current_pipeline_layout_ is text_pipeline_layout_
    // before any descriptor set binds (UBO at set 0, texture at set 1).
    backend.bind_pipeline(pipeline);

    // Reset viewport and scissor to full screen — text coordinates are in
    // screen-pixel space and must not be clipped to the last axes viewport.
    backend.set_viewport(0, 0, screen_width, screen_height);
    backend.set_scissor(
        0, 0, static_cast<uint32_t>(screen_width), static_cast<uint32_t>(screen_height));

    // Bind UBO at set 0 (now uses text_pipeline_layout_)
    backend.bind_buffer(text_ubo_, 0);

    // Bind atlas texture at set 1
    backend.bind_texture(atlas_texture_, 0);

    // Push dummy constants (pipeline layout requires them)
    SeriesPushConstants pc{};
    pc.color[0] = 1.0f;
    pc.color[1] = 1.0f;
    pc.color[2] = 1.0f;
    pc.color[3] = 1.0f;
    backend.push_constants(pc);

    // Bind vertex buffer and draw
    backend.bind_buffer(vb, 0);
    backend.draw(static_cast<uint32_t>(verts.size()));

    verts.clear();
}

void TextRenderer::flush(Backend& backend, float screen_width, float screen_height)
{
    if (!initialized_)
        return;
    flush_batch(backend,
                vertices_,
                vertex_buffer_,
                vertex_buffer_capacity_,
                text_pipeline_,
                screen_width,
                screen_height);
}

void TextRenderer::flush_depth(Backend& backend, float screen_width, float screen_height)
{
    if (!initialized_)
        return;
    flush_batch(backend,
                depth_vertices_,
                depth_vertex_buffer_,
                depth_vertex_buffer_capacity_,
                text_depth_pipeline_,
                screen_width,
                screen_height);
}

}  // namespace spectra
