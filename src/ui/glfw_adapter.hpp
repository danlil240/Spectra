#pragma once

#ifdef PLOTIX_USE_GLFW

#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;

namespace plotix {

class Axes;

// Callback types for input events
struct InputCallbacks {
    std::function<void(double x, double y)>                  on_mouse_move;
    std::function<void(int button, int action, double x, double y)> on_mouse_button;
    std::function<void(double x_offset, double y_offset)>    on_scroll;
    std::function<void(int width, int height)>               on_resize;
};

class GlfwAdapter {
public:
    GlfwAdapter() = default;
    ~GlfwAdapter();

    GlfwAdapter(const GlfwAdapter&) = delete;
    GlfwAdapter& operator=(const GlfwAdapter&) = delete;

    // Initialize GLFW and create a window
    bool init(uint32_t width, uint32_t height, const std::string& title);

    // Shutdown and destroy window
    void shutdown();

    // Poll events (call once per frame)
    void poll_events();

    // Check if window should close
    bool should_close() const;

    // Get the native window handle (for Vulkan surface creation)
    void* native_window() const;

    // Get current framebuffer size
    void framebuffer_size(uint32_t& width, uint32_t& height) const;

    // Set input callbacks
    void set_callbacks(const InputCallbacks& callbacks);

    // Get current mouse position
    void mouse_position(double& x, double& y) const;

    // Check if a mouse button is pressed
    bool is_mouse_button_pressed(int button) const;

private:
    GLFWwindow* window_ = nullptr;
    InputCallbacks callbacks_;

    // Static callback trampolines (GLFW uses C callbacks)
    static void cursor_pos_callback(GLFWwindow* window, double x, double y);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void scroll_callback(GLFWwindow* window, double x_offset, double y_offset);
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
};

} // namespace plotix

#endif // PLOTIX_USE_GLFW
