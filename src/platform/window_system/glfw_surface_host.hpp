#pragma once

#include "surface_host.hpp"

namespace spectra::platform
{

class GlfwSurfaceHost final : public SurfaceHost
{
   public:
    const char* name() const override { return "glfw"; }
    void        append_instance_extensions(std::vector<const char*>& extensions) const override;
    bool create_surface(VkInstance    instance,
                        void*         native_window,
                        VkSurfaceKHR& out_surface) const override;
    bool framebuffer_size(void* native_window, SurfaceSize& out_size) const override;
};

}   // namespace spectra::platform
