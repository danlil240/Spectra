#pragma once

#include "platform/window_system/surface_host.hpp"

#include <memory>

#include <QtGui/QVulkanInstance>

class QWindow;

namespace spectra::adapters::qt
{

class QtSurfaceHost final : public platform::SurfaceHost
{
   public:
    QtSurfaceHost() = default;
    explicit QtSurfaceHost(QVulkanInstance* vulkan_instance)
        : injected_vulkan_instance_(vulkan_instance)
    {
    }

    const char* name() const override { return "qt"; }

    void append_instance_extensions(std::vector<const char*>& extensions) const override;
    bool create_surface(VkInstance    instance,
                        void*         native_window,
                        VkSurfaceKHR& out_surface) const override;
    bool framebuffer_size(void* native_window, platform::SurfaceSize& out_size) const override;
    void destroy_surface(VkInstance instance, VkSurfaceKHR surface) const override;

    void set_vulkan_instance(QVulkanInstance* vulkan_instance)
    {
        injected_vulkan_instance_ = vulkan_instance;
    }

   private:
    bool ensure_window_instance(VkInstance instance, QWindow* window) const;

    mutable QVulkanInstance*                 injected_vulkan_instance_ = nullptr;
    mutable std::unique_ptr<QVulkanInstance> owned_vulkan_instance_;
};

}   // namespace spectra::adapters::qt
