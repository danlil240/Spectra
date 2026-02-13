#pragma once

#include "../backend.hpp"
#include "vk_buffer.hpp"
#include "vk_device.hpp"
#include "vk_pipeline.hpp"
#include "vk_swapchain.hpp"

#include <unordered_map>
#include <vector>

// shader_spirv.hpp included in .cpp

namespace plotix {

class VulkanBackend : public Backend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    // Backend interface
    bool init(bool headless) override;
    void shutdown() override;

    bool create_surface(void* native_window) override;
    bool create_swapchain(uint32_t width, uint32_t height) override;
    bool recreate_swapchain(uint32_t width, uint32_t height) override;

    bool create_offscreen_framebuffer(uint32_t width, uint32_t height) override;

    PipelineHandle create_pipeline(PipelineType type) override;

    BufferHandle create_buffer(BufferUsage usage, size_t size_bytes) override;
    void destroy_buffer(BufferHandle handle) override;
    void upload_buffer(BufferHandle handle, const void* data, size_t size_bytes, size_t offset) override;

    TextureHandle create_texture(uint32_t width, uint32_t height, const uint8_t* rgba_data) override;
    void destroy_texture(TextureHandle handle) override;

    bool begin_frame() override;
    void end_frame() override;

    void begin_render_pass(const Color& clear_color) override;
    void end_render_pass() override;

    void bind_pipeline(PipelineHandle handle) override;
    void bind_buffer(BufferHandle handle, uint32_t binding) override;
    void bind_texture(TextureHandle handle, uint32_t binding) override;
    void push_constants(const SeriesPushConstants& pc) override;
    void set_viewport(float x, float y, float width, float height) override;
    void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;
    void draw(uint32_t vertex_count, uint32_t first_vertex) override;
    void draw_instanced(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex) override;

    bool readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height) override;

    uint32_t swapchain_width() const override;
    uint32_t swapchain_height() const override;

    // Returns true if swapchain needs recreation (set by present OUT_OF_DATE)
    bool swapchain_needs_recreation() const { return swapchain_dirty_; }
    void clear_swapchain_dirty() { swapchain_dirty_ = false; }

    // Returns true if the Vulkan device has been lost (unrecoverable)
    bool is_device_lost() const { return device_lost_; }

    // Vulkan-specific accessors
    VkDevice         device()          const { return ctx_.device; }
    VkPhysicalDevice physical_device() const { return ctx_.physical_device; }
    VkInstance       instance()        const { return ctx_.instance; }
    VkQueue          graphics_queue()  const { return ctx_.graphics_queue; }
    uint32_t         graphics_queue_family() const { return ctx_.queue_families.graphics.value_or(0); }
    VkRenderPass     render_pass()     const;
    VkCommandPool    command_pool()    const { return command_pool_; }
    VkDescriptorPool descriptor_pool() const { return descriptor_pool_; }
    uint32_t         image_count()     const { return headless_ ? 1 : static_cast<uint32_t>(swapchain_.images.size()); }
    uint32_t         min_image_count() const { return headless_ ? 1 : 2; }
    VkCommandBuffer  current_command_buffer() const { return current_cmd_; }

private:
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void create_descriptor_pool();
    VkPipeline create_pipeline_for_type(PipelineType type, VkRenderPass rp);
public:
    void ensure_pipelines();
private:

    vk::DeviceContext     ctx_;
    VkSurfaceKHR          surface_    = VK_NULL_HANDLE;
    vk::SwapchainContext  swapchain_;
    vk::OffscreenContext  offscreen_;
    bool                  headless_       = false;
    bool                  swapchain_dirty_ = false;  // set when present returns OUT_OF_DATE/SUBOPTIMAL
    bool                  device_lost_     = false;  // set on VK_ERROR_DEVICE_LOST — unrecoverable

    VkCommandPool                command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // Sync objects (per frame-in-flight)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence>     in_flight_fences_;
    uint32_t                 current_flight_frame_ = 0;

    // Descriptor management
    VkDescriptorPool      descriptor_pool_        = VK_NULL_HANDLE;
    VkDescriptorSetLayout frame_desc_layout_      = VK_NULL_HANDLE;
    VkDescriptorSetLayout series_desc_layout_     = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_desc_layout_    = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_        = VK_NULL_HANDLE;
    VkPipelineLayout      text_pipeline_layout_   = VK_NULL_HANDLE;

    // Resource tracking
    uint64_t next_buffer_id_   = 1;
    uint64_t next_pipeline_id_ = 1;
    uint64_t next_texture_id_  = 1;

    struct BufferEntry {
        vk::GpuBuffer   gpu_buffer;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        BufferUsage     usage = BufferUsage::Vertex;
    };
    std::unordered_map<uint64_t, BufferEntry> buffers_;
    std::unordered_map<uint64_t, VkPipeline>        pipelines_;
    std::unordered_map<uint64_t, PipelineType>     pipeline_types_;
    std::unordered_map<uint64_t, VkPipelineLayout> pipeline_layouts_;

    VkDescriptorSet frame_desc_set_ = VK_NULL_HANDLE;
    VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout layout);
    void update_ubo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size);
    void update_ssbo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size);

    struct TextureEntry {
        VkImage         image          = VK_NULL_HANDLE;
        VkDeviceMemory  memory         = VK_NULL_HANDLE;
        VkImageView     view           = VK_NULL_HANDLE;
        VkSampler       sampler        = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };
    std::unordered_map<uint64_t, TextureEntry> textures_;

    // Dynamic UBO state (per-draw projection slots)
    VkDeviceSize     ubo_slot_alignment_ = 256;  // minUniformBufferOffsetAlignment
    uint32_t         ubo_next_offset_    = 0;    // next write offset in dynamic UBO
    uint32_t         ubo_bound_offset_   = 0;    // offset of last-written slot (for bind)
    static constexpr uint32_t UBO_MAX_SLOTS = 64;

    // Current frame state
    VkCommandBuffer  current_cmd_ = VK_NULL_HANDLE;
    uint32_t         current_image_index_ = 0;
    VkPipelineLayout current_pipeline_layout_ = VK_NULL_HANDLE;
};

} // namespace plotix
