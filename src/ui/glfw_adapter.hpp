#pragma once

#ifdef SPECTRA_USE_GLFW

    #include <cstdint>
    #include <string>

struct GLFWwindow;

namespace spectra
{

class GlfwAdapter
{
   public:
    GlfwAdapter() = default;
    ~GlfwAdapter();

    GlfwAdapter(const GlfwAdapter&) = delete;
    GlfwAdapter& operator=(const GlfwAdapter&) = delete;

    // Initialize GLFW and create a window
    bool init(uint32_t width, uint32_t height, const std::string& title);

    // Shutdown and destroy window
    void shutdown();

    // Destroy only this window (does NOT call glfwTerminate).
    // Use this when closing one window in a multi-window setup.
    void destroy_window();

    // Release ownership of the window handle without destroying it.
    // Use when another owner (e.g. WindowManager) already destroyed
    // the GLFW window.
    void release_window() { window_ = nullptr; }

    // Terminate the entire GLFW library.  Call once after all windows
    // have been destroyed (typically at application exit).
    static void terminate();

    // Poll events (call once per frame)
    void poll_events();

    // Wait for events (blocks until an event arrives — used when minimized)
    void wait_events();

    // Check if window should close
    bool should_close() const;

    // Get the native window handle (for Vulkan surface creation)
    void* native_window() const;

    // Get current framebuffer size
    void framebuffer_size(uint32_t& width, uint32_t& height) const;

    // Get current mouse position
    void mouse_position(double& x, double& y) const;

    // Get window position on screen (top-left corner)
    void window_pos(int& x, int& y) const;

    // Get window size in screen coordinates
    void window_size(int& width, int& height) const;

    // Hide / show the window (for multi-window: hide primary when closed)
    void hide_window();
    void show_window();

    // Check if a mouse button is pressed
    bool is_mouse_button_pressed(int button) const;

   private:
    GLFWwindow* window_ = nullptr;
};

}  // namespace spectra

#endif  // SPECTRA_USE_GLFW
