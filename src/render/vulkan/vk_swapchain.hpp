#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace plotix::vk {

struct SwapchainContext {
    VkSwapchainKHR           swapchain    = VK_NULL_HANDLE;
    VkFormat                 image_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D               extent       = {0, 0};
    std::vector<VkImage>     images;
    std::vector<VkImageView> image_views;
    VkRenderPass             render_pass  = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    uint32_t                 current_image_index = 0;
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities {};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface);

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats);
VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes);
VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);

VkRenderPass create_render_pass(VkDevice device, VkFormat color_format);

SwapchainContext create_swapchain(VkDevice device,
                                  VkPhysicalDevice physical_device,
                                  VkSurfaceKHR surface,
                                  uint32_t width, uint32_t height,
                                  uint32_t graphics_family,
                                  uint32_t present_family,
                                  VkSwapchainKHR old_swapchain = VK_NULL_HANDLE,
                                  VkRenderPass reuse_render_pass = VK_NULL_HANDLE);

void destroy_swapchain(VkDevice device, SwapchainContext& ctx, bool skip_render_pass = false);

// Offscreen framebuffer for headless rendering
struct OffscreenContext {
    VkImage        color_image  = VK_NULL_HANDLE;
    VkDeviceMemory color_memory = VK_NULL_HANDLE;
    VkImageView    color_view   = VK_NULL_HANDLE;
    VkRenderPass   render_pass  = VK_NULL_HANDLE;
    VkFramebuffer  framebuffer  = VK_NULL_HANDLE;
    VkFormat       format       = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D     extent       = {0, 0};
};

OffscreenContext create_offscreen_framebuffer(VkDevice device,
                                              VkPhysicalDevice physical_device,
                                              uint32_t width, uint32_t height);

void destroy_offscreen(VkDevice device, OffscreenContext& ctx);

} // namespace plotix::vk
