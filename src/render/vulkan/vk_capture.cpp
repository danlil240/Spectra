// vk_capture.cpp — Framebuffer readback and capture operations.
// Split from vk_backend.cpp (MR-2) for focused module ownership.

#include "vk_backend.hpp"

#include <spectra/logger.hpp>

#include <cstring>
#include <vector>

#include "vk_frame_utils.hpp"

namespace spectra
{

bool VulkanBackend::readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height)
{
    // Determine source image and its current layout
    VkImage       src_image  = VK_NULL_HANDLE;
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!out_rgba || device_lost_ || !active_window_)
        return false;

    // The caller's buffer is laid out as requested_w x requested_h.  The copy
    // may be clamped to a smaller surface below; rows are then re-strided to
    // the caller's width and the remainder zero-filled.
    const uint32_t requested_w = width;
    const uint32_t requested_h = height;

    if (headless_)
    {
        src_image  = offscreen_.color_image;
        src_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        // Clamp to actual offscreen extent to prevent out-of-bounds copy
        if (width > offscreen_.extent.width)
            width = offscreen_.extent.width;
        if (height > offscreen_.extent.height)
            height = offscreen_.extent.height;
    }
    else
    {
        if (active_window_->swapchain.images.empty())
            return false;
        // Use last_presented_image_idx — current_image_index may already point
        // to the next acquired (unrendered) image when called between frames
        // (e.g. from automation poll() inside app.step()).
        src_image  = active_window_->swapchain.images[active_window_->last_presented_image_idx];
        src_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        // Clamp to actual swapchain extent to prevent out-of-bounds copy
        // (caller may pass stale dimensions from before a resize)
        if (width > active_window_->swapchain.extent.width)
            width = active_window_->swapchain.extent.width;
        if (height > active_window_->swapchain.extent.height)
            height = active_window_->swapchain.extent.height;
    }

    if (width == 0 || height == 0)
        return false;

    if (src_image == VK_NULL_HANDLE)
        return false;

    vkQueueWaitIdle(ctx_.graphics_queue);

    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Reuse persistent staging buffer when possible (headless path).
    // This avoids alloc+free every frame which is the main readback bottleneck.
    VkBuffer staging_buf  = VK_NULL_HANDLE;
    void*    mapped_ptr   = nullptr;
    bool     owns_staging = false;

    if (headless_ && offscreen_.readback_buffer != VK_NULL_HANDLE
        && offscreen_.readback_capacity >= buffer_size)
    {
        // Reuse existing persistent staging buffer
        staging_buf = offscreen_.readback_buffer;
        mapped_ptr  = offscreen_.readback_mapped_ptr;
    }
    else if (headless_)
    {
        // Grow persistent staging buffer
        auto release_readback = [this]()
        {
            if (offscreen_.readback_mapped_ptr != nullptr)
                vkUnmapMemory(ctx_.device, offscreen_.readback_memory);
            if (offscreen_.readback_buffer != VK_NULL_HANDLE)
                vkDestroyBuffer(ctx_.device, offscreen_.readback_buffer, nullptr);
            if (offscreen_.readback_memory != VK_NULL_HANDLE)
                vkFreeMemory(ctx_.device, offscreen_.readback_memory, nullptr);
            offscreen_.readback_buffer     = VK_NULL_HANDLE;
            offscreen_.readback_memory     = VK_NULL_HANDLE;
            offscreen_.readback_mapped_ptr = nullptr;
            offscreen_.readback_capacity   = 0;
        };
        release_readback();

        VkBufferCreateInfo buf_ci{};
        buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_ci.size  = buffer_size;
        buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(ctx_.device, &buf_ci, nullptr, &offscreen_.readback_buffer)
            != VK_SUCCESS)
        {
            offscreen_.readback_buffer = VK_NULL_HANDLE;
            SPECTRA_LOG_ERROR("vulkan", "readback_framebuffer: failed to create staging buffer");
            return false;
        }

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(ctx_.device, offscreen_.readback_buffer, &mem_req);

        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(ctx_.physical_device, &mem_props);
        uint32_t mem_type = UINT32_MAX;
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        {
            if ((mem_req.memoryTypeBits & (1u << i))
                && (mem_props.memoryTypes[i].propertyFlags
                    & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                       == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                           | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                mem_type = i;
                break;
            }
        }

        if (mem_type == UINT32_MAX)
        {
            release_readback();
            SPECTRA_LOG_ERROR("vulkan", "readback_framebuffer: no host-visible memory type");
            return false;
        }

        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = mem_req.size;
        alloc.memoryTypeIndex = mem_type;
        if (vkAllocateMemory(ctx_.device, &alloc, nullptr, &offscreen_.readback_memory)
            != VK_SUCCESS)
        {
            offscreen_.readback_memory = VK_NULL_HANDLE;
            release_readback();
            SPECTRA_LOG_ERROR("vulkan", "readback_framebuffer: staging allocation failed");
            return false;
        }
        if (vkBindBufferMemory(ctx_.device,
                               offscreen_.readback_buffer,
                               offscreen_.readback_memory,
                               0)
                != VK_SUCCESS
            || vkMapMemory(ctx_.device,
                           offscreen_.readback_memory,
                           0,
                           buffer_size,
                           0,
                           &offscreen_.readback_mapped_ptr)
                   != VK_SUCCESS
            || offscreen_.readback_mapped_ptr == nullptr)
        {
            offscreen_.readback_mapped_ptr = nullptr;
            release_readback();
            SPECTRA_LOG_ERROR("vulkan", "readback_framebuffer: staging bind/map failed");
            return false;
        }

        offscreen_.readback_capacity = buffer_size;
        staging_buf                  = offscreen_.readback_buffer;
        mapped_ptr                   = offscreen_.readback_mapped_ptr;
    }
    else
    {
        // Non-headless: use temporary GpuBuffer (rare path — screenshot only)
        owns_staging = true;
    }

    VkBuffer      copy_dst = staging_buf;
    vk::GpuBuffer temp_staging;
    if (owns_staging)
    {
        temp_staging = vk::GpuBuffer::create(
            ctx_.device,
            ctx_.physical_device,
            buffer_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        copy_dst = temp_staging.buffer();
    }

    // Record copy command
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = command_pool_;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx_.device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Transition source image to TRANSFER_SRC_OPTIMAL if needed
    if (src_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = src_layout;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = src_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
    }

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd,
                           src_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           copy_dst,
                           1,
                           &region);

    // Transition back to original layout if we changed it
    if (src_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                       = src_layout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = src_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.graphics_queue);

    vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);

    // BGRA swapchain formats are swizzled to RGBA for PNG export; RGBA
    // formats (and the RGBA offscreen target) are copied as-is.
    const VkFormat fmt =
        headless_ ? VK_FORMAT_R8G8B8A8_UNORM : active_window_->swapchain.image_format;
    const bool swap_rb = fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
    const bool clamped = width != requested_w || height != requested_h;

    // Read back from staging buffer
    if (owns_staging)
    {
        if (clamped)
        {
            std::vector<uint8_t> tight(static_cast<size_t>(buffer_size));
            temp_staging.read(tight.data(), buffer_size);
            vk::copy_readback_rows(out_rgba,
                                   requested_w,
                                   requested_h,
                                   tight.data(),
                                   width,
                                   height,
                                   swap_rb);
        }
        else
        {
            temp_staging.read(out_rgba, buffer_size);
            vk::copy_readback_rows(out_rgba, width, height, out_rgba, width, height, swap_rb);
        }
        temp_staging.destroy();
    }
    else
    {
        // Persistent buffer is already mapped — copy rows with the caller's stride
        vk::copy_readback_rows(out_rgba,
                               requested_w,
                               requested_h,
                               static_cast<const uint8_t*>(mapped_ptr),
                               width,
                               height,
                               swap_rb);
    }

    return true;
}

void VulkanBackend::request_framebuffer_capture(uint8_t* out_rgba, uint32_t width, uint32_t height)
{
    pending_capture_.buffer        = out_rgba;
    pending_capture_.width         = width;
    pending_capture_.height        = height;
    pending_capture_.done          = false;
    pending_capture_.target_window = nullptr;
}

void VulkanBackend::request_framebuffer_capture(uint8_t*       out_rgba,
                                                uint32_t       width,
                                                uint32_t       height,
                                                WindowContext* target_window)
{
    pending_capture_.buffer        = out_rgba;
    pending_capture_.width         = width;
    pending_capture_.height        = height;
    pending_capture_.done          = false;
    pending_capture_.target_window = target_window;
}

bool VulkanBackend::do_capture_before_present()
{
    if (!pending_capture_.buffer)
        return false;
    if (device_lost_)
    {
        pending_capture_.buffer = nullptr;
        return false;
    }

    // If a target window is specified, only capture when active_window_ matches
    if (pending_capture_.target_window && active_window_ != pending_capture_.target_window)
        return false;

    uint32_t width  = pending_capture_.width;
    uint32_t height = pending_capture_.height;
    // The capture buffer is laid out as requested; a clamped copy is re-strided.
    const uint32_t requested_w = width;
    const uint32_t requested_h = height;

    VkImage       src_image  = VK_NULL_HANDLE;
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (headless_)
    {
        src_image  = offscreen_.color_image;
        src_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        if (width > offscreen_.extent.width)
            width = offscreen_.extent.width;
        if (height > offscreen_.extent.height)
            height = offscreen_.extent.height;
    }
    else
    {
        if (active_window_->swapchain.images.empty())
        {
            pending_capture_.buffer = nullptr;
            return false;
        }
        src_image  = active_window_->swapchain.images[active_window_->current_image_index];
        src_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        if (width > active_window_->swapchain.extent.width)
            width = active_window_->swapchain.extent.width;
        if (height > active_window_->swapchain.extent.height)
            height = active_window_->swapchain.extent.height;
    }

    if (width == 0 || height == 0 || src_image == VK_NULL_HANDLE)
    {
        pending_capture_.buffer = nullptr;
        return false;
    }

    // Wait for rendering to complete before reading back
    vkQueueWaitIdle(ctx_.graphics_queue);

    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;
    auto         staging     = vk::GpuBuffer::create(
        ctx_.device,
        ctx_.physical_device,
        buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = command_pool_;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx_.device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    if (src_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = src_layout;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = src_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
    }

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd,
                           src_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer(),
                           1,
                           &region);

    if (src_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                       = src_layout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = src_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo capture_submit{};
    capture_submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    capture_submit.commandBufferCount = 1;
    capture_submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx_.graphics_queue, 1, &capture_submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.graphics_queue);

    // Swizzle BGRA → RGBA if needed
    VkFormat   fmt = headless_ ? VK_FORMAT_R8G8B8A8_UNORM : active_window_->swapchain.image_format;
    const bool swap_rb = fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;

    if (width != requested_w || height != requested_h)
    {
        std::vector<uint8_t> tight(static_cast<size_t>(buffer_size));
        staging.read(tight.data(), buffer_size);
        vk::copy_readback_rows(pending_capture_.buffer,
                               requested_w,
                               requested_h,
                               tight.data(),
                               width,
                               height,
                               swap_rb);
    }
    else
    {
        staging.read(pending_capture_.buffer, buffer_size);
        vk::copy_readback_rows(pending_capture_.buffer,
                               width,
                               height,
                               pending_capture_.buffer,
                               width,
                               height,
                               swap_rb);
    }

    vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);
    staging.destroy();

    pending_capture_.done   = true;
    pending_capture_.buffer = nullptr;
    return true;
}

}   // namespace spectra
