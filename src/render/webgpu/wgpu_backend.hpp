#pragma once

// WebGPU backend for Spectra — implements the Backend interface using the
// WebGPU C API (Dawn native or Emscripten/wasm browser target).
//
// Compile-time gated behind SPECTRA_USE_WEBGPU.  When that macro is not
// defined this header is a no-op so the rest of the codebase can safely
// include it.

#ifdef SPECTRA_USE_WEBGPU

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../backend.hpp"

// Forward-declare opaque WebGPU handle types so callers do not have to pull in
// the full webgpu.h header.
struct WGPUInstanceImpl;
struct WGPUAdapterImpl;
struct WGPUDeviceImpl;
struct WGPUQueueImpl;
struct WGPUSurfaceImpl;
struct WGPUSwapChainImpl;
struct WGPUTextureImpl;
struct WGPUTextureViewImpl;
struct WGPUBufferImpl;
struct WGPURenderPipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUBindGroupLayoutImpl;
struct WGPUPipelineLayoutImpl;
struct WGPUShaderModuleImpl;
struct WGPUCommandEncoderImpl;
struct WGPURenderPassEncoderImpl;
struct WGPUSamplerImpl;

using WGPUInstance           = WGPUInstanceImpl*;
using WGPUAdapter            = WGPUAdapterImpl*;
using WGPUDevice             = WGPUDeviceImpl*;
using WGPUQueue              = WGPUQueueImpl*;
using WGPUSurface            = WGPUSurfaceImpl*;
using WGPUSwapChain          = WGPUSwapChainImpl*;
using WGPUTexture            = WGPUTextureImpl*;
using WGPUTextureView        = WGPUTextureViewImpl*;
using WGPUBuffer             = WGPUBufferImpl*;
using WGPURenderPipeline     = WGPURenderPipelineImpl*;
using WGPUBindGroup          = WGPUBindGroupImpl*;
using WGPUBindGroupLayout    = WGPUBindGroupLayoutImpl*;
using WGPUPipelineLayout     = WGPUPipelineLayoutImpl*;
using WGPUShaderModule       = WGPUShaderModuleImpl*;
using WGPUCommandEncoder     = WGPUCommandEncoderImpl*;
using WGPURenderPassEncoder  = WGPURenderPassEncoderImpl*;
using WGPUSampler            = WGPUSamplerImpl*;

namespace spectra
{

// ─── WebGPUBackend ────────────────────────────────────────────────────────────
//
// Implements the Backend interface using the WebGPU C API.  Supports both
// native builds (via Dawn) and Emscripten/wasm browser targets.
//
// Key architectural differences from VulkanBackend:
//  • No push constants — SeriesPushConstants are emulated via a small uniform
//    buffer (series_pc_buffer_) written with wgpuQueueWriteBuffer before draw.
//  • Three pipeline layout families:
//     - pl_ssbo_   : group 0 (FrameUBO + SeriesPC) + group 1 (storage buffer)
//     - pl_vertex_ : group 0 only  (vertex-attribute shaders: grid, stat_fill)
//     - pl_texture_: group 0 + group 1 (texture + sampler)  (text shaders)
//  • Bind groups are created lazily per buffer/texture and cached by ID.
//  • FrameUBO uses a dynamic-offset bind group; the offset is set when
//    bind_buffer is called for a Uniform buffer.
//  • Browser file I/O: readback_framebuffer triggers an in-memory download
//    when running under Emscripten.

class WebGPUBackend : public Backend
{
   public:
    WebGPUBackend();
    ~WebGPUBackend() override;

    WebGPUBackend(const WebGPUBackend&)            = delete;
    WebGPUBackend& operator=(const WebGPUBackend&) = delete;

    // ── Backend interface ────────────────────────────────────────────────────

    bool init(bool headless) override;
    void shutdown() override;
    void wait_idle() override;

    bool create_surface(void* native_window) override;
    bool create_swapchain(uint32_t width, uint32_t height) override;
    bool recreate_swapchain(uint32_t width, uint32_t height) override;

    bool create_offscreen_framebuffer(uint32_t width, uint32_t height) override;

    PipelineHandle create_pipeline(PipelineType type) override;
    void           destroy_pipeline(PipelineHandle handle) override;

    BufferHandle create_buffer(BufferUsage usage, size_t size_bytes) override;
    void         destroy_buffer(BufferHandle handle) override;
    void upload_buffer(BufferHandle handle, const void* data, size_t size_bytes,
                       size_t offset) override;

    TextureHandle create_texture(uint32_t width, uint32_t height,
                                 const uint8_t* rgba_data) override;
    void          destroy_texture(TextureHandle handle) override;

    bool begin_frame(FrameProfiler* profiler = nullptr) override;
    void end_frame(FrameProfiler* profiler = nullptr) override;

    void begin_render_pass(const Color& clear_color) override;
    void end_render_pass() override;

    void bind_pipeline(PipelineHandle handle) override;
    void bind_buffer(BufferHandle handle, uint32_t binding) override;
    void bind_index_buffer(BufferHandle handle) override;
    void bind_texture(TextureHandle handle, uint32_t binding) override;
    void push_constants(const SeriesPushConstants& pc) override;
    void set_viewport(float x, float y, float width, float height) override;
    void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;
    void set_line_width(float /*width*/) override {}   // no-op (WebGPU has no line-width)
    void draw(uint32_t vertex_count, uint32_t first_vertex) override;
    void draw_instanced(uint32_t vertex_count, uint32_t instance_count,
                        uint32_t first_vertex, uint32_t first_instance) override;
    void draw_indexed(uint32_t index_count, uint32_t first_index,
                      int32_t vertex_offset) override;

    bool readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height) override;

    uint32_t swapchain_width() const override;
    uint32_t swapchain_height() const override;

   private:
    // ── Device init helpers ──────────────────────────────────────────────────
    bool request_adapter();
    bool request_device();
    void create_bind_group_layouts();
    void create_pipeline_layouts();
    bool create_default_resources();

    // ── Pipeline helpers ─────────────────────────────────────────────────────
    WGPURenderPipeline  create_pipeline_for_type(PipelineType type);
    WGPUShaderModule    create_shader_module(const char* wgsl_source) const;
    WGPUPipelineLayout  layout_for_type(PipelineType type) const;

    // ── Bind group cache helpers ─────────────────────────────────────────────
    WGPUBindGroup get_or_create_ssbo_bind_group(uint64_t buffer_id, WGPUBuffer buf,
                                                uint64_t size);
    WGPUBindGroup get_or_create_texture_bind_group(uint64_t tex_id,
                                                   WGPUTextureView view,
                                                   WGPUSampler smp);
    void          invalidate_bind_group_for_buffer(uint64_t buffer_id);
    void          invalidate_bind_group_for_texture(uint64_t tex_id);

    // ── Readback helper (headless / offscreen) ───────────────────────────────
    bool do_readback(uint8_t* out_rgba, uint32_t width, uint32_t height) const;

    // ── Device ──────────────────────────────────────────────────────────────
    WGPUInstance instance_  = nullptr;
    WGPUAdapter  adapter_   = nullptr;
    WGPUDevice   device_    = nullptr;
    WGPUQueue    queue_     = nullptr;

    // ── Surface / swapchain (windowed mode) ─────────────────────────────────
    WGPUSurface   surface_        = nullptr;
    WGPUSwapChain swapchain_      = nullptr;
    uint32_t      sc_width_       = 0;
    uint32_t      sc_height_      = 0;

    // ── Offscreen framebuffer (headless mode) ────────────────────────────────
    WGPUTexture     offscreen_color_   = nullptr;
    WGPUTexture     offscreen_depth_   = nullptr;
    WGPUTextureView offscreen_view_    = nullptr;
    WGPUTextureView offscreen_depth_view_ = nullptr;
    uint32_t        offscreen_width_   = 0;
    uint32_t        offscreen_height_  = 0;

    bool headless_ = false;

    // ── Current frame state ──────────────────────────────────────────────────
    WGPUCommandEncoder    encoder_      = nullptr;
    WGPURenderPassEncoder pass_encoder_ = nullptr;
    WGPUTextureView       current_frame_view_ = nullptr;   // owned by swapchain each frame

    // ── Bind group layouts ───────────────────────────────────────────────────
    WGPUBindGroupLayout bgl_group0_        = nullptr;   // FrameUBO (dyn) + SeriesPC
    WGPUBindGroupLayout bgl_group1_ssbo_   = nullptr;   // storage buffer
    WGPUBindGroupLayout bgl_group1_texture_= nullptr;   // texture + sampler

    // ── Pipeline layouts ────────────────────────────────────────────────────
    WGPUPipelineLayout pl_ssbo_    = nullptr;   // group0 + group1_ssbo
    WGPUPipelineLayout pl_vertex_  = nullptr;   // group0 only
    WGPUPipelineLayout pl_texture_ = nullptr;   // group0 + group1_texture

    // ── Per-frame uniform buffers ────────────────────────────────────────────
    // frame_ubo_ring_: large ring buffer for FrameUBO slots (dynamic offset)
    // series_pc_buf_: small uniform buffer for SeriesPushConstants per draw
    WGPUBuffer frame_ubo_ring_  = nullptr;
    WGPUBuffer series_pc_buf_   = nullptr;
    uint32_t   frame_ubo_offset_ = 0;   // current dynamic offset

    // Bind group referencing both frame UBO and series PC (group 0).
    // Re-created if the underlying buffers change.
    WGPUBindGroup frame_bind_group_ = nullptr;

    // ── Pipeline registry ────────────────────────────────────────────────────
    struct PipelineEntry
    {
        WGPURenderPipeline pipeline = nullptr;
        WGPUPipelineLayout layout   = nullptr;   // weak ref (owned by pl_*_ above)
    };
    std::unordered_map<uint64_t, PipelineEntry> pipelines_;
    uint64_t                                    next_pipeline_id_ = 1;

    // Currently bound pipeline and its layout (needed for bind group calls).
    WGPURenderPipeline current_pipeline_        = nullptr;
    WGPUPipelineLayout current_pipeline_layout_ = nullptr;

    // ── Buffer registry ──────────────────────────────────────────────────────
    struct BufferEntry
    {
        WGPUBuffer  buffer    = nullptr;
        uint64_t    size      = 0;
        BufferUsage usage     = BufferUsage::Vertex;
        // Cached bind group for Storage buffers (group 1).
        WGPUBindGroup bind_group = nullptr;
    };
    std::unordered_map<uint64_t, BufferEntry> buffers_;
    uint64_t                                  next_buffer_id_ = 1;

    // Last Uniform buffer dynamic offset (set by upload_buffer for Uniform).
    uint32_t ubo_bound_offset_ = 0;

    // Currently bound index buffer (for draw_indexed).
    WGPUBuffer current_index_buf_ = nullptr;

    // ── Texture registry ─────────────────────────────────────────────────────
    struct TextureEntry
    {
        WGPUTexture     texture    = nullptr;
        WGPUTextureView view       = nullptr;
        WGPUSampler     sampler    = nullptr;
        WGPUBindGroup   bind_group = nullptr;
    };
    std::unordered_map<uint64_t, TextureEntry> textures_;
    uint64_t                                   next_texture_id_ = 1;
};

}   // namespace spectra

#endif   // SPECTRA_USE_WEBGPU
