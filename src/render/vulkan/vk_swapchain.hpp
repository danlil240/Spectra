#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace spectra::vk
{

struct SwapchainContext
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat image_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    uint32_t current_image_index = 0;
    // Depth buffer (shared across all framebuffers)
    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    // MSAA resources (VK_NULL_HANDLE when msaa_samples == 1)
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    VkImage msaa_color_image = VK_NULL_HANDLE;
    VkDeviceMemory msaa_color_memory = VK_NULL_HANDLE;
    VkImageView msaa_color_view = VK_NULL_HANDLE;
    VkImage msaa_depth_image = VK_NULL_HANDLE;
    VkDeviceMemory msaa_depth_memory = VK_NULL_HANDLE;
    VkImageView msaa_depth_view = VK_NULL_HANDLE;
};

struct SwapchainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface);

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats);
VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes);
VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities,
                         uint32_t width,
                         uint32_t height);

VkRenderPass create_render_pass(VkDevice device,
                                VkFormat color_format,
                                VkFormat depth_format = VK_FORMAT_D32_SFLOAT,
                                VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT);

SwapchainContext create_swapchain(VkDevice device,
                                  VkPhysicalDevice physical_device,
                                  VkSurfaceKHR surface,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t graphics_family,
                                  uint32_t present_family,
                                  VkSwapchainKHR old_swapchain = VK_NULL_HANDLE,
                                  VkRenderPass reuse_render_pass = VK_NULL_HANDLE,
                                  VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT);

void destroy_swapchain(VkDevice device, SwapchainContext& ctx, bool skip_render_pass = false);

// Offscreen framebuffer for headless rendering
struct OffscreenContext
{
    VkImage color_image = VK_NULL_HANDLE;
    VkDeviceMemory color_memory = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D extent = {0, 0};
    // Depth buffer
    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    // MSAA resources (VK_NULL_HANDLE when msaa_samples == 1)
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    VkImage msaa_color_image = VK_NULL_HANDLE;
    VkDeviceMemory msaa_color_memory = VK_NULL_HANDLE;
    VkImageView msaa_color_view = VK_NULL_HANDLE;
    VkImage msaa_depth_image = VK_NULL_HANDLE;
    VkDeviceMemory msaa_depth_memory = VK_NULL_HANDLE;
    VkImageView msaa_depth_view = VK_NULL_HANDLE;
};

OffscreenContext create_offscreen_framebuffer(
    VkDevice device,
    VkPhysicalDevice physical_device,
    uint32_t width,
    uint32_t height,
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT);

void destroy_offscreen(VkDevice device, OffscreenContext& ctx);

// Find a suitable depth format supported by the physical device
VkFormat find_depth_format(VkPhysicalDevice physical_device);

}  // namespace spectra::vk
