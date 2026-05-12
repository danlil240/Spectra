#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace spectra::platform
{

struct SurfaceSize
{
    uint32_t width  = 0;
    uint32_t height = 0;
};

// Platform adapter seam for window-system integration.
// Render/backend code consumes this interface and stays free of Qt/GLFW headers.
class SurfaceHost
{
   public:
    virtual ~SurfaceHost() = default;

    virtual const char* name() const = 0;

    // Append required Vulkan instance extensions for this window system.
    // Implementations should avoid duplicates in the output vector.
    virtual void append_instance_extensions(std::vector<const char*>& extensions) const = 0;

    // Create a Vulkan surface from a platform-native window handle.
    virtual bool create_surface(VkInstance    instance,
                                void*         native_window,
                                VkSurfaceKHR& out_surface) const = 0;

    // Query framebuffer size in physical pixels.
    virtual bool framebuffer_size(void* native_window, SurfaceSize& out_size) const = 0;

    // Surfaceless query: does the given queue family support presentation for windows
    // created by this host on the given physical device? Used at device-creation time,
    // before any window/surface exists, so the graphics queue family can be chosen to
    // also support present. Default returns true (unknown — assume yes for back-compat).
    virtual bool query_presentation_support(VkInstance       instance,
                                            VkPhysicalDevice physical_device,
                                            uint32_t         queue_family_index) const
    {
        (void)instance;
        (void)physical_device;
        (void)queue_family_index;
        return true;
    }

    // Optional lifecycle hooks for adapters that need to observe surface changes.
    virtual void on_surface_created(void* native_window, VkSurfaceKHR surface) const
    {
        (void)native_window;
        (void)surface;
    }
    virtual void on_surface_about_to_destroy(void* native_window, VkSurfaceKHR surface) const
    {
        (void)native_window;
        (void)surface;
    }

    // Destroy a surface created by this host.
    // Default behavior matches GLFW/native Vulkan ownership.
    // Qt overrides this with a no-op because QVulkanInstance owns the surface.
    virtual void destroy_surface(VkInstance instance, VkSurfaceKHR surface) const
    {
        if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
    }
};

}   // namespace spectra::platform
