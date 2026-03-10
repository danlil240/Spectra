#include "qt_surface_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <QtCore/QSize>
#include <QtGui/QWindow>

namespace spectra::adapters::qt
{

namespace
{

void append_unique(std::vector<const char*>& extensions, const char* extension_name)
{
    if (!extension_name)
    {
        return;
    }

    if (std::find(extensions.begin(), extensions.end(), extension_name) == extensions.end())
    {
        extensions.push_back(extension_name);
    }
}

std::unordered_set<std::string> query_instance_extensions()
{
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_extensions.data());

    std::unordered_set<std::string> names;
    names.reserve(available_extensions.size());
    for (const auto& ext : available_extensions)
    {
        names.insert(ext.extensionName);
    }
    return names;
}

}   // namespace

void QtSurfaceHost::append_instance_extensions(std::vector<const char*>& extensions) const
{
    const auto available = query_instance_extensions();

    auto append_if_supported = [&](const char* extension_name)
    {
        if (!extension_name)
        {
            return;
        }

        if (available.find(extension_name) != available.end())
        {
            append_unique(extensions, extension_name);
        }
    };

    append_if_supported(VK_KHR_SURFACE_EXTENSION_NAME);

    // Enabling multiple supported platform surface extensions is safe and lets
    // the same build run on either X11/Wayland/Win32/macOS backends.
    static constexpr std::array<const char*, 6> k_platform_surface_extensions = {
        "VK_KHR_wayland_surface",
        "VK_KHR_xcb_surface",
        "VK_KHR_xlib_surface",
        "VK_KHR_win32_surface",
        "VK_EXT_metal_surface",
        "VK_MVK_macos_surface",
    };

    for (const char* extension_name : k_platform_surface_extensions)
    {
        append_if_supported(extension_name);
    }
}

bool QtSurfaceHost::create_surface(VkInstance    instance,
                                   void*         native_window,
                                   VkSurfaceKHR& out_surface) const
{
    out_surface = VK_NULL_HANDLE;

    if (instance == VK_NULL_HANDLE || !native_window)
    {
        return false;
    }

    auto* window = static_cast<QWindow*>(native_window);
    if (!window)
    {
        return false;
    }

    if (window->surfaceType() != QSurface::VulkanSurface)
    {
        window->setSurfaceType(QSurface::VulkanSurface);
    }

    if (!ensure_window_instance(instance, window))
    {
        return false;
    }

    auto* window_instance = window->vulkanInstance();
    if (!window_instance)
    {
        return false;
    }

    out_surface = window_instance->surfaceForWindow(window);
    return out_surface != VK_NULL_HANDLE;
}

bool QtSurfaceHost::framebuffer_size(void* native_window, platform::SurfaceSize& out_size) const
{
    auto* window = static_cast<QWindow*>(native_window);
    if (!window)
    {
        return false;
    }

    const QSize logical_size = window->size();
    const qreal dpr          = window->devicePixelRatio();

    const int pixel_width  = static_cast<int>(std::lround(logical_size.width() * dpr));
    const int pixel_height = static_cast<int>(std::lround(logical_size.height() * dpr));

    if (pixel_width <= 0 || pixel_height <= 0)
    {
        out_size.width  = 0;
        out_size.height = 0;
        return false;
    }

    out_size.width  = static_cast<uint32_t>(pixel_width);
    out_size.height = static_cast<uint32_t>(pixel_height);
    return true;
}

void QtSurfaceHost::destroy_surface(VkInstance instance, VkSurfaceKHR surface) const
{
    (void)instance;
    (void)surface;
    // VkSurfaceKHR returned by QVulkanInstance::surfaceForWindow() is owned by Qt.
    // Destroying it manually would double-free during QVulkanInstance teardown.
}

bool QtSurfaceHost::ensure_window_instance(VkInstance instance, QWindow* window) const
{
    if (!window)
    {
        return false;
    }

    QVulkanInstance* window_instance = injected_vulkan_instance_;
    if (!window_instance)
    {
        if (!owned_vulkan_instance_)
        {
            owned_vulkan_instance_ = std::make_unique<QVulkanInstance>();
        }
        window_instance = owned_vulkan_instance_.get();
    }

    if (!window_instance)
    {
        return false;
    }

    if (window_instance->vkInstance() == VK_NULL_HANDLE)
    {
        window_instance->setVkInstance(instance);
    }

    if (window_instance->vkInstance() != instance)
    {
        return false;
    }

    if (window->vulkanInstance() != window_instance)
    {
        window->setVulkanInstance(window_instance);
    }

    return window->vulkanInstance() == window_instance;
}

}   // namespace spectra::adapters::qt
