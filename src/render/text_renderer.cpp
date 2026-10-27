#include "text_renderer.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace spectra
{

// Pixel sizes for each FontSize preset — typography hierarchy for premium visualization.
// Tick < Label < Title: clear visual ranking, data-first aesthetic.
static constexpr float FONT_PIXEL_SIZES[] = {
    13.0f,   // Tick  — tick labels (readable at a glance, still below labels)
    14.0f,   // Label — axis labels (readable, secondary)
    18.0f,   // Title — plot title (leads without dwarfing the tick labels)
};

// Oversampling factor: 4x gives the sharpest sub-pixel positioning and
// high-fidelity coverage values. The 1024x1024 atlas has ample room for
// 3 ASCII font sizes at 4x. stb_truetype rasterizes at 4x resolution then
// downsamples, producing razor-sharp edges and precise fractional offsets.
static constexpr unsigned int OVERSAMPLE = 4;

// ASCII range to bake (space through tilde)
static constexpr int FIRST_CHAR = 32;
static constexpr int NUM_CHARS  = 95;   // 32..126 inclusive

// Extra (non-ASCII) codepoints commonly used in plot titles, labels and units.
// Codepoints missing from the font are skipped at bake time.
static std::vector<int> extra_codepoints()
{
    std::vector<int> cps;
    // Latin-1 Supplement (accented letters, degree, plus-minus, micro, ...)
    for (int c = 0x00A1; c <= 0x00FF; ++c)
        cps.push_back(c);
    // Greek capitals and small letters
    for (int c = 0x0391; c <= 0x03A9; ++c)
        if (c != 0x03A2)   // unassigned
            cps.push_back(c);
    for (int c = 0x03B1; c <= 0x03C9; ++c)
        cps.push_back(c);
    // General Punctuation: dashes, quotes, bullet, ellipsis, primes, per-mille
    for (int c :
         {0x2010, 0x2011, 0x2012, 0x2013, 0x2014, 0x2015, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D,
          0x201E, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2032, 0x2033, 0x2039, 0x203A})
        cps.push_back(c);
    // Superscripts / subscripts, letterlike symbols, arrows, math operators
    for (int c :
         {0x2070, 0x2071, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079, 0x207A, 0x207B, 0x2080,
          0x2081, 0x2082, 0x2083, 0x2084, 0x2085, 0x2086, 0x2087, 0x2088, 0x2089, 0x20AC, 0x2103,
          0x2109, 0x2116, 0x2122, 0x2126, 0x212B, 0x2190, 0x2191, 0x2192, 0x2193, 0x2194, 0x2202,
          0x2206, 0x220F, 0x2211, 0x2212, 0x221A, 0x221E, 0x2248, 0x2260, 0x2264, 0x2265})
        cps.push_back(c);
    return cps;
}

size_t TextRenderer::decode_utf8(std::string_view text, size_t i, uint32_t& cp)
{
    const auto b0 = static_cast<uint8_t>(text[i]);
    if (b0 < 0x80)
    {
        cp = b0;
        return 1;
    }

    int      len = 0;
    uint32_t v   = 0;
    if ((b0 & 0xE0) == 0xC0)
    {
        len = 2;
        v   = b0 & 0x1F;
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        len = 3;
        v   = b0 & 0x0F;
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        len = 4;
        v   = b0 & 0x07;
    }
    else
    {
        cp = 0xFFFD;   // stray continuation byte or invalid lead byte
        return 1;
    }

    if (i + static_cast<size_t>(len) > text.size())
    {
        cp = 0xFFFD;
        return 1;
    }
    for (int k = 1; k < len; ++k)
    {
        const auto b = static_cast<uint8_t>(text[i + static_cast<size_t>(k)]);
        if ((b & 0xC0) != 0x80)
        {
            cp = 0xFFFD;
            return 1;
        }
        v = (v << 6) | (b & 0x3F);
    }
    // Reject overlong encodings, surrogates and out-of-range values.
    static constexpr uint32_t MIN_FOR_LEN[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (v < MIN_FOR_LEN[len] || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
    {
        cp = 0xFFFD;
        return static_cast<size_t>(len);
    }
    cp = v;
    return static_cast<size_t>(len);
}

const GlyphInfo* TextRenderer::find_glyph(const FontData& fd, uint32_t cp)
{
    auto it = fd.glyphs.find(cp);
    if (it != fd.glyphs.end())
        return &it->second;
    // Unknown non-ASCII glyph: draw '?' rather than silently dropping the
    // character, so missing coverage is visible.
    if (cp >= 0x80)
    {
        it = fd.glyphs.find(static_cast<uint32_t>('?'));
        if (it != fd.glyphs.end())
            return &it->second;
    }
    return nullptr;
}

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
    bool valid_sig = (tag == 0x00010000u)       // TrueType
                     || (tag == 0x4F54544Fu)    // 'OTTO' (OpenType/CFF)
                     || (tag == 0x74746366u);   // 'ttcf' (TrueType Collection)
    if (!valid_sig)
        return false;

    static constexpr auto FONT_COUNT = static_cast<size_t>(FontSize::Count);

    // Two ranges per font size: contiguous ASCII and a sparse list of extra
    // codepoints (punctuation, Greek, symbols).
    std::vector<int> extras = extra_codepoints();
    const int        n_ext  = static_cast<int>(extras.size());

    std::vector<stbtt_packedchar> ascii_chardata(FONT_COUNT * NUM_CHARS);
    std::vector<stbtt_packedchar> extra_chardata(FONT_COUNT * static_cast<size_t>(n_ext));
    std::vector<uint8_t>          atlas_bitmap;

    // Try increasingly large atlases until everything fits.
    bool pack_ok = false;
    for (uint32_t dim : {1024u, 2048u})
    {
        atlas_width_  = dim;
        atlas_height_ = dim;
        atlas_bitmap.assign(static_cast<size_t>(dim) * dim, 0);

        // Use stbtt_PackBegin/PackRange/PackEnd for proper rect packing with
        // oversampling. This is the correct API for high-quality text — it
        // handles sub-pixel positioning, padding, and anti-aliasing.
        stbtt_pack_context pack_ctx;
        if (!stbtt_PackBegin(&pack_ctx,
                             atlas_bitmap.data(),
                             static_cast<int>(atlas_width_),
                             static_cast<int>(atlas_height_),
                             0,   // stride_in_bytes (0 = tightly packed)
                             1,   // padding between glyphs
                             nullptr))
        {
            return false;
        }

        // Enable oversampling for crisp sub-pixel positioning.
        stbtt_PackSetOversampling(&pack_ctx, OVERSAMPLE, OVERSAMPLE);

        stbtt_pack_range ranges[FONT_COUNT * 2];
        for (size_t si = 0; si < FONT_COUNT; ++si)
        {
            stbtt_pack_range& ra                = ranges[si * 2];
            ra.font_size                        = FONT_PIXEL_SIZES[si];
            ra.first_unicode_codepoint_in_range = FIRST_CHAR;
            ra.num_chars                        = NUM_CHARS;
            ra.chardata_for_range               = &ascii_chardata[si * NUM_CHARS];
            ra.array_of_unicode_codepoints      = nullptr;

            stbtt_pack_range& re                = ranges[si * 2 + 1];
            re.font_size                        = FONT_PIXEL_SIZES[si];
            re.first_unicode_codepoint_in_range = 0;
            re.num_chars                        = n_ext;
            re.chardata_for_range               = &extra_chardata[si * static_cast<size_t>(n_ext)];
            re.array_of_unicode_codepoints      = extras.data();
        }

        pack_ok = stbtt_PackFontRanges(&pack_ctx,
                                       font_data,
                                       0,   // font_index
                                       ranges,
                                       static_cast<int>(FONT_COUNT * 2))
                  != 0;
        stbtt_PackEnd(&pack_ctx);
        if (pack_ok)
            break;
    }

    if (!pack_ok)
        return false;

    // Extract font metrics and glyph info from packed chardata.
    // stbtt_PackFontRanges produces sub-pixel-accurate quad coordinates
    // that we store in our GlyphInfo for use at draw time.
    stbtt_fontinfo font_info;
    int            offset = stbtt_GetFontOffsetForIndex(font_data, 0);
    if (offset < 0)
        return false;
    if (!stbtt_InitFont(&font_info, font_data, offset))
        return false;

    for (size_t si = 0; si < FONT_COUNT; ++si)
    {
        float pixel_size = FONT_PIXEL_SIZES[si];
        float scale      = stbtt_ScaleForPixelHeight(&font_info, pixel_size);

        int ascent_i   = 0;
        int descent_i  = 0;
        int line_gap_i = 0;
        stbtt_GetFontVMetrics(&font_info, &ascent_i, &descent_i, &line_gap_i);

        fonts_[si].pixel_size  = pixel_size;
        fonts_[si].ascent      = static_cast<float>(ascent_i) * scale;
        fonts_[si].descent     = static_cast<float>(descent_i) * scale;
        fonts_[si].line_height = (static_cast<float>(ascent_i - descent_i + line_gap_i)) * scale;

        auto add_glyphs = [&](const stbtt_packedchar* chardata, int count, auto cp_of)
        {
            for (int ci = 0; ci < count; ++ci)
            {
                const uint32_t cp = cp_of(ci);
                // Skip codepoints the font doesn't contain (they would
                // render as the .notdef box); find_glyph() falls back to '?'.
                if (cp > 0x7F && stbtt_FindGlyphIndex(&font_info, static_cast<int>(cp)) == 0)
                    continue;

                const stbtt_packedchar& pc = chardata[ci];

                // stbtt_GetPackedQuad gives us the exact screen-space quad
                // with sub-pixel offsets baked in from oversampling.
                stbtt_aligned_quad q;
                float              dummy_x = 0.0f;
                float              dummy_y = 0.0f;
                stbtt_GetPackedQuad(chardata,
                                    static_cast<int>(atlas_width_),
                                    static_cast<int>(atlas_height_),
                                    ci,
                                    &dummy_x,
                                    &dummy_y,
                                    &q,
                                    0);   // align_to_integer = 0 for sub-pixel

                GlyphInfo gi{};
                gi.u0 = q.s0;
                gi.v0 = q.t0;
                gi.u1 = q.s1;
                gi.v1 = q.t1;
                // Quad offsets from the cursor position (baseline-relative)
                gi.x_offset  = q.x0;
                gi.y_offset  = q.y0;
                gi.width     = q.x1 - q.x0;
                gi.height    = q.y1 - q.y0;
                gi.x_advance = pc.xadvance;

                fonts_[si].glyphs[cp] = gi;
            }
        };

        add_glyphs(&ascii_chardata[si * NUM_CHARS],
                   NUM_CHARS,
                   [](int ci) { return static_cast<uint32_t>(FIRST_CHAR + ci); });
        add_glyphs(&extra_chardata[si * static_cast<size_t>(n_ext)],
                   n_ext,
                   [&](int ci) { return static_cast<uint32_t>(extras[static_cast<size_t>(ci)]); });
    }

    // Convert single-channel atlas to RGBA (Backend::create_texture expects RGBA).
    // Store coverage in all channels — the fragment shader reads .r as alpha.
    std::vector<uint8_t> atlas_rgba(atlas_width_ * atlas_height_ * 4);
    for (uint32_t i = 0; i < atlas_width_ * atlas_height_; ++i)
    {
        uint8_t a             = atlas_bitmap[i];
        atlas_rgba[i * 4 + 0] = 255;   // R — white glyph
        atlas_rgba[i * 4 + 1] = 255;   // G
        atlas_rgba[i * 4 + 2] = 255;   // B
        atlas_rgba[i * 4 + 3] = a;     // A — coverage from rasterizer
    }

    // Release anything left over from a previous (failed) init attempt so a
    // retry (e.g. Renderer::init's font-file fallbacks) doesn't leak the atlas
    // texture or UBO by overwriting their handles.
    if (atlas_texture_ || text_ubo_)
        shutdown(backend);

    // Upload atlas texture
    atlas_texture_ = backend.create_texture(atlas_width_, atlas_height_, atlas_rgba.data());
    if (!atlas_texture_)
        return false;

    // Create text pipelines.  On any failure below, release what was already
    // created so a failed init leaves no GPU resources behind.
    text_pipeline_ = backend.create_pipeline(PipelineType::Text);
    if (!text_pipeline_)
    {
        shutdown(backend);
        return false;
    }
    text_depth_pipeline_ = backend.create_pipeline(PipelineType::TextDepth);
    if (!text_depth_pipeline_)
    {
        shutdown(backend);
        return false;
    }

    // Create UBO for screen-space ortho projection
    text_ubo_ = backend.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));
    if (!text_ubo_)
    {
        shutdown(backend);
        return false;
    }

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
    for (uint32_t i = 0; i < TEXT_BUFFER_SLOTS; ++i)
    {
        if (vertex_buffer_[i])
        {
            backend.destroy_buffer(vertex_buffer_[i]);
            vertex_buffer_[i]          = {};
            vertex_buffer_capacity_[i] = 0;
        }
        if (depth_vertex_buffer_[i])
        {
            backend.destroy_buffer(depth_vertex_buffer_[i]);
            depth_vertex_buffer_[i]          = {};
            depth_vertex_buffer_capacity_[i] = 0;
        }
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
                                const GlyphInfo&         g,
                                float                    cursor_x,
                                float                    cursor_y,
                                float                    z,
                                uint32_t                 color,
                                float                    cos_a,
                                float                    sin_a,
                                float                    pivot_x,
                                float                    pivot_y)
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
        ox       = pivot_x + dx * cos_a - dy * sin_a;
        oy       = pivot_y + dx * sin_a + dy * cos_a;
    };

    float rx0 = NAN;
    float ry0 = NAN;
    float rx1 = NAN;
    float ry1 = NAN;
    float rx2 = NAN;
    float ry2 = NAN;
    float rx3 = NAN;
    float ry3 = NAN;
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
    const auto& fd    = font(size);
    float       width = 0.0f;

    for (size_t i = 0; i < text.size();)
    {
        uint32_t cp = 0;
        i += decode_utf8(text, i, cp);
        if (const GlyphInfo* g = find_glyph(fd, cp))
            width += g->x_advance;
    }

    return {width, fd.line_height};
}

void TextRenderer::draw_text(const std::string& text,
                             float              x,
                             float              y,
                             FontSize           size,
                             uint32_t           color_rgba,
                             TextAlign          align,
                             TextVAlign         valign)
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

    for (size_t i = 0; i < text.size();)
    {
        uint32_t cp = 0;
        i += decode_utf8(text, i, cp);
        const GlyphInfo* gp = find_glyph(fd, cp);
        if (!gp)
            continue;

        const auto& g = *gp;
        if (g.width > 0 && g.height > 0)
        {
            append_glyph(vertices_, g, cursor_x, cursor_y, 0.0f, color_rgba);
        }
        cursor_x += g.x_advance;
    }
}

void TextRenderer::draw_text_depth(const std::string& text,
                                   float              x,
                                   float              y,
                                   float              ndc_depth,
                                   FontSize           size,
                                   uint32_t           color_rgba,
                                   TextAlign          align,
                                   TextVAlign         valign)
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

    for (size_t i = 0; i < text.size();)
    {
        uint32_t cp = 0;
        i += decode_utf8(text, i, cp);
        const GlyphInfo* gp = find_glyph(fd, cp);
        if (!gp)
            continue;

        const auto& g = *gp;
        if (g.width > 0 && g.height > 0)
        {
            append_glyph(depth_vertices_, g, cursor_x, cursor_y, ndc_depth, color_rgba);
        }
        cursor_x += g.x_advance;
    }
}

void TextRenderer::draw_text_rotated(const std::string& text,
                                     float              x,
                                     float              y,
                                     float              angle_rad,
                                     FontSize           size,
                                     uint32_t           color_rgba,
                                     TextAlign          align,
                                     TextVAlign         valign)
{
    if (!initialized_ || text.empty())
        return;

    const auto& fd    = font(size);
    float       cos_a = std::cos(angle_rad);
    float       sin_a = std::sin(angle_rad);

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

    for (size_t i = 0; i < text.size();)
    {
        uint32_t cp = 0;
        i += decode_utf8(text, i, cp);
        const GlyphInfo* gp = find_glyph(fd, cp);
        if (!gp)
            continue;

        const auto& g = *gp;
        if (g.width > 0 && g.height > 0)
        {
            append_glyph(vertices_, g, cursor_x, cursor_y, 0.0f, color_rgba, cos_a, sin_a, x, y);
        }
        cursor_x += g.x_advance;
    }
}

void TextRenderer::flush_batch(Backend&                 backend,
                               std::vector<TextVertex>& verts,
                               BufferHandle&            vb,
                               size_t&                  vb_capacity,
                               PipelineHandle           pipeline,
                               float                    screen_width,
                               float                    screen_height)
{
    if (verts.empty())
        return;

    // Build screen-space orthographic projection:
    // Maps (0,0)-(w,h) to clip space [-1,1] x [-1,1].
    // Screen coords are Y-down (0=top, h=bottom).
    // Vulkan clip Y is top-down (positive scale), WebGPU is Y-up (negative scale).
    // Z maps [0,1] -> [0,1] for depth buffer compatibility.
    FrameUBO ubo{};   // Value-initializes all members to zero
    float    y_sign = backend.clip_y_down() ? 1.0f : -1.0f;

    // Column-major ortho: [0, screen_width] -> [-1, 1], [0, screen_height] -> [-1, 1]
    ubo.projection[0]  = 2.0f / screen_width;
    ubo.projection[5]  = y_sign * 2.0f / screen_height;
    ubo.projection[10] = 1.0f;   // Z passthrough: NDC z = vertex z
    ubo.projection[12] = -1.0f;
    ubo.projection[13] = y_sign * -1.0f;
    ubo.projection[15] = 1.0f;

    // Identity view
    ubo.view[0]  = 1.0f;
    ubo.view[5]  = 1.0f;
    ubo.view[10] = 1.0f;
    ubo.view[15] = 1.0f;
    // Identity model
    ubo.model[0]  = 1.0f;
    ubo.model[5]  = 1.0f;
    ubo.model[10] = 1.0f;
    ubo.model[15] = 1.0f;

    ubo.viewport_width  = screen_width;
    ubo.viewport_height = screen_height;

    // Upload UBO data (must happen before bind, but bind must happen after pipeline)
    backend.upload_buffer(text_ubo_, &ubo, sizeof(FrameUBO));

    // Upload vertex data
    size_t byte_size = verts.size() * sizeof(TextVertex);

    if (!vb || vb_capacity < byte_size)
    {
        if (vb)
            backend.destroy_buffer(vb);
        size_t alloc = byte_size * 2;   // 2x headroom
        vb           = backend.create_buffer(BufferUsage::Vertex, alloc);
        vb_capacity  = alloc;
    }
    backend.upload_buffer(vb, verts.data(), byte_size);

    // Bind text pipeline FIRST so current_pipeline_layout_ is text_pipeline_layout_
    // before any descriptor set binds (UBO at set 0, texture at set 1).
    backend.bind_pipeline(pipeline);

    // Reset viewport and scissor to full screen — text coordinates are in
    // screen-pixel space and must not be clipped to the last axes viewport.
    backend.set_viewport(0, 0, screen_width, screen_height);
    backend.set_scissor(0,
                        0,
                        static_cast<uint32_t>(screen_width),
                        static_cast<uint32_t>(screen_height));

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

void TextRenderer::begin_frame_recording(uint32_t flight_index)
{
    flush_slot_base_    = (flight_index % TEXT_FLIGHT_FRAMES) * MAX_TEXT_FLUSHES_PER_FRAME;
    flush_cursor_       = 0;
    depth_flush_cursor_ = 0;
}

void TextRenderer::flush(Backend& backend, float screen_width, float screen_height)
{
    if (!initialized_)
        return;
    if (flush_cursor_ >= MAX_TEXT_FLUSHES_PER_FRAME)
    {
        // Out of per-frame slots: drop the queued text rather than letting it
        // carry over into the next frame's first flush (wrong pane / late).
        vertices_.clear();
        return;
    }
    uint32_t slot = flush_slot_base_ + flush_cursor_;
    ++flush_cursor_;
    flush_batch(backend,
                vertices_,
                vertex_buffer_[slot],
                vertex_buffer_capacity_[slot],
                text_pipeline_,
                screen_width,
                screen_height);
}

void TextRenderer::flush_depth(Backend& backend, float screen_width, float screen_height)
{
    if (!initialized_)
        return;
    if (depth_flush_cursor_ >= MAX_TEXT_FLUSHES_PER_FRAME)
    {
        // Out of per-frame slots: drop the queued text rather than letting it
        // carry over into the next frame's first flush (wrong pane / late).
        depth_vertices_.clear();
        return;
    }
    uint32_t slot = flush_slot_base_ + depth_flush_cursor_;
    ++depth_flush_cursor_;
    flush_batch(backend,
                depth_vertices_,
                depth_vertex_buffer_[slot],
                depth_vertex_buffer_capacity_[slot],
                text_depth_pipeline_,
                screen_width,
                screen_height);
}

}   // namespace spectra
