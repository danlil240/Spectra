#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend.hpp"

namespace spectra
{

// Text alignment for draw_text calls
enum class TextAlign : uint8_t
{
    Left = 0,
    Center,
    Right,
};

enum class TextVAlign : uint8_t
{
    Top = 0,
    Middle,
    Bottom,
};

// Font size presets matching the 3 sizes used for plot text
enum class FontSize : uint8_t
{
    Tick  = 0,   // 13px — tick labels
    Label = 1,   // 14px — axis labels
    Title = 2,   // 18px — plot title
    Count = 3,
};

struct GlyphInfo
{
    float u0, v0, u1, v1;   // atlas texel coordinates (normalised to UV at flush time)
    float x_offset;         // horizontal offset from cursor to glyph left edge
    float y_offset;         // vertical offset from baseline to glyph top edge
    float x_advance;        // horizontal advance after this glyph
    float width;            // glyph bitmap width in pixels
    float height;           // glyph bitmap height in pixels
};

struct TextVertex
{
    float    x, y, z;   // screen position in pixels (z = NDC depth for 3D text, 0 for 2D)
    float    u, v;      // atlas UV
    uint32_t col;       // packed RGBA (R in low byte)
};

class TextRenderer
{
   public:
    TextRenderer();
    ~TextRenderer();

    TextRenderer(const TextRenderer&)            = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Initialize: bake font atlas, create GPU resources.
    // Must be called after Backend::init().
    // font_data: pointer to TTF file data (e.g. Inter-Regular.ttf embedded bytes)
    // font_data_size: size in bytes
    bool init(Backend& backend, const uint8_t* font_data, size_t font_data_size);

    // Convenience: load TTF from file path
    bool init_from_file(Backend& backend, const std::string& ttf_path);

    // Destroy GPU resources
    void shutdown(Backend& backend);

    // Queue text for rendering. Coordinates are in screen pixels.
    // Text is batched and drawn on flush().
    void draw_text(const std::string& text,
                   float              x,
                   float              y,
                   FontSize           size,
                   uint32_t           color_rgba,
                   TextAlign          align  = TextAlign::Left,
                   TextVAlign         valign = TextVAlign::Top);

    // Queue rotated text (angle in radians, rotated around (x,y))
    void draw_text_rotated(const std::string& text,
                           float              x,
                           float              y,
                           float              angle_rad,
                           FontSize           size,
                           uint32_t           color_rgba,
                           TextAlign          align  = TextAlign::Center,
                           TextVAlign         valign = TextVAlign::Middle);

    // Clip subsequently queued unrotated 2D text to a screen-space rectangle
    // (glyph quads are cropped, so text can be partially visible).  Rotated
    // text is not clipped.  Stays active until clear_clip_rect().
    void set_clip_rect(float x0, float y0, float x1, float y1);
    void clear_clip_rect() { clip_enabled_ = false; }

    // Nearest baked preset for a requested pixel size (<= 0 maps to Label).
    static FontSize nearest_font_size(float pixel_size);

    // Measure text dimensions without drawing
    struct TextExtent
    {
        float width;
        float height;
    };
    TextExtent measure_text(const std::string& text, FontSize size) const;

    // Queue text with depth testing (for 3D labels occluded by geometry).
    // ndc_depth: depth value in [0,1] range from the 3D MVP projection.
    void draw_text_depth(const std::string& text,
                         float              x,
                         float              y,
                         float              ndc_depth,
                         FontSize           size,
                         uint32_t           color_rgba,
                         TextAlign          align  = TextAlign::Left,
                         TextVAlign         valign = TextVAlign::Top);

    // Flush all queued text: upload vertex buffer, bind pipeline + texture, draw.
    // Must be called inside an active render pass.
    // screen_width/height: current framebuffer size (for ortho projection).
    void flush(Backend& backend, float screen_width, float screen_height);

    // Flush depth-tested text (3D labels). Call after flush() or separately.
    // Uses TextDepth pipeline with depth test enabled.
    void flush_depth(Backend& backend, float screen_width, float screen_height);

    // Reset flush slot cursors for the current in-flight frame. Call once per
    // render pass before any flush() (e.g. from Renderer::begin_render_pass).
    // flight_index: backend.current_flight_frame() (0 or 1 with double-buffering).
    void begin_frame_recording(uint32_t flight_index);

    // Returns true if init() succeeded
    bool is_initialized() const { return initialized_; }

    // Pipeline handle (for external pipeline type registration)
    PipelineHandle pipeline() const { return text_pipeline_; }

    // Decode one UTF-8 codepoint starting at byte index i (i < text.size()).
    // Writes the codepoint (U+FFFD for malformed input) and returns the number
    // of bytes consumed (always >= 1).
    static size_t decode_utf8(std::string_view text, size_t i, uint32_t& cp);

    // True if the atlas currently contains a glyph for this codepoint at the
    // given size (does not trigger lazy loading; see resolve_glyph()).
    bool has_glyph(uint32_t cp, FontSize size = FontSize::Label) const
    {
        return fonts_[static_cast<size_t>(size)].glyphs.count(cp) != 0;
    }

    // Where a codepoint's glyph comes from once resolved.
    enum class GlyphSource : uint8_t
    {
        Primary,    // the font passed to init() (Inter)
        Fallback,   // a system fallback font (symbols / CJK), added on demand
        Missing,    // no font has it: drawn as a hollow "tofu" box
        Ignored,    // control / default-ignorable character: not drawn
    };

    // Resolve (and lazily rasterise into the atlas) the glyph for cp.
    // Glyphs not in the pre-baked set are taken from the primary font if it has
    // them, otherwise from the first fallback font that does (see
    // font_fallback.hpp), otherwise a tofu box is used.  New glyphs are uploaded
    // on the next flush().  Not thread-safe: call from the render thread only.
    GlyphSource resolve_glyph(uint32_t cp, FontSize size = FontSize::Label) const;

    // Override the fallback font files (in priority order).  An empty list
    // disables fallback fonts.  Without a call, system fonts are discovered on
    // the first glyph the primary font lacks (discover_fallback_fonts()).
    void set_fallback_font_paths(std::vector<std::string> paths);

    // Current atlas dimensions in texels (the atlas grows in height on demand).
    uint32_t atlas_width() const { return atlas_width_; }
    uint32_t atlas_height() const { return atlas_height_; }

   private:
    // Per-font-size glyph data
    struct FontData
    {
        float                                   pixel_size  = 0.0f;
        float                                   ascent      = 0.0f;
        float                                   descent     = 0.0f;
        float                                   line_height = 0.0f;
        float                                   em_pixels   = 0.0f;   // primary font em size
        std::unordered_map<uint32_t, GlyphInfo> glyphs;               // codepoint -> glyph
        std::unordered_set<uint32_t>            fallback_cps;         // glyphs from fallback fonts
        std::unordered_set<uint32_t>            missing_cps;          // resolved to tofu / ignored
        GlyphInfo                               tofu{};               // hollow missing-glyph box
        bool                                    has_tofu = false;
    };

    // Lazily-populated glyph cache: measure_text() is const but may rasterise
    // new glyphs, so the cache and the CPU-side atlas are mutable.
    mutable FontData fonts_[static_cast<size_t>(FontSize::Count)];

    // Glyph lookup: cached, lazily rasterised, or the tofu box.  Returns
    // nullptr only for characters that are not drawn at all (controls).
    const GlyphInfo* glyph_for(size_t size_index, uint32_t cp) const;
    bool             rasterize_glyph(size_t         size_index,
                                     uint32_t       cp,
                                     const uint8_t* font_data,
                                     float          pack_size) const;
    bool             add_tofu_glyph(size_t size_index);
    bool             alloc_atlas_rect(int w, int h, int& out_x, int& out_y) const;
    bool             upload_atlas(Backend& backend);
    void             prepare_atlas(Backend& backend);

    // Primary font bytes, parsed font info and fallback chain (pimpl keeps
    // stb_truetype out of this header).
    struct GlyphSources;
    std::unique_ptr<GlyphSources> sources_;

    // Atlas: CPU copy (single channel) plus GPU texture.  Glyphs are added
    // with a simple shelf packer below the pre-baked region; the atlas grows
    // in height (up to MAX_ATLAS_HEIGHT) when full.
    static constexpr uint32_t    MAX_ATLAS_HEIGHT = 4096;
    TextureHandle                atlas_texture_;
    uint32_t                     atlas_tex_width_  = 0;   // size of atlas_texture_
    uint32_t                     atlas_tex_height_ = 0;
    mutable uint32_t             atlas_width_      = 0;
    mutable uint32_t             atlas_height_     = 0;
    mutable std::vector<uint8_t> atlas_bitmap_;
    mutable int                  shelf_x_     = 0;
    mutable int                  shelf_y_     = 0;
    mutable int                  shelf_h_     = 0;
    mutable bool                 atlas_dirty_ = false;

    // Replaced atlas textures still referenced by in-flight frames.
    struct RetiredTexture
    {
        TextureHandle handle;
        uint64_t      retired_at_frame = 0;
    };
    std::vector<RetiredTexture> retired_textures_;
    uint64_t                    frame_serial_ = 0;

    // Text pipelines
    PipelineHandle text_pipeline_;
    PipelineHandle text_depth_pipeline_;   // depth-tested variant for 3D labels

    // Vertex batches
    std::vector<TextVertex> vertices_;         // 2D text (no depth test)
    std::vector<TextVertex> depth_vertices_;   // 3D text (depth-tested)

    // Vertex buffers: partitioned by swapchain flight frame, then by flush index.
    // Split-pane rendering flushes text once per pane (×2 for depth + 2D) within
    // a single command buffer; each flush needs its own slot so uploads do not
    // overwrite draws still in the same submission.  Slots are NOT reused until
    // the matching flight frame completes (see begin_frame_recording).
    static constexpr uint32_t MAX_TEXT_FLUSHES_PER_FRAME = 20;   // 10 panes × 2 flushes
    static constexpr uint32_t TEXT_FLIGHT_FRAMES         = 2;
    static constexpr uint32_t TEXT_BUFFER_SLOTS = MAX_TEXT_FLUSHES_PER_FRAME * TEXT_FLIGHT_FRAMES;
    BufferHandle              vertex_buffer_[TEXT_BUFFER_SLOTS];
    size_t                    vertex_buffer_capacity_[TEXT_BUFFER_SLOTS] = {};
    BufferHandle              depth_vertex_buffer_[TEXT_BUFFER_SLOTS];
    size_t                    depth_vertex_buffer_capacity_[TEXT_BUFFER_SLOTS] = {};
    uint32_t                  flush_slot_base_ = 0;   // flight_index * MAX_TEXT_FLUSHES_PER_FRAME
    uint32_t                  flush_cursor_    = 0;   // per-frame flush count (2D text)
    uint32_t                  depth_flush_cursor_ = 0;   // per-frame flush count (3D text)

    // UBO for screen-space ortho projection
    BufferHandle text_ubo_;

    bool initialized_ = false;

    // Optional clip rectangle for draw_text() (screen pixels).
    bool  clip_enabled_ = false;
    float clip_x0_      = 0.0f;
    float clip_y0_      = 0.0f;
    float clip_x1_      = 0.0f;
    float clip_y1_      = 0.0f;

    // Helper: append quad vertices for a single glyph
    void append_glyph(std::vector<TextVertex>& target,
                      const GlyphInfo&         g,
                      float                    cursor_x,
                      float                    cursor_y,
                      float                    z,
                      uint32_t                 color,
                      float                    cos_a   = 1.0f,
                      float                    sin_a   = 0.0f,
                      float                    pivot_x = 0.0f,
                      float                    pivot_y = 0.0f);

    // Internal: flush a vertex batch with a given pipeline
    void flush_batch(Backend&                 backend,
                     std::vector<TextVertex>& verts,
                     BufferHandle&            vb,
                     size_t&                  vb_capacity,
                     PipelineHandle           pipeline,
                     float                    screen_width,
                     float                    screen_height);

    // Helper: get font data for a size
    const FontData& font(FontSize s) const { return fonts_[static_cast<size_t>(s)]; }
};

}   // namespace spectra
