#ifdef SPECTRA_USE_GLFW

    #include "glfw_adapter.hpp"

    #include "glfw_utils.hpp"
    #include "window_placement.hpp"
    #include <spectra/logger.hpp>

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
namespace spectra
{

GlfwAdapter::~GlfwAdapter()
{
    shutdown();
}

bool GlfwAdapter::init(uint32_t width, uint32_t height, const std::string& title)
{
    configure_glfw_platform();
    if (!glfwInit())
    {
        SPECTRA_LOG_ERROR("glfw", "Failed to initialize GLFW");
        return false;
    }

    #ifndef __EMSCRIPTEN__
    // Browser builds render through WebGPU onto the canvas, not Vulkan.
    if (!glfwVulkanSupported())
    {
        SPECTRA_LOG_ERROR("glfw", "GLFW: Vulkan not supported");
        glfwTerminate();
        return false;
    }
    #endif

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // No OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    #ifdef __EMSCRIPTEN__
    // Size the canvas backing store at devicePixelRatio x its CSS size, so the
    // browser does not upscale (and blur) the plot on HiDPI displays.  The
    // window size stays in CSS pixels, which is what mouse events report.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    #endif
    set_wayland_app_id();

    #ifndef __EMSCRIPTEN__
    // Never open larger than the screen: a 1500x900 default overflows a small
    // laptop display.  Scale down (keeping the aspect ratio) to fit the
    // primary monitor's work area with a margin.
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
    {
        ScreenWorkArea wa;
        glfwGetMonitorWorkarea(mon, &wa.x, &wa.y, &wa.w, &wa.h);
        const auto fit =
            fit_window_to_work_area(static_cast<int>(width), static_cast<int>(height), wa);
        if (fit.first != static_cast<int>(width) || fit.second != static_cast<int>(height))
        {
            SPECTRA_LOG_INFO("glfw",
                             "Window {}x{} does not fit the {}x{} work area; opening at {}x{}",
                             width,
                             height,
                             wa.w,
                             wa.h,
                             fit.first,
                             fit.second);
            width  = static_cast<uint32_t>(fit.first);
            height = static_cast<uint32_t>(fit.second);
        }
    }
    #endif

    window_ = glfwCreateWindow(static_cast<int>(width),
                               static_cast<int>(height),
                               title.c_str(),
                               nullptr,
                               nullptr);

    if (!window_)
    {
        SPECTRA_LOG_ERROR("glfw", "Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    #ifndef __EMSCRIPTEN__
    // The OS picks the first window's position.  Windows only keeps a new
    // window on its monitor, not inside the work area, so a tall window in a
    // late cascade slot ended under the taskbar.  Pull it into the work area,
    // as WindowManager::create_window() does for every later window.
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
    {
        ScreenWorkArea wa;
        glfwGetMonitorWorkarea(mon, &wa.x, &wa.y, &wa.w, &wa.h);
        int x = 0, y = 0, cw = 0, ch = 0, fl = 0, ft = 0, fr = 0, fb = 0;
        glfwGetWindowPos(window_, &x, &y);
        glfwGetWindowSize(window_, &cw, &ch);
        glfwGetWindowFrameSize(window_, &fl, &ft, &fr, &fb);
        const auto pos = keep_window_in_work_area(x, y, cw, ch, fl, ft, fr, fb, wa);
        if (pos.first != x || pos.second != y)
            glfwSetWindowPos(window_, pos.first, pos.second);
    }
    #endif

    // NOTE: No GLFW callbacks are installed here.  WindowManager owns all
    // callbacks for every window (initial and secondary).  Installing
    // callbacks here would cause ImGui's callback chaining to invoke them
    // with the wrong user-pointer type (WindowManager* vs GlfwAdapter*),
    // resulting in a segfault.

    set_window_icon(window_);

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
    return (window_ ? glfwWindowShouldClose(window_) : 1) != 0;
}

void* GlfwAdapter::native_window() const
{
    return static_cast<void*>(window_);
}

void GlfwAdapter::framebuffer_size(uint32_t& width, uint32_t& height) const
{
    if (window_)
    {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        width  = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
    }
    else
    {
        width  = 0;
        height = 0;
    }
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
        width  = 0;
        height = 0;
    }
}

void GlfwAdapter::hide_window()
{
    if (window_)
        glfwHideWindow(window_);
}

void GlfwAdapter::show_window()
{
    if (window_)
        glfwShowWindow(window_);
}

bool GlfwAdapter::is_mouse_button_pressed(int button) const
{
    if (window_)
    {
        return glfwGetMouseButton(window_, button) == GLFW_PRESS;
    }
    return false;
}

}   // namespace spectra

#endif   // SPECTRA_USE_GLFW
