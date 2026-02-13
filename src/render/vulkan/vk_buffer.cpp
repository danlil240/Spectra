#include "vk_buffer.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace plotix::vk {

static uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                  uint32_t type_filter,
                                  VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

// --- GpuBuffer ---

GpuBuffer::~GpuBuffer() {
    destroy();
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : device_(other.device_)
    , buffer_(other.buffer_)
    , memory_(other.memory_)
    , size_(other.size_)
    , mapped_(other.mapped_)
{
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.mapped_  = nullptr;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_   = other.size_;
        mapped_ = other.mapped_;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.mapped_  = nullptr;
    }
    return *this;
}

GpuBuffer GpuBuffer::create(VkDevice device,
                             VkPhysicalDevice physical_device,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags memory_properties) {
    GpuBuffer buf;
    buf.device_ = device;
    buf.size_   = size;

    VkBufferCreateInfo buffer_info {};
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = size;
    buffer_info.usage       = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, nullptr, &buf.buffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, buf.buffer_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info {};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, mem_reqs.memoryTypeBits,
                                                   memory_properties);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &buf.memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buf.buffer_, buf.memory_, 0);

    // Persistently map host-visible buffers
    if (memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(device, buf.memory_, 0, size, 0, &buf.mapped_);
    }

    return buf;
}

void GpuBuffer::upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!mapped_) {
        throw std::runtime_error("Cannot upload to non-mapped buffer");
    }
    std::memcpy(static_cast<char*>(mapped_) + offset, data, size);
}

void GpuBuffer::read(void* dst, VkDeviceSize size, VkDeviceSize offset) const {
    if (!mapped_) {
        throw std::runtime_error("Cannot read from non-mapped buffer");
    }
    std::memcpy(dst, static_cast<const char*>(mapped_) + offset, size);
}

void GpuBuffer::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    if (mapped_) {
        vkUnmapMemory(device_, memory_);
        mapped_ = nullptr;
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

// --- RingBuffer ---

void RingBuffer::init(VkDevice device,
                      VkPhysicalDevice physical_device,
                      VkDeviceSize frame_size,
                      uint32_t frame_count,
                      VkBufferUsageFlags usage) {
    frame_size_  = frame_size;
    frame_count_ = frame_count;
    current_frame_ = 0;

    buffer_ = GpuBuffer::create(
        device, physical_device,
        frame_size * frame_count,
        usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
}

void RingBuffer::destroy() {
    buffer_.destroy();
}

void RingBuffer::advance_frame() {
    current_frame_ = (current_frame_ + 1) % frame_count_;
}

void RingBuffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset_in_frame) {
    VkDeviceSize abs_offset = current_frame_ * frame_size_ + offset_in_frame;
    buffer_.upload(data, size, abs_offset);
}

// --- Staging upload ---

void staging_upload(VkDevice device,
                    VkPhysicalDevice physical_device,
                    VkCommandPool command_pool,
                    VkQueue queue,
                    VkBuffer dst_buffer,
                    const void* data,
                    VkDeviceSize size,
                    VkDeviceSize dst_offset) {
    // Create staging buffer
    auto staging = GpuBuffer::create(
        device, physical_device, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    staging.upload(data, size);

    // Allocate one-shot command buffer
    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = command_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkBufferCopy copy_region {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = dst_offset;
    copy_region.size      = size;
    vkCmdCopyBuffer(cmd, staging.buffer(), dst_buffer, 1, &copy_region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info {};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &cmd;

    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, command_pool, 1, &cmd);
    staging.destroy();
}

} // namespace plotix::vk
