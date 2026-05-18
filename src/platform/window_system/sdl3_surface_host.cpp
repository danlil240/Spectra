#include "sdl3_surface_host.hpp"

#ifdef SPECTRA_USE_SDL3

    #include <SDL3/SDL.h>
    #include <SDL3/SDL_vulkan.h>

    #include <algorithm>
    #include <cstring>

namespace spectra::platform
{

void Sdl3SurfaceHost::append_instance_extensions(std::vector<const char*>& extensions) const
{
    Uint32             count    = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!sdl_exts)
        return;

    for (Uint32 i = 0; i < count; ++i)
    {
        if (sdl_exts[i]
            && std::find_if(extensions.begin(),
                            extensions.end(),
                            [&](const char* e) { return std::strcmp(e, sdl_exts[i]) == 0; })
                   == extensions.end())
        {
            extensions.push_back(sdl_exts[i]);
        }
    }
}

bool Sdl3SurfaceHost::create_surface(VkInstance    instance,
                                     void*         native_window,
                                     VkSurfaceKHR& out_surface) const
{
    auto* sdl_win = static_cast<SDL_Window*>(native_window);
    return SDL_Vulkan_CreateSurface(sdl_win, instance, nullptr, &out_surface);
}

bool Sdl3SurfaceHost::framebuffer_size(void* native_window, SurfaceSize& out_size) const
{
    auto* sdl_win = static_cast<SDL_Window*>(native_window);
    int   w = 0, h = 0;
    SDL_GetWindowSizeInPixels(sdl_win, &w, &h);
    out_size.width  = static_cast<uint32_t>(w);
    out_size.height = static_cast<uint32_t>(h);
    return w > 0 && h > 0;
}

}   // namespace spectra::platform

#endif   // SPECTRA_USE_SDL3
