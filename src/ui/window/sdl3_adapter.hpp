#pragma once

#ifdef SPECTRA_USE_SDL3

    #include <cstdint>
    #include <string>

struct SDL_Window;

namespace spectra
{

class Sdl3Adapter
{
   public:
    Sdl3Adapter() = default;
    ~Sdl3Adapter();

    Sdl3Adapter(const Sdl3Adapter&)            = delete;
    Sdl3Adapter& operator=(const Sdl3Adapter&) = delete;

    bool init(uint32_t width, uint32_t height, const std::string& title);
    void shutdown();
    void destroy_window();
    void release_window() { window_ = nullptr; }

    static void terminate();

    bool  should_close() const { return should_close_; }
    void* native_window() const;
    void  framebuffer_size(uint32_t& width, uint32_t& height) const;
    void  mouse_position(double& x, double& y) const;
    void  window_pos(int& x, int& y) const;
    void  window_size(int& width, int& height) const;
    void  hide_window();
    void  show_window();
    bool  is_mouse_button_pressed(int button) const;

   private:
    SDL_Window* window_       = nullptr;
    bool        should_close_ = false;
    bool        sdl_owned_    = false;
};

}   // namespace spectra

#endif   // SPECTRA_USE_SDL3
