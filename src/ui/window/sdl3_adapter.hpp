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

    // Event pumping, mirroring GlfwAdapter (single-window path without a
    // WindowManager).  Drain the SDL queue and set should_close() on quit or
    // a close request for this adapter's window.
    void poll_events();
    // Block until an event arrives, then drain.
    void wait_events();
    // Block until an event arrives or timeout_seconds pass, then drain.
    void wait_events_timeout(double timeout_seconds);

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
