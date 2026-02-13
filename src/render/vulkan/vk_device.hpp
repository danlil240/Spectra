#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace plotix::vk {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> transfer;

    bool is_complete() const { return graphics.has_value(); }
    bool has_present() const { return present.has_value(); }
};

struct DeviceContext {
    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device = VK_NULL_HANDLE;
    VkDevice                 device          = VK_NULL_HANDLE;
    VkQueue                  graphics_queue  = VK_NULL_HANDLE;
    VkQueue                  present_queue   = VK_NULL_HANDLE;
    QueueFamilyIndices       queue_families;

    VkPhysicalDeviceProperties       properties {};
    VkPhysicalDeviceMemoryProperties memory_properties {};
};

// Instance creation
VkInstance create_instance(bool enable_validation);

// Debug messenger
VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance);
void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger);

// Physical device selection
VkPhysicalDevice pick_physical_device(VkInstance instance, VkSurfaceKHR surface = VK_NULL_HANDLE);

// Queue family discovery
QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface = VK_NULL_HANDLE);

// Logical device creation
VkDevice create_logical_device(VkPhysicalDevice physical_device,
                               const QueueFamilyIndices& indices,
                               bool enable_validation);

// Validation layer support check
bool check_validation_layer_support();

// Required device extensions
std::vector<const char*> get_required_device_extensions(bool need_swapchain);

} // namespace plotix::vk
