#ifdef SPECTRA_USE_SDL3

    #include "sdl3_adapter.hpp"

    #include <SDL3/SDL.h>

namespace spectra
{

Sdl3Adapter::~Sdl3Adapter()
{
    shutdown();
}

bool Sdl3Adapter::init(uint32_t width, uint32_t height, const std::string& title)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        return false;
    sdl_owned_ = true;

    window_ = SDL_CreateWindow(title.c_str(),
                               static_cast<int>(width),
                               static_cast<int>(height),
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    return window_ != nullptr;
}

void Sdl3Adapter::shutdown()
{
    destroy_window();
    if (sdl_owned_)
    {
        SDL_Quit();
        sdl_owned_ = false;
    }
}

void Sdl3Adapter::destroy_window()
{
    if (window_)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void Sdl3Adapter::terminate()
{
    SDL_Quit();
}

void* Sdl3Adapter::native_window() const
{
    return window_;
}

void Sdl3Adapter::framebuffer_size(uint32_t& width, uint32_t& height) const
{
    int w = 0, h = 0;
    if (window_)
        SDL_GetWindowSizeInPixels(window_, &w, &h);
    width  = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
}

void Sdl3Adapter::mouse_position(double& x, double& y) const
{
    float fx = 0.0f, fy = 0.0f;
    SDL_GetMouseState(&fx, &fy);
    x = static_cast<double>(fx);
    y = static_cast<double>(fy);
}

void Sdl3Adapter::window_pos(int& x, int& y) const
{
    if (window_)
        SDL_GetWindowPosition(window_, &x, &y);
    else
        x = y = 0;
}

void Sdl3Adapter::window_size(int& width, int& height) const
{
    if (window_)
        SDL_GetWindowSize(window_, &width, &height);
    else
        width = height = 0;
}

void Sdl3Adapter::hide_window()
{
    if (window_)
        SDL_HideWindow(window_);
}

void Sdl3Adapter::show_window()
{
    if (window_)
        SDL_ShowWindow(window_);
}

bool Sdl3Adapter::is_mouse_button_pressed(int button) const
{
    // button: 0=left, 1=right, 2=middle (GLFW convention)
    // SDL3: SDL_BUTTON_LEFT=1, SDL_BUTTON_RIGHT=3, SDL_BUTTON_MIDDLE=2
    int sdl_btn = 0;
    switch (button)
    {
        case 0:
            sdl_btn = SDL_BUTTON_LEFT;
            break;
        case 1:
            sdl_btn = SDL_BUTTON_RIGHT;
            break;
        case 2:
            sdl_btn = SDL_BUTTON_MIDDLE;
            break;
        default:
            sdl_btn = button + 1;
            break;
    }
    SDL_MouseButtonFlags state = SDL_GetMouseState(nullptr, nullptr);
    return (state & SDL_BUTTON_MASK(sdl_btn)) != 0;
}

}   // namespace spectra

#endif   // SPECTRA_USE_SDL3
