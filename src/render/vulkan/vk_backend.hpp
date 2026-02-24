#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "../backend.hpp"
#include "vk_buffer.hpp"
#include "vk_device.hpp"
#include "vk_pipeline.hpp"
#include "vk_swapchain.hpp"
#include "window_context.hpp"

// shader_spirv.hpp included in .cpp

namespace spectra
{

class FrameProfiler;

class VulkanBackend : public Backend
{
   public:
    VulkanBackend();
    ~VulkanBackend() override;

    // Backend interface
    bool init(bool headless) override;
    void shutdown() override;
    void wait_idle() override;

    bool create_surface(void* native_window) override;
    bool create_swapchain(uint32_t width, uint32_t height) override;
    bool recreate_swapchain(uint32_t width, uint32_t height) override;

    bool create_offscreen_framebuffer(uint32_t width, uint32_t height) override;

    PipelineHandle create_pipeline(PipelineType type) override;

    BufferHandle create_buffer(BufferUsage usage, size_t size_bytes) override;
    void         destroy_buffer(BufferHandle handle) override;
    void         upload_buffer(BufferHandle handle,
                               const void*  data,
                               size_t       size_bytes,
                               size_t       offset) override;

    TextureHandle create_texture(uint32_t       width,
                                 uint32_t       height,
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
    void set_line_width(float width) override;
    void draw(uint32_t vertex_count, uint32_t first_vertex) override;
    void draw_instanced(uint32_t vertex_count,
                        uint32_t instance_count,
                        uint32_t first_vertex,
                        uint32_t first_instance) override;
    void draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) override;

    bool readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height) override;

    // Request a framebuffer capture during the next end_frame().
    // The copy happens after GPU submit but before present, when the
    // swapchain image content is guaranteed valid.
    void request_framebuffer_capture(uint8_t* out_rgba, uint32_t width, uint32_t height);
    bool has_pending_capture() const { return pending_capture_.buffer != nullptr; }

    uint32_t swapchain_width() const override;
    uint32_t swapchain_height() const override;

    // Returns true if swapchain needs recreation (set by present OUT_OF_DATE)
    bool swapchain_needs_recreation() const { return active_window_->swapchain_dirty; }
    void clear_swapchain_dirty() { active_window_->swapchain_dirty = false; }

    // Multi-window support: set the active window context for frame operations.
    // All begin_frame/end_frame/render pass calls target the active context.
    void           set_active_window(WindowContext* ctx) { active_window_ = ctx; }
    WindowContext* active_window() const { return active_window_; }
    // Transfer ownership of the initial WindowContext to the caller.
    // After this call, initial_window_ is nullptr.
    // The caller MUST set_active_window() before any frame ops.
    std::unique_ptr<WindowContext> release_initial_window()
    {
        active_window_ = nullptr;
        return std::move(initial_window_);
    }

    // Initialize Vulkan resources for a secondary WindowContext.
    // Creates surface from GLFW window, swapchain, command buffers, and sync objects.
    // The WindowContext must have a valid glfw_window pointer set before calling.
    bool init_window_context(WindowContext& wctx, uint32_t width, uint32_t height);

    // Initialize Vulkan resources AND a per-window ImGui context for a secondary
    // WindowContext.  Enforces Section 3F render pass compatibility constraints:
    //   1. set_active_window(wctx) before ImGui init so render_pass() returns
    //      the correct per-window render pass.
    //   2. Assert swapchain image format matches primary (log warning + force
    //      primary format on mismatch).
    //   3. Use per-window ImageCount for ImGui_ImplVulkan_InitInfo.
    //   4. Per-window on_swapchain_recreated() (only that window's ImGui updated).
    // The WindowContext must have a valid glfw_window pointer set before calling.
    // On success, wctx.ui_ctx is NOT set here — caller (WindowManager) owns that.
    // Returns false on failure (Vulkan or ImGui init error).
    bool init_window_context_with_imgui(WindowContext& wctx, uint32_t width, uint32_t height);

    // Destroy all Vulkan resources owned by a WindowContext.
    // Waits for in-flight fences before cleanup.
    void destroy_window_context(WindowContext& wctx);

    // Allocate command buffers for a specific WindowContext from the shared pool.
    void create_command_buffers_for(WindowContext& wctx);

    // Create sync objects (semaphores + fences) for a specific WindowContext.
    void create_sync_objects_for(WindowContext& wctx);

    // Recreate swapchain for a specific WindowContext (saves/restores active_window_).
    bool recreate_swapchain_for(WindowContext& wctx, uint32_t width, uint32_t height);

    // Recreate swapchain for a WindowContext that has a per-window ImGui context.
    // After swapchain recreation, updates the window's ImGui Vulkan backend with
    // the new render pass and image count (Section 3F constraint 4).
    // Falls back to recreate_swapchain_for() if the window has no ImGui context.
    bool recreate_swapchain_for_with_imgui(WindowContext& wctx, uint32_t width, uint32_t height);

    // Returns true if the Vulkan device has been lost (unrecoverable)
    bool is_device_lost() const { return device_lost_; }

    // Vulkan-specific accessors
    VkDevice         device() const { return ctx_.device; }
    VkPhysicalDevice physical_device() const { return ctx_.physical_device; }
    VkInstance       instance() const { return ctx_.instance; }
    VkQueue          graphics_queue() const { return ctx_.graphics_queue; }
    uint32_t      graphics_queue_family() const { return ctx_.queue_families.graphics.value_or(0); }
    VkRenderPass  render_pass() const;
    VkCommandPool command_pool() const { return command_pool_; }
    VkDescriptorPool descriptor_pool() const { return descriptor_pool_; }
    uint32_t         image_count() const
    {
        return headless_ ? 1 : static_cast<uint32_t>(active_window_->swapchain.images.size());
    }
    uint32_t        min_image_count() const { return headless_ ? 1 : 2; }
    VkCommandBuffer current_command_buffer() const { return active_window_->current_cmd; }

   private:
    void       create_command_pool();
    void       create_command_buffers();
    void       create_sync_objects();
    void       create_descriptor_pool();
    VkPipeline create_pipeline_for_type(PipelineType type, VkRenderPass rp);

   public:
    void ensure_pipelines();
    bool is_headless() const { return headless_; }

   private:
    vk::DeviceContext    ctx_;
    vk::OffscreenContext offscreen_;
    bool                 headless_    = false;
    bool                 device_lost_ = false;   // set on VK_ERROR_DEVICE_LOST — unrecoverable

    // Initial window context (heap-allocated for uniform ownership).
    // All per-window resources (surface, swapchain, command buffers, sync
    // objects) live here.  active_window_ points to the context that
    // begin_frame/end_frame currently target.
    std::unique_ptr<WindowContext> initial_window_;
    WindowContext*                 active_window_ = nullptr;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Descriptor management
    VkDescriptorPool      descriptor_pool_      = VK_NULL_HANDLE;
    VkDescriptorSetLayout frame_desc_layout_    = VK_NULL_HANDLE;
    VkDescriptorSetLayout series_desc_layout_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_desc_layout_  = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_      = VK_NULL_HANDLE;
    VkPipelineLayout      text_pipeline_layout_ = VK_NULL_HANDLE;

    // Resource tracking
    uint64_t next_buffer_id_   = 1;
    uint64_t next_pipeline_id_ = 1;
    uint64_t next_texture_id_  = 1;

    struct BufferEntry
    {
        vk::GpuBuffer   gpu_buffer;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        BufferUsage     usage          = BufferUsage::Vertex;
    };
    std::unordered_map<uint64_t, BufferEntry>      buffers_;
    std::unordered_map<uint64_t, VkPipeline>       pipelines_;
    std::unordered_map<uint64_t, PipelineType>     pipeline_types_;
    std::unordered_map<uint64_t, VkPipelineLayout> pipeline_layouts_;

    // Deferred buffer+descriptor deletion.  Each entry is stamped with the
    // frame counter at destruction time.  At the start of each frame, after
    // the fence wait, we flush entries that are old enough (>= flight_count
    // frames ago) so that every in-flight command buffer has completed.
    struct DeferredBufferFree
    {
        BufferEntry entry;
        uint64_t    frame_destroyed;   // value of frame_counter_ when queued
    };
    std::vector<DeferredBufferFree> pending_buffer_frees_;
    uint64_t                        frame_counter_ = 0;
    uint32_t                        flight_count_  = 2;   // updated from in_flight_fences.size()
    void                            flush_pending_buffer_frees(bool force_all = false);

   public:
    // Call once per application tick (NOT per window) to advance the
    // deferred-deletion frame counter and flush old entries.
    void advance_deferred_deletion();

   private:
    VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout layout);
    void            update_ubo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size);
    void            update_ssbo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size);

    struct TextureEntry
    {
        VkImage         image          = VK_NULL_HANDLE;
        VkDeviceMemory  memory         = VK_NULL_HANDLE;
        VkImageView     view           = VK_NULL_HANDLE;
        VkSampler       sampler        = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };
    std::unordered_map<uint64_t, TextureEntry> textures_;

    // Dynamic UBO state (per-draw projection slots)
    VkDeviceSize              ubo_slot_alignment_ = 256;   // minUniformBufferOffsetAlignment
    uint32_t                  ubo_next_offset_    = 0;     // next write offset in dynamic UBO
    uint32_t                  ubo_bound_offset_   = 0;     // offset of last-written slot (for bind)
    static constexpr uint32_t UBO_MAX_SLOTS       = 64;

    // Pending framebuffer capture (filled by end_frame between submit and present)
    struct PendingCapture
    {
        uint8_t* buffer = nullptr;
        uint32_t width  = 0;
        uint32_t height = 0;
        bool     done   = false;
    };
    PendingCapture pending_capture_;
    bool           do_capture_before_present();

    // Current frame state
    VkPipelineLayout current_pipeline_layout_ = VK_NULL_HANDLE;
};

}   // namespace spectra
