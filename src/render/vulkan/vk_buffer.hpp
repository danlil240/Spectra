#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace plotix::vk
{

class GpuBuffer
{
   public:
    GpuBuffer() = default;
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    static GpuBuffer create(VkDevice device,
                            VkPhysicalDevice physical_device,
                            VkDeviceSize size,
                            VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags memory_properties);

    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void destroy();

    void read(void* dst, VkDeviceSize size, VkDeviceSize offset = 0) const;

    VkBuffer buffer() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    bool valid() const { return buffer_ != VK_NULL_HANDLE; }
    void* mapped_data() const { return mapped_; }

   private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

class RingBuffer
{
   public:
    RingBuffer() = default;

    void init(VkDevice device,
              VkPhysicalDevice physical_device,
              VkDeviceSize frame_size,
              uint32_t frame_count,
              VkBufferUsageFlags usage);

    void destroy();

    // Advance to next frame slot
    void advance_frame();

    // Write data into current frame's region
    void write(const void* data, VkDeviceSize size, VkDeviceSize offset_in_frame = 0);

    VkBuffer buffer() const { return buffer_.buffer(); }
    VkDeviceSize current_offset() const { return current_frame_ * frame_size_; }
    VkDeviceSize frame_size() const { return frame_size_; }
    uint32_t frame_count() const { return frame_count_; }

   private:
    GpuBuffer buffer_;
    VkDeviceSize frame_size_ = 0;
    uint32_t frame_count_ = 0;
    uint32_t current_frame_ = 0;
};

// Staging upload helper: create a staging buffer, copy data, submit transfer
void staging_upload(VkDevice device,
                    VkPhysicalDevice physical_device,
                    VkCommandPool command_pool,
                    VkQueue queue,
                    VkBuffer dst_buffer,
                    const void* data,
                    VkDeviceSize size,
                    VkDeviceSize dst_offset = 0);

}  // namespace plotix::vk
