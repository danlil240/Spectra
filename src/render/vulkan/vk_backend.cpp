#include "vk_backend.hpp"

#include "shader_spirv.hpp"

#ifdef PLOTIX_USE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace plotix {

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend() {
    shutdown();
}

bool VulkanBackend::init(bool headless) {
    headless_ = headless;

    try {
#ifdef NDEBUG
        bool enable_validation = false;
#else
        bool enable_validation = true;
#endif
        ctx_.instance = vk::create_instance(enable_validation);

        if (enable_validation) {
            ctx_.debug_messenger = vk::create_debug_messenger(ctx_.instance);
        }

        // For headless, pick device without surface
        ctx_.physical_device = vk::pick_physical_device(ctx_.instance, surface_);
        ctx_.queue_families  = vk::find_queue_families(ctx_.physical_device, surface_);

        // When not headless, force swapchain extension even though surface doesn't exist yet
        // (surface is created later by GLFW adapter, but device needs the extension at creation time)
        if (!headless_) {
            ctx_.queue_families.present = ctx_.queue_families.graphics;
        }
        ctx_.device = vk::create_logical_device(ctx_.physical_device, ctx_.queue_families, enable_validation);

        vkGetDeviceQueue(ctx_.device, ctx_.queue_families.graphics.value(), 0, &ctx_.graphics_queue);
        if (ctx_.queue_families.has_present()) {
            vkGetDeviceQueue(ctx_.device, ctx_.queue_families.present.value(), 0, &ctx_.present_queue);
        }

        vkGetPhysicalDeviceProperties(ctx_.physical_device, &ctx_.properties);
        vkGetPhysicalDeviceMemoryProperties(ctx_.physical_device, &ctx_.memory_properties);

        create_command_pool();
        create_descriptor_pool();

        // Create descriptor set layouts and pipeline layouts
        frame_desc_layout_  = vk::create_frame_descriptor_layout(ctx_.device);
        series_desc_layout_ = vk::create_series_descriptor_layout(ctx_.device);
        pipeline_layout_    = vk::create_pipeline_layout(ctx_.device, {frame_desc_layout_, series_desc_layout_});

        // Texture descriptor set layout (combined image sampler at binding 0)
        {
            VkDescriptorSetLayoutBinding sampler_binding {};
            sampler_binding.binding         = 0;
            sampler_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_binding.descriptorCount = 1;
            sampler_binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layout_info {};
            layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 1;
            layout_info.pBindings    = &sampler_binding;

            if (vkCreateDescriptorSetLayout(ctx_.device, &layout_info, nullptr, &texture_desc_layout_) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create texture descriptor set layout");
            }
        }

        // Text pipeline layout: frame UBO (set 0) + texture sampler (set 1)
        text_pipeline_layout_ = vk::create_pipeline_layout(ctx_.device, {frame_desc_layout_, texture_desc_layout_});

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Plotix] Backend init failed: " << e.what() << "\n";
        return false;
    }
}

void VulkanBackend::shutdown() {
    if (ctx_.device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(ctx_.device);

    // Destroy pipelines
    for (auto& [id, pipeline] : pipelines_) {
        vkDestroyPipeline(ctx_.device, pipeline, nullptr);
    }
    pipelines_.clear();

    // Destroy buffers (GpuBuffer destructors handle Vulkan cleanup)
    buffers_.clear();

    // Destroy textures
    for (auto& [id, tex] : textures_) {
        if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(ctx_.device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE)    vkDestroyImageView(ctx_.device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE)   vkDestroyImage(ctx_.device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)  vkFreeMemory(ctx_.device, tex.memory, nullptr);
    }
    textures_.clear();

    // Destroy sync objects
    for (auto sem : image_available_semaphores_) vkDestroySemaphore(ctx_.device, sem, nullptr);
    for (auto sem : render_finished_semaphores_) vkDestroySemaphore(ctx_.device, sem, nullptr);
    for (auto fence : in_flight_fences_)         vkDestroyFence(ctx_.device, fence, nullptr);
    image_available_semaphores_.clear();
    render_finished_semaphores_.clear();
    in_flight_fences_.clear();

    // Destroy layouts
    if (text_pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device, text_pipeline_layout_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
    if (texture_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, texture_desc_layout_, nullptr);
    if (series_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, series_desc_layout_, nullptr);
    if (frame_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, frame_desc_layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);

    if (command_pool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx_.device, command_pool_, nullptr);

    vk::destroy_offscreen(ctx_.device, offscreen_);
    vk::destroy_swapchain(ctx_.device, swapchain_);

    if (surface_ != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(ctx_.instance, surface_, nullptr);

    vkDestroyDevice(ctx_.device, nullptr);

    if (ctx_.debug_messenger != VK_NULL_HANDLE)
        vk::destroy_debug_messenger(ctx_.instance, ctx_.debug_messenger);

    vkDestroyInstance(ctx_.instance, nullptr);

    ctx_ = {};
    surface_ = VK_NULL_HANDLE;
}

bool VulkanBackend::create_surface(void* native_window) {
    if (!native_window) return false;

#ifdef PLOTIX_USE_GLFW
    auto* glfw_window = static_cast<GLFWwindow*>(native_window);
    VkResult result = glfwCreateWindowSurface(ctx_.instance, glfw_window, nullptr, &surface_);
    if (result != VK_SUCCESS) {
        std::cerr << "[Plotix] Failed to create Vulkan surface (VkResult=" << result << ")\n";
        return false;
    }

    // Now that we have a surface, re-query present support
    ctx_.queue_families = vk::find_queue_families(ctx_.physical_device, surface_);
    if (ctx_.queue_families.has_present()) {
        vkGetDeviceQueue(ctx_.device, ctx_.queue_families.present.value(), 0, &ctx_.present_queue);
    }
    // Always ensure present_queue is valid — fallback to graphics queue
    if (ctx_.present_queue == VK_NULL_HANDLE) {
        ctx_.present_queue = ctx_.graphics_queue;
    }

    return true;
#else
    (void)native_window;
    return false;
#endif
}

bool VulkanBackend::create_swapchain(uint32_t width, uint32_t height) {
    if (surface_ == VK_NULL_HANDLE) return false;

    try {
        swapchain_ = vk::create_swapchain(
            ctx_.device, ctx_.physical_device, surface_,
            width, height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value())
        );
        create_command_buffers();
        create_sync_objects();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Plotix] Swapchain creation failed: " << e.what() << "\n";
        return false;
    }
}

bool VulkanBackend::recreate_swapchain(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(ctx_.device);

    auto old = swapchain_.swapchain;
    vk::destroy_swapchain(ctx_.device, swapchain_);

    try {
        swapchain_ = vk::create_swapchain(
            ctx_.device, ctx_.physical_device, surface_,
            width, height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
            old
        );
        if (old != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(ctx_.device, old, nullptr);
        }
        create_command_buffers();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Plotix] Swapchain recreation failed: " << e.what() << "\n";
        return false;
    }
}

bool VulkanBackend::create_offscreen_framebuffer(uint32_t width, uint32_t height) {
    try {
        vk::destroy_offscreen(ctx_.device, offscreen_);
        offscreen_ = vk::create_offscreen_framebuffer(ctx_.device, ctx_.physical_device, width, height);
        create_command_buffers();
        create_sync_objects();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Plotix] Offscreen framebuffer creation failed: " << e.what() << "\n";
        return false;
    }
}

PipelineHandle VulkanBackend::create_pipeline(PipelineType type) {
    PipelineHandle h;
    h.id = next_pipeline_id_++;

    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE) {
        // Render pass not yet available — store placeholder, will be created lazily
        pipelines_[h.id] = VK_NULL_HANDLE;
        pipeline_types_[h.id] = type;
        return h;
    }

    pipelines_[h.id] = create_pipeline_for_type(type, rp);
    return h;
}

VkPipeline VulkanBackend::create_pipeline_for_type(PipelineType type, VkRenderPass rp) {
    vk::PipelineConfig cfg;
    cfg.render_pass     = rp;
    cfg.pipeline_layout = pipeline_layout_;
    cfg.enable_blending = true;
    cfg.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    switch (type) {
        case PipelineType::Line:
            cfg.vert_spirv      = shaders::line_vert;
            cfg.vert_spirv_size = shaders::line_vert_size;
            cfg.frag_spirv      = shaders::line_frag;
            cfg.frag_spirv_size = shaders::line_frag_size;
            break;
        case PipelineType::Scatter:
            cfg.vert_spirv      = shaders::scatter_vert;
            cfg.vert_spirv_size = shaders::scatter_vert_size;
            cfg.frag_spirv      = shaders::scatter_frag;
            cfg.frag_spirv_size = shaders::scatter_frag_size;
            break;
        case PipelineType::Grid:
            cfg.vert_spirv      = shaders::grid_vert;
            cfg.vert_spirv_size = shaders::grid_vert_size;
            cfg.frag_spirv      = shaders::grid_frag;
            cfg.frag_spirv_size = shaders::grid_frag_size;
            cfg.topology        = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            // Grid uses vec2 vertex attribute for line endpoints
            cfg.vertex_bindings.push_back({0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
            break;
        case PipelineType::Text:
            cfg.vert_spirv      = shaders::text_vert;
            cfg.vert_spirv_size = shaders::text_vert_size;
            cfg.frag_spirv      = shaders::text_frag;
            cfg.frag_spirv_size = shaders::text_frag_size;
            cfg.pipeline_layout = text_pipeline_layout_;
            // Text uses vec2 position + vec2 UV per vertex
            cfg.vertex_bindings.push_back({0, sizeof(float) * 4, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
            cfg.vertex_attributes.push_back({1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2});
            break;
        case PipelineType::Heatmap:
            return VK_NULL_HANDLE; // Not yet implemented
    }

    try {
        return vk::create_graphics_pipeline(ctx_.device, cfg);
    } catch (const std::exception& e) {
        std::cerr << "[Plotix] Pipeline creation failed: " << e.what() << "\n";
        return VK_NULL_HANDLE;
    }
}

void VulkanBackend::ensure_pipelines() {
    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE) return;

    for (auto& [id, pipeline] : pipelines_) {
        if (pipeline == VK_NULL_HANDLE) {
            auto it = pipeline_types_.find(id);
            if (it != pipeline_types_.end()) {
                pipeline = create_pipeline_for_type(it->second, rp);
            }
        }
    }
}

BufferHandle VulkanBackend::create_buffer(BufferUsage usage, size_t size_bytes) {
    VkBufferUsageFlags vk_usage = 0;
    VkMemoryPropertyFlags mem_props = 0;

    switch (usage) {
        case BufferUsage::Vertex:
            vk_usage  = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mem_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case BufferUsage::Index:
            vk_usage  = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mem_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case BufferUsage::Uniform:
            vk_usage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Storage:
            vk_usage  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Staging:
            vk_usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
    }

    auto buf = vk::GpuBuffer::create(ctx_.device, ctx_.physical_device,
                                      static_cast<VkDeviceSize>(size_bytes),
                                      vk_usage, mem_props);

    BufferHandle h;
    h.id = next_buffer_id_++;

    BufferEntry entry;
    entry.gpu_buffer = std::move(buf);
    entry.usage = usage;

    // Allocate descriptor set for UBO or SSBO buffers
    if (usage == BufferUsage::Uniform) {
        entry.descriptor_set = allocate_descriptor_set(frame_desc_layout_);
        if (entry.descriptor_set != VK_NULL_HANDLE) {
            update_ubo_descriptor(entry.descriptor_set, entry.gpu_buffer.buffer(),
                                  static_cast<VkDeviceSize>(size_bytes));
        }
    } else if (usage == BufferUsage::Storage) {
        entry.descriptor_set = allocate_descriptor_set(series_desc_layout_);
        if (entry.descriptor_set != VK_NULL_HANDLE) {
            update_ssbo_descriptor(entry.descriptor_set, entry.gpu_buffer.buffer(),
                                   static_cast<VkDeviceSize>(size_bytes));
        }
    }

    buffers_.emplace(h.id, std::move(entry));
    return h;
}

void VulkanBackend::destroy_buffer(BufferHandle handle) {
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end()) {
        // Descriptor sets are freed when the pool is destroyed/reset
        buffers_.erase(it);
    }
}

void VulkanBackend::upload_buffer(BufferHandle handle, const void* data, size_t size_bytes, size_t offset) {
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;

    auto& buf = it->second.gpu_buffer;
    // For host-visible buffers, direct upload
    try {
        buf.upload(data, static_cast<VkDeviceSize>(size_bytes), static_cast<VkDeviceSize>(offset));
    } catch (...) {
        // For device-local buffers, use staging
        vk::staging_upload(ctx_.device, ctx_.physical_device, command_pool_,
                           ctx_.graphics_queue, buf.buffer(),
                           data, static_cast<VkDeviceSize>(size_bytes),
                           static_cast<VkDeviceSize>(offset));
    }
}

TextureHandle VulkanBackend::create_texture(uint32_t width, uint32_t height, const uint8_t* rgba_data) {
    TextureHandle h;
    h.id = next_texture_id_++;
    TextureEntry tex {};

    VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Create VkImage
    VkImageCreateInfo image_info {};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent        = {width, height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ctx_.device, &image_info, nullptr, &tex.image) != VK_SUCCESS) {
        textures_[h.id] = {};
        return h;
    }

    // Allocate device memory for the image
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx_.device, tex.image, &mem_reqs);

    uint32_t mem_type_idx = UINT32_MAX;
    for (uint32_t i = 0; i < ctx_.memory_properties.memoryTypeCount; ++i) {
        if ((mem_reqs.memoryTypeBits & (1 << i)) &&
            (ctx_.memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type_idx = i;
            break;
        }
    }
    if (mem_type_idx == UINT32_MAX) {
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    VkMemoryAllocateInfo alloc_info {};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type_idx;

    if (vkAllocateMemory(ctx_.device, &alloc_info, nullptr, &tex.memory) != VK_SUCCESS) {
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }
    vkBindImageMemory(ctx_.device, tex.image, tex.memory, 0);

    // Upload pixel data via staging buffer
    if (rgba_data) {
        auto staging = vk::GpuBuffer::create(
            ctx_.device, ctx_.physical_device, image_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.upload(rgba_data, image_size);

        // One-shot command buffer for layout transition + copy
        VkCommandBufferAllocateInfo cmd_alloc {};
        cmd_alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool        = command_pool_;
        cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(ctx_.device, &cmd_alloc, &cmd);

        VkCommandBufferBeginInfo begin {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier {};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = tex.image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer to image
        VkBufferImageCopy region {};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {0, 0, 0};
        region.imageExtent       = {width, height, 1};

        vkCmdCopyBufferToImage(cmd, staging.buffer(), tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit {};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx_.graphics_queue);

        vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);
        staging.destroy();
    }

    // Create image view
    VkImageViewCreateInfo view_info {};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = tex.image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(ctx_.device, &view_info, nullptr, &tex.view) != VK_SUCCESS) {
        vkFreeMemory(ctx_.device, tex.memory, nullptr);
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    // Create sampler
    VkSamplerCreateInfo sampler_info {};
    sampler_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter    = VK_FILTER_LINEAR;
    sampler_info.minFilter    = VK_FILTER_LINEAR;
    sampler_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod       = 1.0f;

    if (vkCreateSampler(ctx_.device, &sampler_info, nullptr, &tex.sampler) != VK_SUCCESS) {
        vkDestroyImageView(ctx_.device, tex.view, nullptr);
        vkFreeMemory(ctx_.device, tex.memory, nullptr);
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    // Allocate and update descriptor set for this texture
    tex.descriptor_set = allocate_descriptor_set(texture_desc_layout_);
    if (tex.descriptor_set != VK_NULL_HANDLE) {
        VkDescriptorImageInfo img_info {};
        img_info.sampler     = tex.sampler;
        img_info.imageView   = tex.view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write {};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = tex.descriptor_set;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &img_info;

        vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
    }

    textures_[h.id] = tex;
    return h;
}

void VulkanBackend::destroy_texture(TextureHandle handle) {
    auto it = textures_.find(handle.id);
    if (it != textures_.end()) {
        auto& tex = it->second;
        if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(ctx_.device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE)    vkDestroyImageView(ctx_.device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE)   vkDestroyImage(ctx_.device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)  vkFreeMemory(ctx_.device, tex.memory, nullptr);
        textures_.erase(it);
    }
}

bool VulkanBackend::begin_frame() {
    if (headless_) {
        // For offscreen, just allocate a command buffer
        if (command_buffers_.empty()) return false;
        current_cmd_ = command_buffers_[0];

        VkCommandBufferBeginInfo begin_info {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(current_cmd_, &begin_info);
        return true;
    }

    // Windowed mode
    vkWaitForFences(ctx_.device, 1, &in_flight_fences_[current_flight_frame_], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        ctx_.device, swapchain_.swapchain, UINT64_MAX,
        image_available_semaphores_[current_flight_frame_],
        VK_NULL_HANDLE, &current_image_index_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;  // Caller should recreate swapchain
    }

    vkResetFences(ctx_.device, 1, &in_flight_fences_[current_flight_frame_]);

    current_cmd_ = command_buffers_[current_flight_frame_];
    vkResetCommandBuffer(current_cmd_, 0);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(current_cmd_, &begin_info);

    return true;
}

void VulkanBackend::end_frame() {
    vkEndCommandBuffer(current_cmd_);

    if (headless_) {
        VkSubmitInfo submit {};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &current_cmd_;
        vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx_.graphics_queue);
        return;
    }

    // Windowed submit
    VkSemaphore wait_semaphores[]   = {image_available_semaphores_[current_flight_frame_]};
    VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_flight_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit {};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = wait_semaphores;
    submit.pWaitDstStageMask    = wait_stages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &current_cmd_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = signal_semaphores;

    vkQueueSubmit(ctx_.graphics_queue, 1, &submit, in_flight_fences_[current_flight_frame_]);

    VkPresentInfoKHR present {};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = signal_semaphores;
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchain_.swapchain;
    present.pImageIndices      = &current_image_index_;

    vkQueuePresentKHR(ctx_.present_queue, &present);

    current_flight_frame_ = (current_flight_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanBackend::begin_render_pass(const Color& clear_color) {
    VkClearValue clear_value {};
    clear_value.color = {{clear_color.r, clear_color.g, clear_color.b, clear_color.a}};

    VkRenderPassBeginInfo info {};
    info.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.clearValueCount = 1;
    info.pClearValues    = &clear_value;

    if (headless_) {
        info.renderPass  = offscreen_.render_pass;
        info.framebuffer = offscreen_.framebuffer;
        info.renderArea  = {{0, 0}, offscreen_.extent};
    } else {
        info.renderPass  = swapchain_.render_pass;
        info.framebuffer = swapchain_.framebuffers[current_image_index_];
        info.renderArea  = {{0, 0}, swapchain_.extent};
    }

    vkCmdBeginRenderPass(current_cmd_, &info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanBackend::end_render_pass() {
    vkCmdEndRenderPass(current_cmd_);
}

void VulkanBackend::bind_pipeline(PipelineHandle handle) {
    auto it = pipelines_.find(handle.id);
    if (it != pipelines_.end() && it->second != VK_NULL_HANDLE) {
        vkCmdBindPipeline(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
    }
}

void VulkanBackend::bind_buffer(BufferHandle handle, uint32_t binding) {
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;

    auto& entry = it->second;

    if (entry.usage == BufferUsage::Uniform && entry.descriptor_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 0, 1, &entry.descriptor_set, 0, nullptr);
    } else if (entry.usage == BufferUsage::Storage && entry.descriptor_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 1, 1, &entry.descriptor_set, 0, nullptr);
    } else {
        VkBuffer bufs[] = {entry.gpu_buffer.buffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(current_cmd_, binding, 1, bufs, offsets);
    }
}

void VulkanBackend::bind_texture(TextureHandle handle, uint32_t /*binding*/) {
    auto it = textures_.find(handle.id);
    if (it == textures_.end()) return;

    auto& tex = it->second;
    if (tex.descriptor_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                text_pipeline_layout_, 1, 1, &tex.descriptor_set, 0, nullptr);
    }
}

void VulkanBackend::push_constants(const SeriesPushConstants& pc) {
    vkCmdPushConstants(current_cmd_, pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SeriesPushConstants), &pc);
}

void VulkanBackend::set_viewport(float x, float y, float width, float height) {
    VkViewport vp {};
    vp.x        = x;
    vp.y        = y;
    vp.width    = width;
    vp.height   = height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(current_cmd_, 0, 1, &vp);
}

void VulkanBackend::set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor {};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(current_cmd_, 0, 1, &scissor);
}

void VulkanBackend::draw(uint32_t vertex_count, uint32_t first_vertex) {
    vkCmdDraw(current_cmd_, vertex_count, 1, first_vertex, 0);
}

void VulkanBackend::draw_instanced(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex) {
    vkCmdDraw(current_cmd_, vertex_count, instance_count, first_vertex, 0);
}

bool VulkanBackend::readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height) {
    if (!headless_) return false;

    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;

    auto staging = vk::GpuBuffer::create(
        ctx_.device, ctx_.physical_device, buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Record copy command
    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = command_pool_;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx_.device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkBufferImageCopy region {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd, offscreen_.color_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer(), 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit {};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.graphics_queue);

    vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);

    // Read back from staging buffer (GpuBuffer auto-maps host-visible memory)
    staging.read(out_rgba, buffer_size);
    staging.destroy();
    return true;
}

uint32_t VulkanBackend::swapchain_width() const {
    if (headless_) return offscreen_.extent.width;
    return swapchain_.extent.width;
}

uint32_t VulkanBackend::swapchain_height() const {
    if (headless_) return offscreen_.extent.height;
    return swapchain_.extent.height;
}

VkRenderPass VulkanBackend::render_pass() const {
    if (headless_) return offscreen_.render_pass;
    return swapchain_.render_pass;
}

// --- Private helpers ---

void VulkanBackend::create_command_pool() {
    VkCommandPoolCreateInfo info {};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = ctx_.queue_families.graphics.value();

    if (vkCreateCommandPool(ctx_.device, &info, nullptr, &command_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

void VulkanBackend::create_command_buffers() {
    uint32_t count = headless_ ? 1 : MAX_FRAMES_IN_FLIGHT;
    command_buffers_.resize(count);

    VkCommandBufferAllocateInfo info {};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = command_pool_;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = count;

    if (vkAllocateCommandBuffers(ctx_.device, &info, command_buffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void VulkanBackend::create_sync_objects() {
    if (headless_) return;

    image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_info {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(ctx_.device, &sem_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx_.device, &sem_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(ctx_.device, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create sync objects");
        }
    }
}

void VulkanBackend::create_descriptor_pool() {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32},
    };

    VkDescriptorPoolCreateInfo info {};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = 256;
    info.poolSizeCount = 3;
    info.pPoolSizes    = pool_sizes;

    if (vkCreateDescriptorPool(ctx_.device, &info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

VkDescriptorSet VulkanBackend::allocate_descriptor_set(VkDescriptorSetLayout layout) {
    if (descriptor_pool_ == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo alloc_info {};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(ctx_.device, &alloc_info, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return set;
}

void VulkanBackend::update_ubo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo buf_info {};
    buf_info.buffer = buffer;
    buf_info.offset = 0;
    buf_info.range  = size;

    VkWriteDescriptorSet write {};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo     = &buf_info;

    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}

void VulkanBackend::update_ssbo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo buf_info {};
    buf_info.buffer = buffer;
    buf_info.offset = 0;
    buf_info.range  = size;

    VkWriteDescriptorSet write {};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo     = &buf_info;

    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}

} // namespace plotix
