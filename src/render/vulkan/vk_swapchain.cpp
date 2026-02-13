#include "vk_swapchain.hpp"

#include <algorithm>
#include <stdexcept>

namespace plotix::vk {

SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count > 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, nullptr);
    if (mode_count > 0) {
        details.present_modes.resize(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, details.present_modes.data());
    }

    return details;
}

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    VkExtent2D extent = {width, height};
    extent.width  = std::clamp(extent.width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

VkRenderPass create_render_pass(VkDevice device, VkFormat color_format) {
    VkAttachmentDescription color_attachment {};
    color_attachment.format         = color_format;
    color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref {};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dependency {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info {};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color_attachment;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dependency;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &info, nullptr, &render_pass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
    return render_pass;
}

SwapchainContext create_swapchain(VkDevice device,
                                  VkPhysicalDevice physical_device,
                                  VkSurfaceKHR surface,
                                  uint32_t width, uint32_t height,
                                  uint32_t graphics_family,
                                  uint32_t present_family,
                                  VkSwapchainKHR old_swapchain) {
    auto support = query_swapchain_support(physical_device, surface);
    auto format  = choose_surface_format(support.formats);
    auto mode    = choose_present_mode(support.present_modes);
    auto extent  = choose_extent(support.capabilities, width, height);

    uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info {};
    create_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface          = surface;
    create_info.minImageCount    = image_count;
    create_info.imageFormat      = format.format;
    create_info.imageColorSpace  = format.colorSpace;
    create_info.imageExtent      = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.preTransform     = support.capabilities.currentTransform;
    create_info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode      = mode;
    create_info.clipped          = VK_TRUE;
    create_info.oldSwapchain     = old_swapchain;

    uint32_t family_indices[] = {graphics_family, present_family};
    if (graphics_family != present_family) {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    SwapchainContext ctx;
    ctx.image_format = format.format;
    ctx.extent       = extent;

    if (vkCreateSwapchainKHR(device, &create_info, nullptr, &ctx.swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    // Get images
    vkGetSwapchainImagesKHR(device, ctx.swapchain, &image_count, nullptr);
    ctx.images.resize(image_count);
    vkGetSwapchainImagesKHR(device, ctx.swapchain, &image_count, ctx.images.data());

    // Create image views
    ctx.image_views.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info {};
        view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image    = ctx.images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format   = ctx.image_format;
        view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel   = 0;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &view_info, nullptr, &ctx.image_views[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain image view");
        }
    }

    // Create render pass
    ctx.render_pass = create_render_pass(device, ctx.image_format);

    // Create framebuffers
    ctx.framebuffers.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkFramebufferCreateInfo fb_info {};
        fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass      = ctx.render_pass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments    = &ctx.image_views[i];
        fb_info.width           = extent.width;
        fb_info.height          = extent.height;
        fb_info.layers          = 1;

        if (vkCreateFramebuffer(device, &fb_info, nullptr, &ctx.framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }

    return ctx;
}

void destroy_swapchain(VkDevice device, SwapchainContext& ctx) {
    for (auto fb : ctx.framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    ctx.framebuffers.clear();

    if (ctx.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, ctx.render_pass, nullptr);
    ctx.render_pass = VK_NULL_HANDLE;

    for (auto view : ctx.image_views)
        vkDestroyImageView(device, view, nullptr);
    ctx.image_views.clear();

    if (ctx.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, ctx.swapchain, nullptr);
    ctx.swapchain = VK_NULL_HANDLE;
}

// Helper: find memory type
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

OffscreenContext create_offscreen_framebuffer(VkDevice device,
                                              VkPhysicalDevice physical_device,
                                              uint32_t width, uint32_t height) {
    OffscreenContext ctx;
    ctx.format = VK_FORMAT_R8G8B8A8_UNORM;
    ctx.extent = {width, height};

    // Create color image
    VkImageCreateInfo img_info {};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = ctx.format;
    img_info.extent        = {width, height, 1};
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &img_info, nullptr, &ctx.color_image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, ctx.color_image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info {};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, mem_reqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &ctx.color_memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate offscreen image memory");
    }
    vkBindImageMemory(device, ctx.color_image, ctx.color_memory, 0);

    // Create image view
    VkImageViewCreateInfo view_info {};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = ctx.color_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = ctx.format;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &view_info, nullptr, &ctx.color_view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen image view");
    }

    // Create render pass (final layout is TRANSFER_SRC for readback)
    VkAttachmentDescription attachment {};
    attachment.format         = ctx.format;
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref {};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkRenderPassCreateInfo rp_info {};
    rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments    = &attachment;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;

    if (vkCreateRenderPass(device, &rp_info, nullptr, &ctx.render_pass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen render pass");
    }

    // Create framebuffer
    VkFramebufferCreateInfo fb_info {};
    fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass      = ctx.render_pass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments    = &ctx.color_view;
    fb_info.width           = width;
    fb_info.height          = height;
    fb_info.layers          = 1;

    if (vkCreateFramebuffer(device, &fb_info, nullptr, &ctx.framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen framebuffer");
    }

    return ctx;
}

void destroy_offscreen(VkDevice device, OffscreenContext& ctx) {
    if (ctx.framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, ctx.framebuffer, nullptr);
    if (ctx.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, ctx.render_pass, nullptr);
    if (ctx.color_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, ctx.color_view, nullptr);
    if (ctx.color_image != VK_NULL_HANDLE)
        vkDestroyImage(device, ctx.color_image, nullptr);
    if (ctx.color_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, ctx.color_memory, nullptr);
    ctx = {};
}

} // namespace plotix::vk
