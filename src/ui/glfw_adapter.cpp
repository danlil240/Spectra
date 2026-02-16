#ifdef SPECTRA_USE_GLFW

    #include "glfw_adapter.hpp"

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
    #include <iostream>

namespace spectra
{

GlfwAdapter::~GlfwAdapter()
{
    shutdown();
}

bool GlfwAdapter::init(uint32_t width, uint32_t height, const std::string& title)
{
    if (!glfwInit())
    {
        std::cerr << "[spectra] Failed to initialize GLFW\n";
        return false;
    }

    if (!glfwVulkanSupported())
    {
        std::cerr << "[spectra] GLFW: Vulkan not supported\n";
        glfwTerminate();
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(
        static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);

    if (!window_)
    {
        std::cerr << "[spectra] Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }

    // Store this pointer for static callbacks
    glfwSetWindowUserPointer(window_, this);

    // Register callbacks
    glfwSetCursorPosCallback(window_, cursor_pos_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);
    glfwSetScrollCallback(window_, scroll_callback);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetKeyCallback(window_, key_callback);

    return true;
}

void GlfwAdapter::shutdown()
{
    destroy_window();
    terminate();
}

void GlfwAdapter::destroy_window()
{
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

void GlfwAdapter::terminate()
{
    glfwTerminate();
}

void GlfwAdapter::poll_events()
{
    glfwPollEvents();
}

void GlfwAdapter::wait_events()
{
    glfwWaitEvents();
}

bool GlfwAdapter::should_close() const
{
    return window_ ? glfwWindowShouldClose(window_) : true;
}

void* GlfwAdapter::native_window() const
{
    return static_cast<void*>(window_);
}

void GlfwAdapter::framebuffer_size(uint32_t& width, uint32_t& height) const
{
    if (window_)
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
    }
    else
    {
        width = 0;
        height = 0;
    }
}

void GlfwAdapter::set_callbacks(const InputCallbacks& callbacks)
{
    callbacks_ = callbacks;
}

void GlfwAdapter::mouse_position(double& x, double& y) const
{
    if (window_)
    {
        glfwGetCursorPos(window_, &x, &y);
    }
    else
    {
        x = 0.0;
        y = 0.0;
    }
}

void GlfwAdapter::window_pos(int& x, int& y) const
{
    if (window_)
    {
        glfwGetWindowPos(window_, &x, &y);
    }
    else
    {
        x = 0;
        y = 0;
    }
}

void GlfwAdapter::window_size(int& width, int& height) const
{
    if (window_)
    {
        glfwGetWindowSize(window_, &width, &height);
    }
    else
    {
        width = 0;
        height = 0;
    }
}

bool GlfwAdapter::is_mouse_button_pressed(int button) const
{
    if (window_)
    {
        return glfwGetMouseButton(window_, button) == GLFW_PRESS;
    }
    return false;
}

// ─── Static callback trampolines ────────────────────────────────────────────

void GlfwAdapter::cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    auto* adapter = static_cast<GlfwAdapter*>(glfwGetWindowUserPointer(window));
    if (adapter && adapter->callbacks_.on_mouse_move)
    {
        adapter->callbacks_.on_mouse_move(x, y);
    }
}

void GlfwAdapter::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto* adapter = static_cast<GlfwAdapter*>(glfwGetWindowUserPointer(window));
    if (adapter && adapter->callbacks_.on_mouse_button)
    {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        adapter->callbacks_.on_mouse_button(button, action, mods, x, y);
    }
}

void GlfwAdapter::scroll_callback(GLFWwindow* window, double x_offset, double y_offset)
{
    auto* adapter = static_cast<GlfwAdapter*>(glfwGetWindowUserPointer(window));
    if (adapter && adapter->callbacks_.on_scroll)
    {
        adapter->callbacks_.on_scroll(x_offset, y_offset);
    }
}

void GlfwAdapter::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto* adapter = static_cast<GlfwAdapter*>(glfwGetWindowUserPointer(window));
    if (adapter && adapter->callbacks_.on_resize)
    {
        adapter->callbacks_.on_resize(width, height);
    }
}

void GlfwAdapter::key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods)
{
    auto* adapter = static_cast<GlfwAdapter*>(glfwGetWindowUserPointer(window));
    if (adapter && adapter->callbacks_.on_key)
    {
        adapter->callbacks_.on_key(key, action, mods);
    }
}

}  // namespace spectra

#endif  // SPECTRA_USE_GLFW
