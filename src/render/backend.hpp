#pragma once

#include <plotix/color.hpp>
#include <plotix/series.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace plotix {

enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    Staging,
};

enum class PipelineType {
    Line,
    Scatter,
    Grid,
    Heatmap,
    // 3D pipeline types
    Line3D,
    Scatter3D,
    Mesh3D,
    Surface3D,
    Grid3D,
    GridOverlay3D,  // Same as Grid3D but no depth test — for grid lines rendered after series
};

struct BufferHandle {
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct PipelineHandle {
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct TextureHandle {
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct FrameUBO {
    float projection[16] {};  // mat4 — orthographic (2D) or perspective/ortho (3D)
    float view[16]       {};  // mat4 — identity (2D) or camera view matrix (3D)
    float model[16]      {};  // mat4 — identity (2D) or per-series transform (3D)
    float viewport_width  = 0.0f;
    float viewport_height = 0.0f;
    float time            = 0.0f;
    float _pad0           = 0.0f;
    // 3D-specific fields (std140 aligned)
    float camera_pos[3]   {};  // Eye position (for lighting)
    float near_plane      = 0.01f;
    float light_dir[3]    {};  // Directional light (Phase 3)
    float far_plane       = 1000.0f;
};

struct SeriesPushConstants {
    float color[4]     {};
    float line_width   = 2.0f;
    float point_size   = 4.0f;
    float data_offset_x = 0.0f;
    float data_offset_y = 0.0f;
    // Plot style fields (line dash pattern + marker shape)
    uint32_t line_style  = 1;  // 0=None, 1=Solid, 2=Dashed, 3=Dotted, 4=DashDot, 5=DashDotDot
    uint32_t marker_type = 0;  // 0=None, 1=Point, 2=Circle, ... (matches MarkerStyle enum)
    float marker_size    = 6.0f;
    float opacity        = 1.0f;
    // Dash pattern (up to 4 on/off pairs)
    float dash_pattern[8] {};
    float dash_total     = 0.0f;
    int   dash_count     = 0;
    float _pad2[2]       {};   // alignment padding
};

class Backend {
public:
    virtual ~Backend() = default;

    // Lifecycle
    virtual bool init(bool headless) = 0;
    virtual void shutdown() = 0;
    virtual void wait_idle() = 0;

    // Surface / swapchain (windowed mode)
    virtual bool create_surface(void* native_window) = 0;
    virtual bool create_swapchain(uint32_t width, uint32_t height) = 0;
    virtual bool recreate_swapchain(uint32_t width, uint32_t height) = 0;

    // Offscreen framebuffer (headless mode)
    virtual bool create_offscreen_framebuffer(uint32_t width, uint32_t height) = 0;

    // Pipeline management
    virtual PipelineHandle create_pipeline(PipelineType type) = 0;

    // Buffer management
    virtual BufferHandle create_buffer(BufferUsage usage, size_t size_bytes) = 0;
    virtual void destroy_buffer(BufferHandle handle) = 0;
    virtual void upload_buffer(BufferHandle handle, const void* data, size_t size_bytes, size_t offset = 0) = 0;

    // Texture management
    virtual TextureHandle create_texture(uint32_t width, uint32_t height, const uint8_t* rgba_data) = 0;
    virtual void destroy_texture(TextureHandle handle) = 0;

    // Frame rendering
    virtual bool begin_frame() = 0;
    virtual void end_frame() = 0;

    // Render pass
    virtual void begin_render_pass(const Color& clear_color = colors::white) = 0;
    virtual void end_render_pass() = 0;

    // Drawing
    virtual void bind_pipeline(PipelineHandle handle) = 0;
    virtual void bind_buffer(BufferHandle handle, uint32_t binding) = 0;
    virtual void bind_index_buffer(BufferHandle handle) = 0;
    virtual void bind_texture(TextureHandle handle, uint32_t binding) = 0;
    virtual void push_constants(const SeriesPushConstants& pc) = 0;
    virtual void set_viewport(float x, float y, float width, float height) = 0;
    virtual void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;
    virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0) = 0;
    virtual void draw_instanced(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex = 0) = 0;
    virtual void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) = 0;

    // Readback (for offscreen/export)
    virtual bool readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height) = 0;

    // Queries
    virtual uint32_t swapchain_width() const = 0;
    virtual uint32_t swapchain_height() const = 0;
};

} // namespace plotix
